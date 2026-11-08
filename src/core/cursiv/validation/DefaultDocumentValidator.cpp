#include "cursiv/validation/DefaultDocumentValidator.hpp"

#include <algorithm>
#include <cassert>
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
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/services/JsonValue.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/DefaultCodeActionProtocol.hpp"
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

// Naming no category runs everything; naming some runs the built-in half only
// when `built_in` is among them, and the checks only when something else is.
[[nodiscard]] bool runs_built_in(ValidationCategories categories) noexcept {
  return categories.empty() ||
         std::ranges::find(categories, &built_in) != categories.end();
}
[[nodiscard]] bool runs_checks(ValidationCategories categories) noexcept {
  return categories.empty() ||
         std::ranges::any_of(categories, [](const ValidationCategory* c) {
           return c != &built_in;
         });
}
[[nodiscard]] bool runs_category(ValidationCategories categories,
                                 const ValidationCategory* category) noexcept {
  return categories.empty() ||
         std::ranges::find(categories, category) != categories.end();
}
// A parse that said only how far its repairs can be trusted has no fault.
[[nodiscard]] bool said_a_fault(const ParseResult& parsed) noexcept {
  return std::ranges::any_of(parsed.diagnostics, [](const ParseDiagnostic& d) {
    return d.kind != DiagnosticKind::bounded;
  });
}

// Whether `processParsingErrors` says this parse went wrong: the verdicts, or a
// fault the parser wrote.
[[nodiscard]] bool said_a_parse_error(const ParseResult& parsed) noexcept {
  return !parsed.accepted || parsed.error != AstBuildError::none ||
         said_a_fault(parsed);
}

}  // namespace

