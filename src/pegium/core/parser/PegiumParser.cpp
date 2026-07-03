#include <pegium/core/parser/PegiumParser.hpp>

#include <limits>
#include <optional>
#include <stdexcept>

#include <pegium/core/parser/AssignmentHelpers.hpp>
#include <pegium/core/parser/CstSearch.hpp>
#include <pegium/core/parser/ParseDiagnostics.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoverySearch.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/utils/Cancellation.hpp>

#if defined(PEGIUM_ENABLE_STEP_TRACE)
#include <iostream>
#endif

namespace pegium::parser {

namespace {

#if defined(PEGIUM_ENABLE_STEP_TRACE)
/// Dumps the step counters to stderr at process exit so each test
/// binary contributes to the corpus-wide view of `Wins/Runs` ratios
/// for the global compensations. Compiled in only when
/// `PEGIUM_ENABLE_STEP_TRACE` is defined; the production parser pays
/// nothing for this hook in release builds.
struct StepTraceAtExitDumper {
  ~StepTraceAtExitDumper() noexcept {
    std::cerr << "[step-trace-atexit] step counters at process exit:\n";
    detail::stepTraceDumpSummary(std::cerr);
  }
};
[[maybe_unused]] const StepTraceAtExitDumper stepTraceAtExitDumper;
#endif


struct StandaloneCoreServices {
  pegium::SharedCoreServices shared;
  pegium::CoreServices core;

