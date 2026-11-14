#include "cursiv/validation/DefaultDocumentValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
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

// The fewest nodes a worker checks at once: enough to outweigh the cost of
// handing them out, few enough that a medium document spreads over the pool.
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
      throw_if_cancelled(token);
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
      throw_if_cancelled(token);
      path.push_back(up);
    }
    for (const AstNode* up : path) ancestry.emplace(up, under);
    if (under) refused.insert(node);
  }
  return refused;
}

// Naming no category runs everything; naming some runs the built-in half only
// when `built_in` is among them, and the checks only when something else is.
[[nodiscard]] bool runs_checks(ValidationCategories categories) noexcept {
  return categories.empty() ||
         std::ranges::any_of(categories, [](const ValidationCategory* c) {
           return c != &built_in;
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
  throw_if_cancelled(token);
  const std::span<const ValidationCategory* const> categories =
      options.categories;
  // Both halves are filtered, so a client asking only for fast checks does not
  // pay the walk over `document.references()` on every keystroke.
  const bool builtIn = detail::runs_category(categories, &built_in);
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
  // Sorted into document order on the way out, stably: parse, then linking,
  // then checks at one position. The (offset, index) pairs are sorted rather
  // than the large diagnostics, and a list already in order is kept as is.
  if (diagnostics.size() > 1 &&
      !std::ranges::is_sorted(
          diagnostics, {}, [](const Diagnostic& d) { return d.range.begin; })) {
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
  throw_if_cancelled(token);
}

// What the recovery inserted or replaced, as edit rows the code-action provider
// turns into quick fixes.
void DefaultDocumentValidator::attachRepairActions(
    const Document& document, TextRange range,
    const ParseDiagnostic& parseDiagnostic, Diagnostic& diagnostic) const {
  // The repair names its tokens, and the grammar spells them. A deletion names
  // the token it resumed on, already in the text, so it gets no fix.
  if (parseDiagnostic.element == kNoElement ||
      parseDiagnostic.kind == DiagnosticKind::Deleted)
    return;

  constexpr std::size_t kMaxActions = 4;
  const std::vector<std::string_view> spellings = expected_spellings(
      services.parser->getGrammar(), parseDiagnostic, kMaxActions);
  if (spellings.empty()) return;

  const std::string_view text = document.textDocument().getText();
  const bool insert = range.begin == range.end;
  const std::string_view replaced =
      insert || range.end > text.size()
          ? std::string_view{}
          : text.substr(range.begin, range.end - range.begin);

  JsonValue::Array rows;
  for (const std::string_view spelling : spellings) {
    if (spelling == replaced) continue;
    rows.push_back(detail::make_action_row(
        insert ? detail::kInsertEdit : detail::kReplaceEdit,
        std::format("{} '{}'", insert ? "Insert" : "Replace with", spelling),
        range, std::string(spelling), document.buildRevision));
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
  throw_if_cancelled(token);
  const std::string_view source = getSource();
  const std::string_view text = document.textDocument().getText();
  const Grammar& grammar = services.parser->getGrammar();
  const ParserErrorMessageProvider& words =
      *services.parserErrorMessageProvider;
  diagnostics.reserve(diagnostics.size() + parsed.diagnostics.size());
  for (const ParseDiagnostic& d : parsed.diagnostics) {
    throw_if_cancelled(token);
    // A diagnostic is its kind: a conversion says so, and everything else is
    // a repair the parser made.
    const std::string_view code = d.kind == DiagnosticKind::ConversionError
                                      ? kConversionError
                                      : kParseRepaired;
    Diagnostic diagnostic;
    diagnostic.range = TextRange{d.begin, d.end};
    // `bounded` is no fault of the author: it says the reading or the repairs
    // around it may not be the best.
    diagnostic.severity = d.kind == DiagnosticKind::Bounded
                              ? DiagnosticSeverity::Information
                              : DiagnosticSeverity::Error;
    diagnostic.message = words.buildMessage(grammar, d, text);
    diagnostic.source = source;
    // A quick fix is built from the element the repair expected.
    if (d.kind != DiagnosticKind::ConversionError)
      attachRepairActions(document, diagnostic.range, d, diagnostic);
    set_data_code(diagnostic, code);
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
  set_data_code(d, kSyntaxError);
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
        throw_if_cancelled(token);
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

// A document hook that throws fails alone, not the whole pass, as a per-node
// check does: the registry's `handleException` reports it, with no node.
// Cancellation passes through.
namespace {
void run_document_check(const ValidationRegistry& registry,
                        const DocumentCheckEntry& entry,
                        std::string_view context, const Document& document,
                        ValidationAcceptor& accept,
                        ValidationCategories categories,
                        const CancellationToken& token) {
  if (!detail::runs_category(categories, entry.category)) return;
  try {
    entry.check(document, accept, token);
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    registry.handleException(std::current_exception(), context, accept,
                             nullptr);
  }
}
}  // namespace

void DefaultDocumentValidator::validateAst(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const ValidationOptions& options, const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  // Nothing registered is nothing to walk: a language with no check pays no
  // tree traversal, and one with only document hooks pays the hooks alone --
  // which take no node, so a document with no tree still runs them.
  if (registry == nullptr) return;
  if (registry->checkCount() == 0 && registry->checksBefore().empty() &&
      registry->checksAfter().empty())
    return;
  if (!options.categories.empty() &&
      std::ranges::none_of(registry->categories(), [&](const auto* category) {
        return detail::runs_category(options.categories, category);
      }))
    return;

  // The collector lives on this frame, which is what makes the acceptor's
  // `function_ref` legal for the whole walk.
  const auto collect = [&diagnostics](Diagnostic&& diagnostic) {
    diagnostics.push_back(std::move(diagnostic));
  };
  ValidationAcceptor accept(ValidationAcceptor::Sink(collect), document,
                            getSource());

  validateAstBefore(document, accept, options, token);
  if (document.parseResult.value != nullptr)
    validateAstNodes(document, accept, options, token);
  validateAstAfter(document, accept, options, token);
  // A refused diagnostic fails the pass, so its categories are not recorded as
  // run.
  accept.throwIfDiagnosticLost();
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
  const ValidationRegistry* registry = validationRegistry();
  if (registry == nullptr) return;
  for (const DocumentCheckEntry& entry : registry->checksBefore()) {
    throw_if_cancelled(token);
    run_document_check(*registry, entry,
                       "An error occurred during set-up of the validation",
                       document, accept, options.categories, token);
  }
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
  if (registry == nullptr || registry->checkCount() == 0) return;

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
  // The root first, the one node whose position is a decision: a check about
  // the whole document belongs at the head of the block rather than after every
  // per-node finding.
  if (root != nullptr) {
    throw_if_cancelled(token);
    if (!refused.contains(root))
      registry->runChecks(*root, accept, options.categories, token);
  }

  // Then the rest in `nodes` order, children first; `validateDocument` sorts
  // the findings into document order. The same loop writes directly, or into
  // each worker's collector before the findings are joined.
  const auto checkKept = [&](OwnedNodes run, ValidationAcceptor& target,
                             auto kept) {
    for (const AstNode* const node : run) {
      throw_if_cancelled(token);
      if (node != root && kept(node))
        registry->runChecks(*node, target, options.categories, token);
    }
  };
  // Nothing refused is the usual case: its loop looks no node up.
  const auto checkRun = [&](OwnedNodes run, ValidationAcceptor& target) {
    if (refused.empty())
      checkKept(run, target, [](const AstNode*) { return true; });
    else
      checkKept(run, target,
                [&](const AstNode* node) { return !refused.contains(node); });
  };
  if (nodes.size() < 2 * kCheckRun) {
    checkRun(nodes, accept);
    return;
  }
  if (services.shared().taskScheduler->workerCount() == 0) {
    checkRun(nodes, accept);
    throw_if_cancelled(token);
    return;
  }
  // A larger one is checked in runs spread over the pool. Each run keeps its
  // findings in the slot its place gives it, handed on in run order, so the
  // list is the one a single walk makes.
  std::vector<std::vector<Diagnostic>> found(nodes.size() / kCheckRun);
  services.shared().taskScheduler->parallelFor(
      token, nodes, kCheckRun, [&](OwnedNodes run) {
        std::vector<Diagnostic>& kept =
            found[static_cast<std::size_t>(run.data() - nodes.data()) /
                  kCheckRun];
        const auto keep = [&kept](Diagnostic&& diagnostic) {
          kept.push_back(std::move(diagnostic));
        };
        ValidationAcceptor local(ValidationAcceptor::Sink(keep), document,
                                 getSource());
        checkRun(run, local);
        local.throwIfDiagnosticLost();
      });
  for (std::vector<Diagnostic>& run : found)
    for (Diagnostic& diagnostic : run) accept.accept(std::move(diagnostic));
}

void DefaultDocumentValidator::validateAstAfter(
    const Document& document, ValidationAcceptor& accept,
    const ValidationOptions& options, const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  if (registry == nullptr) return;
  for (const DocumentCheckEntry& entry : registry->checksAfter()) {
    throw_if_cancelled(token);
    run_document_check(*registry, entry,
                       "An error occurred during tear-down of the validation",
                       document, accept, options.categories, token);
  }
}

}  // namespace cursiv
