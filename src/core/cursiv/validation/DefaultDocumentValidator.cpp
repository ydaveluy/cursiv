#include "cursiv/validation/DefaultDocumentValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/parser/ParserErrorMessageProvider.hpp"
#include "cursiv/references/Linker.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {
namespace {

// Batch size for long validation walks and for collecting diagnostics.
constexpr std::size_t kCheckRun = 512;

// The validator whose default node options answered last on this thread.
thread_local const DefaultDocumentValidator* acceptsEveryNode = nullptr;

// The nodes a language takes out of the walk: those it refuses, and those
// under one it prunes. `ask` answers each node but `first`, which `said`.
template <class Ask>
std::unordered_set<const AstNode*> refused_nodes(
    OwnedNodes nodes, const AstNode& first, ValidateSingleNodeOptions said,
    TaskScheduler& scheduler, const CancellationToken& token, Ask ask) {
  using Nodes = std::vector<const AstNode*>;
  const auto note = [](const AstNode& node, ValidateSingleNodeOptions options,
                       Nodes& unchecked, Nodes& pruned) {
    if (!options.validateNode) unchecked.push_back(&node);
    if (!options.validateChildren) pruned.push_back(&node);
  };
  const auto askRun = [&](OwnedNodes run, Nodes& unchecked, Nodes& pruned) {
    for (const AstNode* const node : run) {
      throwIfCancelled(token);
      if (node != &first) note(*node, ask(*node), unchecked, pruned);
    }
  };
  Nodes unchecked;
  Nodes pruned;
  note(first, said, unchecked, pruned);
  if (nodes.size() < 2 * kCheckRun || scheduler.workerCount() == 0) {
    askRun(nodes, unchecked, pruned);
  } else {
    std::vector<std::pair<Nodes, Nodes>> runs(nodes.size() / kCheckRun);
    scheduler.parallelFor(token, nodes, kCheckRun, [&](OwnedNodes run) {
      auto& [uncheckedHere, prunedHere] =
          runs[static_cast<std::size_t>(run.data() - nodes.data()) / kCheckRun];
      askRun(run, uncheckedHere, prunedHere);
    });
    for (const auto& [uncheckedHere, prunedHere] : runs) {
      unchecked.insert(unchecked.end(), uncheckedHere.begin(),
                       uncheckedHere.end());
      pruned.insert(pruned.end(), prunedHere.begin(), prunedHere.end());
    }
  }
  std::unordered_set<const AstNode*> refused(unchecked.begin(),
                                             unchecked.end());
  if (pruned.empty()) return refused;

  // Shared ancestry is memoised: a deep or widely pruned tree stays linear.
  const std::unordered_set<const AstNode*> cut(pruned.begin(), pruned.end());
  std::unordered_map<const AstNode*, bool> ancestry;
  Nodes path;
  for (const AstNode* const node : nodes) {
    path.clear();
    bool under = false;
    for (const AstNode* up = node->parent; up != nullptr; up = up->parent) {
      if (cut.contains(up)) {
        under = true;
        break;
      }
      if (const auto known = ancestry.find(up); known != ancestry.end()) {
        under = known->second;
        break;
      }
      throwIfCancelled(token);
      path.push_back(up);
    }
    for (const AstNode* up : path) ancestry.emplace(up, under);
    if (under) refused.insert(node);
  }
  return refused;
}

// Naming no category runs everything; naming some runs the built-in half only
// when `kBuiltIn` is among them, and the checks only when something else is.
[[nodiscard]] bool runs_checks(ValidationCategories categories) noexcept {
  return categories.empty() ||
         std::ranges::any_of(categories, [](const ValidationCategory* c) {
           return c != &kBuiltIn;
         });
}
// Whether the parse reported a fault; a `bounded` note alone is none.
[[nodiscard]] bool said_a_fault(const ParseResult& parsed) noexcept {
  return std::ranges::any_of(parsed.diagnostics, [](const ParseDiagnostic& d) {
    return d.kind != DiagnosticKind::Bounded;
  });
}

// Whether `processParsingErrors` says this parse went wrong: the verdicts, or a
// fault the parser wrote.
[[nodiscard]] bool said_a_parse_error(const ParseResult& parsed) noexcept {
  return !parsed.accepted || parsed.error != AstBuildError::None ||
         said_a_fault(parsed);
}

}  // namespace