  StandaloneCoreServices() : core(shared) {}
};

const pegium::CoreServices &standalone_core_services() noexcept {
  // Construct in place and install into it — never return-by-value: the
  // installed services hold back-references to `instance`, and CoreServices is
  // non-movable, so a returned-by-value copy would dangle (and not compile).
  static StandaloneCoreServices instance;
  [[maybe_unused]] static const bool initialized = [] {
    pegium::installDefaultSharedCoreServices(instance.shared);
    pegium::installDefaultCoreServices(instance.core);
    return true;
  }();
  return instance.core;
}

} // namespace

PegiumParser::PegiumParser() noexcept
    : pegium::DefaultCoreService(standalone_core_services()) {}

ParseResult PegiumParser::parse(text::TextSnapshot text,
                                const utils::CancellationToken &cancelToken) const {
  // TextOffset is 32-bit: a larger input would silently truncate offsets and
  // misparse. Reject it with a clear error instead.
  if (text.size() > std::numeric_limits<TextOffset>::max()) {
    throw std::length_error(
        "pegium: input exceeds the maximum supported size (4 GiB)");
  }
  const auto &entryRule = getEntryRule();
  const auto &skipper = getSkipper();
  ParseOptions options = getParseOptions();
  if constexpr (!detail::kRecoveryCompiledIn) {
    // Every rule's recovery-mode ops slots are null in this build (see
    // ParseExpression.hpp/InfixRule.hpp), so a recovery attempt would
    // dereference a null function pointer. Forcing the option off here keeps
    // this build's observable behavior identical to running a recovery-
    // capable build with `ParseOptions::recoveryEnabled = false`:
    // `RecoverySearch.cpp` already bails before ever touching a rule's
    // recovery path when this flag is false.
    options.recoveryEnabled = false;
  }
  ParseResult result;
  const auto inputSize = static_cast<TextOffset>(text.size());
  auto recoverySearch =
      detail::orchestrate_recovery_search(entryRule, skipper, options, text,
                                          cancelToken);
  auto &selectedAttempt = recoverySearch.selectedAttempt;
  const auto &selectedWindows = recoverySearch.selectedWindows;
  const auto failureVisibleCursorOffset =
      recoverySearch.failureVisibleCursorOffset;
  utils::throw_if_cancelled(cancelToken);

  result.cst = std::move(selectedAttempt.cst);
  result.fullMatch = selectedAttempt.fullMatch;
  result.parsedLength = selectedAttempt.parsedLength;
  result.lastVisibleCursorOffset =
      selectedAttempt.failureSnapshot.has_value() &&
              !selectedAttempt.failureSnapshot->failureLeafHistory.empty()
          ? selectedAttempt.failureSnapshot->failureLeafHistory.back().endOffset
          : selectedAttempt.lastVisibleCursorOffset;
  result.failureVisibleCursorOffset = failureVisibleCursorOffset;
  result.maxCursorOffset = selectedAttempt.maxCursorOffset;
  auto syntaxDiagnostics = selectedAttempt.recoveryEdits;
  std::optional<RecoveryWindowReport> lastRecoveryWindow;
  if (!selectedWindows.empty()) {
    lastRecoveryWindow = RecoveryWindowReport{
        .beginOffset = selectedWindows.back().beginOffset,
        .maxCursorOffset = selectedWindows.back().maxCursorOffset,
        .backwardTokenCount = selectedWindows.back().tokenCount,
        .forwardTokenCount = selectedWindows.back().forwardTokenCount,
    };
  }
  result.recoveryReport = {
      .hasRecovered = !selectedWindows.empty(),
      .fullRecovered = !selectedWindows.empty() && result.fullMatch,
      .recoveryCount = static_cast<std::uint32_t>(selectedWindows.size()),
      .recoveryWindowsTried = recoverySearch.recoveryWindowsTried,
      .strictParseRuns = recoverySearch.strictParseRuns,
      .recoveryAttemptRuns = recoverySearch.recoveryAttemptRuns,
      .recoveryEdits = selectedAttempt.editCount,
      .choiceRecoverCacheHits = recoverySearch.choiceRecoverCacheHits,
      .choiceRecoverCacheMisses = recoverySearch.choiceRecoverCacheMisses,
      .lastRecoveryWindow = std::move(lastRecoveryWindow),
  };
  if (!syntaxDiagnostics.empty() || !result.fullMatch ||
      result.recoveryReport.hasRecovered) {
    detail::append_syntax_summary_entry(syntaxDiagnostics, result.cst.get(),
                                        result.parsedLength,
                                        result.lastVisibleCursorOffset,
                                        result.failureVisibleCursorOffset,
                                        inputSize,
                                        result.recoveryReport.hasRecovered,
                                        result.fullMatch, options.recoveryEnabled);
  }
  result.parseDiagnostics =
      detail::materialize_syntax_diagnostics(syntaxDiagnostics);
  if (selectedAttempt.entryRuleMatched) {
    auto matchedNode = detail::findFirstRootMatchingNode(*result.cst, &entryRule);
    if (!matchedNode.has_value()) {
      matchedNode = detail::findFirstMatchingNode(*result.cst, &entryRule);
    }
    if (matchedNode.has_value()) {
      result.astArena = std::make_unique<AstArena>(*result.cst);
      const ValueBuildContext context{
          .references = &result.references,
          .linker = services.references.linker.get(),
          .diagnostics = &result.parseDiagnostics,
          .arena = result.astArena.get(),
      };
      result.value = entryRule.getValue(*matchedNode, context);
    }
  }

  detail::stepTraceDumpSummary(entryRule.getName(), result.fullMatch,
                               !result.parseDiagnostics.empty(),
                               result.parsedLength, inputSize);
  PEGIUM_RECOVERY_TRACE("[parser result] full=", result.fullMatch,
                        " parsed=", result.parsedLength, "/", inputSize,
                        " diag=", result.parseDiagnostics.size(),
                        " recovered=", result.recoveryReport.hasRecovered,
                        " fullRecovered=", result.recoveryReport.fullRecovered,
                        " windows=", result.recoveryReport.recoveryCount,
                        " edits=", result.recoveryReport.recoveryEdits);
  return result;
}

ExpectResult PegiumParser::expect(
    std::string_view text, TextOffset offset,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  // TextOffset is 32-bit: a larger input would silently truncate offsets and
  // misparse. Reject it here too, so expect() agrees with parse() (above).
  if (text.size() > std::numeric_limits<TextOffset>::max()) {
    throw std::length_error(
        "pegium: input exceeds the maximum supported size (4 GiB)");
  }
  const auto &entryRule = getEntryRule();
  const auto &skipper = getSkipper();
  const auto options = getParseOptions();
  ExpectContext ctx{text, skipper, offset, cancelToken};
  ctx.editPolicy.maxConsecutiveCodepointDeletes =
      options.maxConsecutiveCodepointDeletes;
  ctx.editPolicy.maxEditsPerAttempt = options.maxRecoveryEditsPerAttempt;
  ctx.editPolicy.maxEditCost = options.maxRecoveryEditCost;
  ctx.skip();

  ExpectResult result;
  result.offset =
      std::min<TextOffset>(offset, static_cast<TextOffset>(text.size()));
  result.reachedAnchor = false;
  if (entryRule.expect(ctx)) {
    result.frontier = std::move(ctx.frontier);
    result.reachedAnchor = ctx.reachedAnchor() || !result.frontier.empty();
  }
  return result;
}

} // namespace pegium::parser