void DefaultDocumentValidator::validateDocument(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const ValidationOptions& options, const CancellationToken& token) const {
  throw_if_cancelled(token);
  const std::span<const ValidationCategory* const> categories =
      options.categories;
  // Both halves gated: ungated, a client asking for the fast checks alone would
  // repay the full walk of `document.references()` on every keystroke.
  const bool builtIn = runs_built_in(categories);
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
      validateAst(document, diagnostics, categories, token);
  }
  // Document order on the way out, sorting the vector of diagnostics and never
  // the walk. Stable, so the documented order survives within one position:
  // parse first, linking second, checks last.
  //
  // The permutation is sorted, not the diagnostics. A `Diagnostic` is 272
  // bytes: a merge sort moves each one a logarithmic number of times, through
  // a buffer of its own, and every comparison reaches into an array of that
  // stride. Offset and position pack into a pair that stays in cache, the
  // position makes the order total -- so plain `sort` is already stable here --
  // and the answer is applied in one pass of n moves.
  //
  // A list that already arrives in order is kept as it is: checks that report
  // on sibling nodes often emit in document order, and then the permutation,
  // the second vector and its n moves buy nothing. Equal positions keep their
  // emission order either way, so the result is the same.
  if (diagnostics.size() > 1 &&
      !std::ranges::is_sorted(
          diagnostics, {}, [](const Diagnostic& d) { return d.range.begin; })) {
    std::vector<std::pair<TextOffset, std::uint32_t>> order;
    order.reserve(diagnostics.size());
    for (std::uint32_t i = 0; i < diagnostics.size(); ++i)
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
    const Document& document, TextRange range, const ParseDiagnostic& owed,
    Diagnostic& diagnostic) const {
  // The repair already says which tokens it was owed, and what they spell is
  // the grammar's own answer -- never a second walk over the document. A
  // deletion names the token it resumed on, already in the text: writing it
  // again is no fix.
  if (owed.element == kNoElement || owed.kind == DiagnosticKind::deleted)
    return;

  constexpr std::size_t kMaxActions = 4;
  const std::vector<std::string_view> spellings =
      expected_spellings(_core.parser->getGrammar(), owed, kMaxActions);
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

// The code is read off the tree, the parse diagnostic carrying no kind: no leaf
// is the parser stopping, a leaf marked `recovered` is a repair, any other leaf
// is a value a converter refused. The range comes from the diagnostic rather
// than a lookup, so a node-less sentence still says where it stopped.
void DefaultDocumentValidator::processParsingErrors(
    const Document& document, std::vector<Diagnostic>& diagnostics,
    const CancellationToken& token) const {
  const ParseResult& parsed = document.parseResult;
  // A refusal that says nothing reads as a clean document. The list below is
  // what the parser repaired, and a failure it could not repair leaves it
  // empty -- so the verdict is the only witness left, and it has to speak.
  // Both verdicts: the parse's, and the build's, which had no reader at all.
  if (said_a_parse_error(parsed) && !said_a_fault(parsed))
    diagnostics.push_back(unrepairedParseError(document));
  // Not gated on the tree. What follows is what the parser wrote, and a repair
  // needs no node to be said: gating it on the AST lost every repair of a
  // document too damaged to build one -- the case where a reader needs them
  // most. Only the message and the grammar are read below.
  if (parsed.diagnostics.empty()) return;
  throw_if_cancelled(token);
  const std::string_view source = getSource();
  const std::string_view text = document.textDocument().getText();
  const Grammar& grammar = _core.parser->getGrammar();
  const ParserErrorMessageProvider& words = *_core.parserErrorMessageProvider;
  diagnostics.reserve(diagnostics.size() + parsed.diagnostics.size());
  for (const ParseDiagnostic& d : parsed.diagnostics) {
    throw_if_cancelled(token);
    // A diagnostic is its kind: a conversion says so, and everything else is
    // a repair the parser made.
    const std::string_view code = d.kind == DiagnosticKind::conversion_error
                                      ? kConversionError
                                      : kParseRepaired;
    Diagnostic diagnostic;
    diagnostic.range = TextRange{d.begin, d.end};
    // A bound names no fault of the author's: it says how far the repairs
    // around it can be trusted.
    diagnostic.severity = d.kind == DiagnosticKind::bounded
                              ? DiagnosticSeverity::Information
                              : DiagnosticSeverity::Error;
    diagnostic.message = words.buildMessage(grammar, d, text);
    diagnostic.source = source;
    // The element the repair was owed is what a quick fix is built from.
    if (d.kind != DiagnosticKind::conversion_error)
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
  const ParserErrorMessageProvider& words = *_core.parserErrorMessageProvider;
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.source = getSource();
  // The syntax code, never the repair one: nothing was repaired here, and no
  // quick fix rides this diagnostic -- a client filtering on `parse-repaired`
  // must not be offered an action that does not exist.
  set_data_code(d, kSyntaxError);
  if (document.parseResult.accepted) {
    // The tree stands and no node could be made of it: the repairs left a
    // shape the grammar's classes do not describe.
    d.range = TextRange{0, static_cast<TextOffset>(text.size())};
    d.message = words.buildNoTreeMessage(text);
  } else if (cst.has_entry() && cst.entry_root() != kNoNode) {
    d.range = TextRange{cst[cst.entry_root()].end,
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
  const AstReflection* const reflection = _core.astReflection.get();
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
        d.message = _core.linker->createLinkingError(site, why).message;
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

// A document hook that throws costs itself, not the pass -- the same law as a
// per-node check, and the same door: the registry's `handleException`, with no
// node, since a document check has none. Cancellation goes straight through.
namespace {
void run_document_check(const ValidationRegistry& registry,
                        const ValidationPreparationEntry& entry,
                        std::string_view context, const Document& document,
                        ValidationAcceptor& accept,
                        ValidationCategories categories,
                        const CancellationToken& token) {
  if (!runs_category(categories, entry.category)) return;
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
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  // Nothing registered is nothing to walk: a language with no check pays no
  // tree traversal, and one with only document hooks pays the hooks alone --
  // which take no node, so a document with no tree still runs them.
  if (registry == nullptr) return;
  if (registry->checkCount() == 0 && registry->checksBefore().empty() &&
      registry->checksAfter().empty())
    return;
  if (!categories.empty() &&
      std::ranges::none_of(registry->categories(), [&](const auto* category) {
        return runs_category(categories, category);
      }))
    return;

  // The collector lives on this frame, which is what makes the acceptor's
  // `function_ref` legal for the whole walk.
  const auto collect = [&diagnostics](Diagnostic&& diagnostic) {
    diagnostics.push_back(std::move(diagnostic));
  };
  ValidationAcceptor accept(ValidationAcceptor::Sink(collect), document,
                            getSource());

  validateAstBefore(document, accept, categories, token);
  if (document.parseResult.value != nullptr)
    validateAstNodes(document, accept, categories, token);
  validateAstAfter(document, accept, categories, token);
}

std::string_view DefaultDocumentValidator::getSource() const noexcept {
  return _core.languageMetaData.languageId;
}

const ValidationRegistry* DefaultDocumentValidator::validationRegistry()
    const noexcept {
  return _core.validationRegistry.get();
}

void DefaultDocumentValidator::validateAstBefore(
    const Document& document, ValidationAcceptor& accept,
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  if (registry == nullptr) return;
  for (const ValidationPreparationEntry& entry : registry->checksBefore()) {
    throw_if_cancelled(token);
    run_document_check(*registry, entry,
                       "An error occurred during set-up of the validation",
                       document, accept, categories, token);
  }
}

ValidateSingleNodeOptions DefaultDocumentValidator::validateSingleNodeOptions(
    const AstNode&, std::span<const ValidationCategory* const>) const {
  return kValidateEachNode;
}

void DefaultDocumentValidator::validateAstNodes(
    const Document& document, ValidationAcceptor& accept,
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  if (registry == nullptr || registry->checkCount() == 0) return;

  // The root first, the one node whose position is a decision: a check about
  // the whole document belongs at the head of the block rather than after every
  // per-node finding.
  const AstNode* root = document.parseResult.value;

  // The language's word on every node, taken first when it may refuse one:
  // the walk below completes children before their parent, so a refusal read
  // at the parent would arrive after the children it excludes.
  const bool asks = asksNodeOptions();
  std::unordered_set<const AstNode*> unchecked;
  std::unordered_set<const AstNode*> pruned;
  auto ask = [&](const AstNode& node) {
    throw_if_cancelled(token);
    const ValidateSingleNodeOptions options =
        validateSingleNodeOptions(node, categories);
    if (!options.validateNode) unchecked.insert(&node);
    if (!options.validateChildren) pruned.insert(&node);
  };
  if (asks) {
    if (root != nullptr) ask(*root);
    for (const AstNode* const node : document.parseResult.nodes())
      if (node != root) ask(*node);
  }

  // Shared ancestry is memoised, so a deep or widely pruned tree stays linear.
  std::unordered_map<const AstNode*, bool> ancestry;
  std::vector<const AstNode*> path;
  const auto excluded = [&](const AstNode& node) {
    if (unchecked.contains(&node)) return true;
    if (pruned.empty()) return false;
    path.clear();
    bool refused = false;
    for (const AstNode* up = node.parent; up != nullptr; up = up->parent) {
      if (pruned.contains(up)) {
        refused = true;
        break;
      }
      if (const auto known = ancestry.find(up); known != ancestry.end()) {
        refused = known->second;
        break;
      }
      throw_if_cancelled(token);
      path.push_back(up);
    }
    for (const AstNode* up : path) ancestry.emplace(up, refused);
    return refused;
  };
  const auto check = [&](const AstNode& node) {
    throw_if_cancelled(token);
    assert((asks || [&] {
             const auto options = validateSingleNodeOptions(node, categories);
             return options.validateNode && options.validateChildren;
           }()) &&
           "an override of validateSingleNodeOptions answers asksNodeOptions");
    if (asks && excluded(node)) return;
    registry->runChecks(node, accept, categories, token);
  };

  if (root != nullptr) check(*root);

  // Then the rest in `nodes` order, which is the order the tree was built:
  // the parser numbers a CST node when it completes, so children precede their
  // parent. That is a total, deterministic order, and it is not document order.
  //
  // The walk stays that way on purpose. Document order is owed on the published
  // list, not on the traversal, and the list is a handful of diagnostics rather
  // than a tree of nodes -- `validateDocument` sorts it on the way out.
  //
  // Node options share the ancestry memo. Small trees also use this walk.
  const OwnedNodes nodes = document.parseResult.nodes();
  if (asks || nodes.size() < 2 * kCheckRun) {
    for (const AstNode* const node : nodes)
      if (node != root) check(*node);
    return;
  }
  // Larger runs need no node options. The same loop writes directly without
  // workers, or into each worker's collector before the findings are joined.
  const auto checkRun = [&](OwnedNodes run, ValidationAcceptor& target) {
    for (const AstNode* const node : run) {
      throw_if_cancelled(token);
      if (node != root)
        registry->runChecks(*node, target, categories, token);
    }
  };
  if (_core.shared().taskScheduler->workerCount() == 0) {
    checkRun(nodes, accept);
    throw_if_cancelled(token);
    return;
  }
  // A larger one is checked in runs spread over the pool. Each run keeps its
  // findings in the slot its place gives it, handed on in run order, so the
  // list is the one a single walk makes.
  std::vector<std::vector<Diagnostic>> found(nodes.size() / kCheckRun);
  _core.shared().taskScheduler->parallelFor(
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
      });
  for (std::vector<Diagnostic>& run : found)
    for (Diagnostic& diagnostic : run) accept.accept(std::move(diagnostic));
}

void DefaultDocumentValidator::validateAstAfter(
    const Document& document, ValidationAcceptor& accept,
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) const {
  const ValidationRegistry* registry = validationRegistry();
  if (registry == nullptr) return;
  for (const ValidationPreparationEntry& entry : registry->checksAfter()) {
    throw_if_cancelled(token);
    run_document_check(*registry, entry,
                       "An error occurred during tear-down of the validation",
                       document, accept, categories, token);
  }
}

}  // namespace cursiv