void DefaultDocumentValidator::validateDocument(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const ValidationOptions& options, const CancellationToken& token) const {
  throwIfCancelled(token);
  const std::span<const ValidationCategory* const> categories =
      options.categories;
  // Both halves are filtered, so a client asking only for fast checks does not
  // pay the walk over `document.references()` on every keystroke.
  const bool builtIn = detail::runs_category(categories, &kBuiltIn);
  if (builtIn) processParsingErrors(document, diagnostics, token);
  // The stops belong to built-in validation, so a filtered pass runs only
  // its selected checks.
  if (const bool stopOnParse = builtIn && options.stopAfterParsingErrors &&
                               said_a_parse_error(document.parseResult);
      !stopOnParse) {
    const std::size_t beforeLinking = diagnostics.size();
    if (builtIn) processLinkingErrors(document, diagnostics, token);
    // The linking stop is a count: a broken reference is exactly what the half
    // above publishes.
    const bool stopOnLinking =
        options.stopAfterLinkingErrors && diagnostics.size() > beforeLinking;
    if (!stopOnLinking && runs_checks(categories))
      validateAst(document, diagnostics, options, token);
  }
  // Preserve emission order at equal offsets. Two sorted runs merge directly.
  const auto position = [](const Diagnostic& d) { return d.range.begin; };
  const auto middle = std::ranges::is_sorted_until(diagnostics, {}, position);
  if (middle != diagnostics.end() &&
      std::ranges::is_sorted(middle, diagnostics.end(), {}, position)) {
    std::ranges::inplace_merge(diagnostics, middle, {}, position);
  } else if (middle != diagnostics.end()) {
    std::vector<std::pair<TextOffset, std::size_t>> order;
    order.reserve(diagnostics.size());
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
      order.emplace_back(diagnostics[i].range.begin, i);
    std::ranges::sort(order);
    std::vector<Diagnostic> ordered;
    ordered.reserve(diagnostics.size());
    for (const auto& [at, i] : order)
      ordered.push_back(std::move(diagnostics[i]));
    diagnostics.swap(ordered);
  }
  throwIfCancelled(token);
}

// The tokens a repair needed, as edit rows the code-action provider turns into
// quick fixes: a misspelling's are written over its text, any other repair's
// after the text it covers, which stays as the author wrote it.
void DefaultDocumentValidator::attachRepairActions(
    const Document& document, TextRange range,
    const ParseDiagnostic& parseDiagnostic, Diagnostic& diagnostic) const {
  // The repair names its tokens, and the grammar spells them. A deletion names
  // the token it resumed on, already in the text, so it gets no fix.
  if (parseDiagnostic.element == kNoElement ||
      parseDiagnostic.kind == DiagnosticKind::Deleted)
    return;

  constexpr std::size_t kMaxActions = 4;
  const std::vector<std::string_view> spellings = expectedSpellings(
      services.parser->getGrammar(), parseDiagnostic, kMaxActions);
  if (spellings.empty()) return;

  const std::string_view text = document.textDocument().getText();
  const bool insert = parseDiagnostic.kind != DiagnosticKind::Replaced ||
                      range.begin == range.end;
  const TextRange at = insert ? TextRange{range.end, range.end} : range;
  const std::string_view replaced =
      insert || range.end > text.size()
          ? std::string_view{}
          : text.substr(range.begin, range.end - range.begin);

  JsonValue::Array rows;
  for (const std::string_view spelling : spellings) {
    if (spelling == replaced) continue;
    rows.push_back(detail::make_action_row(
        (insert ? "Insert '" : "Replace with '") + std::string(spelling) + "'",
        at, std::string(spelling), document.buildRevision));
  }
  if (rows.empty()) return;

  JsonValue::Object payload;
  payload.try_emplace(std::string(detail::kDefaultCodeActionsKey),
                      std::move(rows));
  diagnostic.data = JsonValue(std::move(payload));
}

// One diagnostic per parse diagnostic, its code from its kind. The range comes
// from the parse diagnostic, so a sentence with no node still has a place.
void DefaultDocumentValidator::processParsingErrors(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const CancellationToken& token) const {
  const ParseResult& parsed = document.parseResult;
  // A failed parse with no repair would read as a clean document: the parse or
  // build verdict is reported instead.
  if (said_a_parse_error(parsed) && !said_a_fault(parsed))
    diagnostics.push_back(unrepairedParseError(document));
  // Not gated on the tree: a document too damaged to build one still reports
  // its repairs.
  if (parsed.diagnostics.empty()) return;
  throwIfCancelled(token);
  const std::string_view source = getSource();
  const std::string_view text = document.textDocument().getText();
  const Grammar& grammar = services.parser->getGrammar();
  const ParserErrorMessageProvider& words =
      *services.parserErrorMessageProvider;
  diagnostics.reserve(diagnostics.size() + parsed.diagnostics.size());
  for (const ParseDiagnostic& d : parsed.diagnostics) {
    throwIfCancelled(token);
    std::string_view code = kParseRepaired;
    if (d.kind == DiagnosticKind::ConversionError) code = kConversionError;
    Diagnostic diagnostic;
    diagnostic.range = TextRange{d.begin, d.end};
    diagnostic.severity = d.kind == DiagnosticKind::Bounded
                              ? DiagnosticSeverity::Information
                              : DiagnosticSeverity::Error;
    diagnostic.message = words.buildMessage(grammar, d, text);
    diagnostic.source = source;
    // A quick fix is built from the element the repair expected.
    if (d.isSyntax())
      attachRepairActions(document, diagnostic.range, d, diagnostic);
    setDataCode(diagnostic, code);
    diagnostics.push_back(std::move(diagnostic));
  }
}

// Where the parse stopped, said plainly. Two shapes, and the second is the one
// a reader can act on: an entry rule that matched and left text behind names
// that text, while one that never completed can only name the document.
Diagnostic DefaultDocumentValidator::unrepairedParseError(
    const Document& document) const {
  const std::string_view text = document.textDocument().getText();
  const Cst& cst = document.parseResult.cst();
  const ParserErrorMessageProvider& words =
      *services.parserErrorMessageProvider;
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.source = getSource();
  // The syntax code, never the repair one: nothing was repaired here, and no
  // quick fix is attached to it -- a client filtering on `parse-repaired`
  // must not be offered an action that does not exist.
  setDataCode(d, kSyntaxError);
  if (document.parseResult.accepted) {
    // The tree stands and no node could be made of it: the repairs left a
    // shape the grammar's classes do not describe.
    d.range = TextRange{0, static_cast<TextOffset>(text.size())};
    d.message = words.buildNoTreeMessage(text);
  } else if (cst.hasEntry() && cst.entryRoot() != kNoNode) {
    d.range = TextRange{cst[cst.entryRoot()].end,
                        static_cast<TextOffset>(text.size())};
    d.message = words.buildNotAllInputParsedMessage(text, d.range.begin);
  } else {
    d.range = TextRange{0, static_cast<TextOffset>(text.size())};
    d.message = words.buildUnparsedMessage(text);
  }
  return d;
}

// Every message names the reference text: a linking error that says
// "unresolved reference" and nothing else makes the reader hunt for which one.
void DefaultDocumentValidator::processLinkingErrors(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const CancellationToken& token) const {
  const DocumentReferences* const references =
      document.parseResult.references.get();
  if (references == nullptr) return;
  const std::string_view source = getSource();
  const AstReflection* const reflection = services.astReflection.get();
  references->visitUnresolved(
      document.parseResult.cst(),
      [&](RefState why, TextOffset begin, TextOffset end,
          const ReferenceInfo& site) {
        // Tested per reference, not only per document: one workspace-sized
        // file is the case a between-documents test never reaches.
        throwIfCancelled(token);
        const std::string_view text = site.referenceText;
        Diagnostic d;
        d.range = TextRange{begin, end};
        d.severity = DiagnosticSeverity::Error;
        d.message =
            services.linker
                ->createLinkingError(site, why == RefState::ErrorCycle
                                               ? LinkingFailure::Cycle
                                           : why == RefState::ErrorThrown
                                               ? LinkingFailure::Threw
                                               : LinkingFailure::NotFound)
                .message;
        d.source = source;
        JsonValue::Object payload;
        payload.try_emplace(std::string(kDataCode), std::string(kLinkingError));
        payload.try_emplace(std::string(kLinkingRefText), std::string(text));
        // The class that declares the member and the member's name: what a
        // code action needs to know which reference broke without parsing the
        // message back out.
        {
          if (const AstTypeTag* const owner =
                  reflection->referenceOwner(site.reference);
              owner != nullptr)
            payload.try_emplace(std::string(kLinkingContainerType),
                                std::string(owner->name));
          if (const std::string_view property =
                  reflection->referenceProperty(site.reference);
              !property.empty())
            payload.try_emplace(std::string(kLinkingProperty),
                                std::string(property));
        }
        d.data = JsonValue(std::move(payload));
        diagnostics.push_back(std::move(d));
      });
}

namespace {
// Report a failed hook through the registry and propagate cancellation.
void run_document_checks(const ValidationRegistry& registry,
                         std::span<const DocumentCheckEntry> entries,
                         std::string_view context, const Document& document,
                         ValidationAcceptor& accept, ValidationCategories categories,
                         const CancellationToken& token) {
  for (const auto& entry : entries) {
    throwIfCancelled(token);
    if (!detail::runs_category(categories, entry.category)) continue;
    try {
      entry.check(document, accept, token);
    } catch (const OperationCancelled&) {
      throw;
    } catch (...) {
      registry.handleException(std::current_exception(), context, accept, nullptr);
    }
  }
}
}  // namespace

void DefaultDocumentValidator::validateAst(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const ValidationOptions& options, const CancellationToken& token) const {
  // Hooks and node checks share the same ordered batches.
  std::vector<std::vector<Diagnostic>> batches;
  const auto collect = [&](Diagnostic&& diagnostic) {
    auto* batch = batches.empty() ? &diagnostics : &batches.back();
    if (batch->size() == batch->capacity() && batch->size() >= kCheckRun) {
      batch = &batches.emplace_back();
      batch->reserve(kCheckRun);
    }
    batch->push_back(std::move(diagnostic));
  };
  ValidationAcceptor accept(collect, document, getSource(), batches);

  // Each default part skips what the registry does not hold; the document
  // hooks take no node and run on a document with no tree too.
  validateAstBefore(document, accept, options, token);
  if (document.parseResult.value != nullptr)
    validateAstNodes(document, accept, options, token);
  validateAstAfter(document, accept, options, token);
  // A refused diagnostic fails the pass, so its categories are not recorded as
  // run.
  accept.throwIfDiagnosticLost();
  if (!batches.empty()) {
    auto total = diagnostics.size();
    for (const auto& run : batches) total += run.size();
    diagnostics.reserve(total);
    for (auto& run : batches)
      std::ranges::move(run, std::back_inserter(diagnostics));
  }
}

std::string_view DefaultDocumentValidator::getSource() const noexcept {
  return services.languageMetaData.languageId;
}

const ValidationRegistry* DefaultDocumentValidator::validationRegistry()
    const noexcept {
  return services.validationRegistry.get();
}

void DefaultDocumentValidator::validateAstBefore(
    const Document& document, ValidationAcceptor& accept,
    const ValidationOptions& options, const CancellationToken& token) const {
  if (const auto* registry = validationRegistry())
    run_document_checks(*registry, registry->checksBefore(),
                        "An error occurred during set-up of the validation",
                        document, accept, options.categories, token);
}

ValidateSingleNodeOptions DefaultDocumentValidator::validateSingleNodeOptions(
    const AstNode&, const ValidationOptions&) const {
  acceptsEveryNode = this;
  return kValidateEachNode;
}

void DefaultDocumentValidator::validateAstNodes(
    const Document& document, ValidationAcceptor& accept,
    const ValidationOptions& options, const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  // No check in a selected category is no walk: a language with no check pays
  // no tree traversal.
  if (registry == nullptr || registry->checkCount() == 0 ||
      std::ranges::none_of(registry->categories(), [&](const auto* category) {
        return detail::runs_category(options.categories, category);
      }))
    return;

  const AstNode* root = document.parseResult.value;
  const OwnedNodes nodes = document.parseResult.nodes();

  // The language's word on every node, taken before the first check: the walk
  // completes children before their parent. The default says the same of every
  // node and no override can call it: once it answers, no other node is asked.
  std::unordered_set<const AstNode*> refused;
  if (const AstNode* const first = root != nullptr ? root
                                   : nodes.empty() ? nullptr
                                                   : nodes.front()) {
    acceptsEveryNode = nullptr;
    const ValidateSingleNodeOptions said =
        validateSingleNodeOptions(*first, options);
    if (acceptsEveryNode != this)
      refused =
          refused_nodes(nodes, *first, said, *services.shared().taskScheduler,
                        token, [&](const AstNode& node) {
                          return validateSingleNodeOptions(node, options);
                        });
  }
  const auto checkRun = [&](OwnedNodes run, ValidationAcceptor& target) {
    const auto check = [&](const AstNode* node) {
      throwIfCancelled(token);
      if (refused.empty() || !refused.contains(node))
        registry->runChecks(*node, target, options.categories, token);
    };
    // The root runs first, even when the arena does not own it.
    if (run.data() == nodes.data() && root != nullptr) check(root);
    for (const AstNode* node : run)
      if (node != root) check(node);
  };
  const auto& scheduler = shared.taskScheduler;
  const auto workers =
      nodes.size() < 2 * kCheckRun ? 0 : scheduler->workerCount();
  if (workers == 0) {
    checkRun(nodes, accept);
    throwIfCancelled(token);
    return;
  }
  // Each worker fills a private vector and publishes it once in run order.
  const auto runSize =
      nodes.size() / kCheckRun < 4 * workers ? kCheckRun / 4 : kCheckRun;
  std::vector<std::vector<Diagnostic>> found(nodes.size() / runSize);
  scheduler->parallelFor(token, nodes, runSize, [&](OwnedNodes run) {
    std::vector<Diagnostic> kept;
    const auto keep = [&kept](Diagnostic&& diagnostic) {
      kept.push_back(std::move(diagnostic));
    };
    const auto source = getSource();
    ValidationAcceptor local(keep, document,
                             source.empty() ? accept.source() : source);
    checkRun(run, local);
    local.throwIfDiagnosticLost();
    found[static_cast<std::size_t>(run.data() - nodes.data()) / runSize] =
        std::move(kept);
  });
  accept.acceptBatches(std::move(found));
}

void DefaultDocumentValidator::validateAstAfter(
    const Document& document, ValidationAcceptor& accept,
    const ValidationOptions& options, const CancellationToken& token) const {
  if (const auto* registry = validationRegistry())
    run_document_checks(*registry, registry->checksAfter(),
                        "An error occurred during tear-down of the validation",
                        document, accept, options.categories, token);
}

}  // namespace cursiv
