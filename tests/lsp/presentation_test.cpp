#include <lsp/messages.h>
#include <lsp/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/references/DefaultNameProvider.hpp"
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/services/JsonValue.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/MultiRefItems.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/actions/DefaultCodeActionProvider.hpp"
#include "cursiv/actions/JsonBridge.hpp"
#include "cursiv/completion/FuzzyMatcher.hpp"
#include "cursiv/content/DefaultTextDocumentContentProvider.hpp"
#include "cursiv/content/TextDocumentContentProvider.hpp"
#include "cursiv/ranges/DefaultFoldingRangeProvider.hpp"
#include "cursiv/ranges/FoldingRangeProvider.hpp"
#include "cursiv/ranges/SelectionRangeProvider.hpp"
#include "cursiv/runtime/LanguageClient.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/semantic/AbstractSemanticTokenProvider.hpp"
#include "cursiv/semantic/GrammarSemanticTokenProvider.hpp"
#include "cursiv/semantic/SemanticTokenProvider.hpp"
#include "cursiv/semantic/SignatureHelpProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/Check.hpp"
#include "support/Throws.hpp"
#include "cursiv/DecodedSemanticTokens.hpp"
#include "lsp/uri.h"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/core/ast.hpp"

namespace {

// A new URI for each document, so tests never share one.
struct UniqueUriWorld : support::LspWorkspace {
  mutable unsigned nextUri = 0;

  cursiv::Document* built(std::string text) const {
    return WorkspaceWorld::built(
        "file:///w/a" + std::to_string(nextUri++) + ".ws", std::move(text));
  }
};

// Counts the walks over all of a document's references, resolved or failed;
// forwards the rest.
struct CountingReferences final : cursiv::DocumentReferences {
  explicit CountingReferences(std::unique_ptr<cursiv::DocumentReferences> in)
      : inner(std::move(in)) {}
  std::unique_ptr<cursiv::DocumentReferences> inner;
  mutable int walks = 0;

  Answer resolve(std::uint32_t rank) const override {
    return inner->resolve(rank);
  }
  cursiv::RefState resolveAll(std::uint32_t rank,
                              cursiv::MultiRefItems& out) const override {
    return inner->resolveAll(rank, out);
  }
  void force(std::span<const cursiv::RefHandle> which,
             const cursiv::CancellationToken& token) override {
    inner->force(which, token);
  }
  void forceAt(const cursiv::Cst& cst, cursiv::TextOffset offset,
               const cursiv::CancellationToken& token) override {
    inner->forceAt(cst, offset, token);
  }
  void visitResolved(const cursiv::Cst& cst,
                     cursiv::ResolvedReferenceVisitor visitor) const override {
    ++walks;
    inner->visitResolved(cst, visitor);
  }
  void reset() noexcept override { inner->reset(); }
  [[nodiscard]] bool anyUnresolved() const noexcept override {
    return inner->anyUnresolved();
  }
  void visitUnresolved(
      const cursiv::Cst& cst,
      cursiv::UnresolvedReferenceVisitor visitor) const override {
    ++walks;
    inner->visitUnresolved(cst, visitor);
  }
  void visitIn(const cursiv::Cst& cst, cursiv::TextOffset begin,
               cursiv::TextOffset end,
               cursiv::ResolvedReferenceVisitor resolved,
               cursiv::UnresolvedReferenceVisitor unresolved) const override {
    inner->visitIn(cst, begin, end, resolved, unresolved);
  }
};

}  // namespace

// One fold per AST node below the root that spans three lines or more, its
// closing line left visible, and one per comment of that size; a language
// changes this through hooks.
namespace folding_range_provider {

namespace {

struct World : UniqueUriWorld {
  [[nodiscard]] std::vector<lsp::FoldingRange> folds(
      std::string text,
      const cursiv::FoldingRangeProvider* provider = nullptr) const {
    const cursiv::Document* const document = built(std::move(text));
    if (provider == nullptr) provider = ws->foldingRangeProvider.get();
    return provider->getFoldingRanges(*document, cursiv::kNoCancel);
  }
};

constexpr std::string_view kModel = support::kPackage;

[[nodiscard]] const lsp::FoldingRange* starting(
    const std::vector<lsp::FoldingRange>& ranges, lsp::Uint line) {
  const auto at = std::ranges::find_if(
      ranges,
      [line](const lsp::FoldingRange& r) { return r.startLine == line; });
  return at == ranges.end() ? nullptr : &*at;
}

[[nodiscard]] bool is_comment(const lsp::FoldingRange& range) {
  return range.kind.has_value() &&
         *range.kind == lsp::FoldingRangeKind::Comment;
}

// A fold is published in both units: the lines a chevron is drawn on, and the
// columns a client that folds mid-line reads.
void a_fold_carries_its_lines_and_its_columns() {
  const World world;
  const std::vector<lsp::FoldingRange> ranges =
      world.folds(std::string(kModel));
  CHECK(ranges.size() == 3);

  // The entity's block: lines 5 to 6, the closing `}` on line 7 left visible.
  const lsp::FoldingRange* const entity = starting(ranges, 5);
  CHECK(entity != nullptr);
  if (entity != nullptr) {
    CHECK(entity->endLine == 6);
    CHECK(!entity->kind.has_value());
    CHECK(entity->startCharacter == 2);
    CHECK(entity->endCharacter == 17);
  }
  // The package, over everything but its own closer.
  const lsp::FoldingRange* const package = starting(ranges, 0);
  CHECK(package != nullptr && package->endLine == 7);

  // The block comment, folded as a comment so "fold all comments" reaches it,
  // and its last line kept visible.
  const auto comment = std::ranges::find_if(ranges, is_comment);
  CHECK(comment != ranges.end());
  if (comment != ranges.end()) {
    CHECK(comment->startLine == 1 && comment->endLine == 2);
    CHECK(comment->startCharacter == 2);
    CHECK(comment->endCharacter.has_value());
  }

  for (const lsp::FoldingRange& range : ranges) {
    CHECK(range.startCharacter.has_value());
    CHECK(range.endCharacter.has_value());
  }
}

void a_record_under_three_lines_and_the_root_do_not_fold() {
  const World world;
  // The root covers lines 0-3; the entity is the only fold.
  const std::vector<lsp::FoldingRange> two =
      world.folds("datatype A\nentity E {\n  f : A\n}\n");
  CHECK(two.size() == 1);
  CHECK(!two.empty() && two.front().startLine == 1 && two.front().endLine == 2);

  // A two-line block, and a two-line feature ending in a name.
  CHECK(world.folds("entity E {\n}\n").empty());
  const std::vector<lsp::FoldingRange> feature =
      world.folds("entity E {\n  f :\n  A\n}\n");
  CHECK(feature.size() == 1);
  CHECK(!feature.empty() && feature.front().startLine == 0 &&
        feature.front().endLine == 2);
}

// The line index numbers a lone `\r` as a line break, and so does the fold.
void a_document_with_lone_cr_line_breaks_folds() {
  const World world;
  const std::vector<lsp::FoldingRange> ranges =
      world.folds("entity Person {\rname : String\rage : Int\r}\r");
  CHECK(ranges.size() == 1);
  CHECK(!ranges.empty() && ranges.front().startLine == 0 &&
        ranges.front().endLine == 2);
}

// Each line comment is a node of its own: three stacked line comments are
// three one-line nodes, and none folds.
void a_stack_of_line_comments_is_not_a_fold() {
  const World world;
  const std::vector<lsp::FoldingRange> ranges =
      world.folds("// one\n// two\n// three\nentity E {}\n");
  CHECK(std::ranges::none_of(ranges, is_comment));
  // A three-line block comment folds, even before the first declaration.
  const std::vector<lsp::FoldingRange> block =
      world.folds("/* one\n   two\n   three */\nentity E {}\n");
  CHECK(block.size() == 1 && is_comment(block.front()));
}

// A language says which nodes fold, and whose content is entered at all.
struct NoEntities final : cursiv::DefaultFoldingRangeProvider {
  using DefaultFoldingRangeProvider::DefaultFoldingRangeProvider;
  bool shouldProcess(const cursiv::AstNode& node) const override {
    return node.type != &cursiv::ast_type_tag<ws::ast::Entity>;
  }
};
struct ClosedPackages final : cursiv::DefaultFoldingRangeProvider {
  using DefaultFoldingRangeProvider::DefaultFoldingRangeProvider;
  bool shouldProcessContent(const cursiv::AstNode& node) const override {
    return node.type != &cursiv::ast_type_tag<ws::ast::PackageDeclaration>;
  }
};

void a_language_chooses_which_nodes_fold() {
  const World world;
  const NoEntities noEntities(*world.ws);
  const std::vector<lsp::FoldingRange> without =
      world.folds(std::string(kModel), &noEntities);
  CHECK(starting(without, 5) == nullptr);
  CHECK(starting(without, 0) != nullptr);

  // Skipping the package's content skips the entity inside it; the package
  // still folds, and the comment is not content.
  const ClosedPackages closed(*world.ws);
  const std::vector<lsp::FoldingRange> shallow =
      world.folds(std::string(kModel), &closed);
  CHECK(starting(shallow, 5) == nullptr);
  CHECK(starting(shallow, 0) != nullptr);
  CHECK(std::ranges::any_of(shallow, is_comment));
}

// The last-line hook sees the node and the fold kind, so a language may keep
// the closing line inside its fold.
struct KeepsTheCloser final : cursiv::DefaultFoldingRangeProvider {
  using DefaultFoldingRangeProvider::DefaultFoldingRangeProvider;
  bool includeLastFoldingLine(
      const cursiv::CstNodeView& node,
      std::optional<lsp::FoldingRangeKindEnum> kind) const override {
    return !kind.has_value() && node.text().ends_with('}');
  }
};

void a_language_may_keep_the_closer_in_its_fold() {
  const World world;
  const KeepsTheCloser keeps(*world.ws);
  const std::vector<lsp::FoldingRange> ranges =
      world.folds(std::string(kModel), &keeps);
  const lsp::FoldingRange* const entity = starting(ranges, 5);
  CHECK(entity != nullptr && entity->endLine == 7);
  const auto comment = std::ranges::find_if(ranges, is_comment);
  CHECK(comment != ranges.end() && comment->endLine == 2);
}

// The cancellation token is checked at every node, not only at folds.
struct CancelsAsItWalks final : cursiv::DefaultFoldingRangeProvider {
  CancelsAsItWalks(const cursiv::LspServices& services,
                   cursiv::CancellationTokenSource& source)
      : DefaultFoldingRangeProvider(services), source(&source) {}
  cursiv::CancellationTokenSource* source;
  bool shouldProcess(const cursiv::AstNode&) const override {
    source->request_stop();
    return false;
  }
};

void a_cancelled_walk_stops_without_a_fold() {
  const World world;
  cursiv::CancellationTokenSource source;
  const CancelsAsItWalks cancels(*world.ws, source);
  const cursiv::Document* const document =
      world.built("datatype A\ndatatype B\n");
  bool cancelled = false;
  try {
    (void)cancels.getFoldingRanges(*document, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
}

}  // namespace

void run() {
  a_fold_carries_its_lines_and_its_columns();
  a_record_under_three_lines_and_the_root_do_not_fold();
  a_document_with_lone_cr_line_breaks_folds();
  a_stack_of_line_comments_is_not_a_fold();
  a_language_chooses_which_nodes_fold();
  a_language_may_keep_the_closer_in_its_fold();
  a_cancelled_walk_stops_without_a_fold();
}

}  // namespace folding_range_provider

namespace symbol_edge_cases {

struct QueryMatcher final : cursiv::FuzzyMatcher {
  mutable std::vector<std::string> queries;

  bool match(std::string_view query, std::string_view text) const override {
    queries.emplace_back(query);
    return query == "person" && text == "Person";
  }
};

struct UnplacedNames final : cursiv::DefaultNameProvider {
  using DefaultNameProvider::DefaultNameProvider;

  std::optional<cursiv::CstNodeView> getNameNode(
      const cursiv::AstNode&, const cursiv::Document*) const override {
    return std::nullopt;
  }
};

void run() {
  folding_range_provider::World world;
  cursiv::Document* const document =
      world.built("entity Person { name : String }\n");
  cursiv::AstNode* entity = nullptr;
  for (cursiv::AstNode* node : document->parseResult.nodes())
    if (cursiv::ast_cast<ws::ast::Entity>(node) != nullptr) entity = node;
  CHECK(entity != nullptr);
  if (entity == nullptr) return;

  const cursiv::NodeId saved = entity->cstNodeId;
  entity->cstNodeId = cursiv::kNoNode;
  const auto outline = world.ws->documentSymbolProvider->getSymbols(*document);
  entity->cstNodeId = saved;
  CHECK(outline.size() == 1 && outline.front().name == "name");

  auto matcher = std::make_unique<QueryMatcher>();
  const QueryMatcher& observed = *matcher;
  world.shared.fuzzyMatcher = std::move(matcher);
  const auto symbols =
      world.shared.workspaceSymbolProvider->getSymbols("PeRsOn");
  CHECK(symbols.size() == 1 && symbols.front().name == "Person");
  CHECK(!observed.queries.empty());
  CHECK(std::ranges::all_of(observed.queries, [](const std::string& query) {
    return query == "person";
  }));

  world.ws->nameProvider = std::make_unique<UnplacedNames>(*world.ws);
  CHECK(world.shared.workspaceSymbolProvider->getSymbols("Person").empty());
}

}  // namespace symbol_edge_cases

namespace selection_range_provider {

namespace {

struct World : UniqueUriWorld {
  [[nodiscard]] std::vector<lsp::SelectionRange> at(std::string text,
                                                    lsp::Position caret) const {
    const cursiv::Document* const document = built(std::move(text));
    return ws->selectionRangeProvider->getSelectionRanges(
        *document, {&caret, 1}, cursiv::kNoCancel);
  }
};

[[nodiscard]] bool spans(const lsp::Range& range, lsp::Uint line,
                         lsp::Uint from, lsp::Uint to) {
  return range.start.line == line && range.start.character == from &&
         range.end.line == line && range.end.character == to;
}

void a_selection_grows_from_the_word_to_the_document() {
  const World world;
  const std::vector<lsp::SelectionRange> answer = world.at(
      "package p {\n"
      "  datatype String\n"
      "  entity Person {\n"
      "    name : String\n"
      "  }\n"
      "}\n",
      {3, 6});  // inside `name`
  CHECK(answer.size() == 1);
  if (answer.empty()) return;

  // The innermost step is the word itself.
  CHECK(spans(answer.front().range, 3, 4, 8));

  // Each parent is strictly wider, and the last covers the whole text.
  std::size_t steps = 1;
  const lsp::SelectionRange* at = &answer.front();
  const lsp::Range* previous = &at->range;
  while (at->parent != nullptr) {
    at = at->parent.get();
    ++steps;
    const bool wider = at->range.start.line < previous->start.line ||
                       (at->range.start.line == previous->start.line &&
                        at->range.start.character <= previous->start.character);
    CHECK(wider);
    previous = &at->range;
  }
  CHECK(steps > 2);
  CHECK(at->range.start.line == 0 && at->range.start.character == 0);
}

// A caret on nothing still gets an entry, clamped inside the document like
// every other range.
void a_caret_nothing_covers_answers_a_clamped_empty_range() {
  const World world;
  // Past the last line of a two-line document.
  const std::vector<lsp::SelectionRange> past =
      world.at("entity E {}\n", {5, 3});
  CHECK(past.size() == 1);
  CHECK(!past.empty() && spans(past.front().range, 1, 0, 0));
  CHECK(!past.empty() && past.front().parent == nullptr);

  // Past the end of a whitespace-only line.
  const std::vector<lsp::SelectionRange> beyond =
      world.at("entity E {}\n   \n", {1, 10});
  CHECK(beyond.size() == 1);
  CHECK(!beyond.empty() && spans(beyond.front().range, 1, 3, 3));
}

void a_document_without_a_tree_answers_every_cursor() {
  const World world;
  const cursiv::Document* const document = world.built("");
  const lsp::Position positions[] = {{0, 0}, {0, 0}};
  const auto answer = world.ws->selectionRangeProvider->getSelectionRanges(
      *document, positions, cursiv::kNoCancel);
  CHECK(answer.size() == 2);
  for (const lsp::SelectionRange& range : answer) {
    CHECK(spans(range.range, 0, 0, 0));
    CHECK(range.parent == nullptr);
  }
}

// A non-ASCII word is one word, and a non-ASCII dash ends one.
void a_word_past_ascii_is_read_by_scalar() {
  const World world;
  // `Über` inside a comment: four characters, four UTF-16 units.
  const std::vector<lsp::SelectionRange> umlaut = world.at(
      "entity E {}\n/* \xC3\x9C"
      "ber x */\n",
      {1, 4});
  CHECK(umlaut.size() == 1);
  CHECK(!umlaut.empty() && spans(umlaut.front().range, 1, 3, 7));

  // `alpha—beta` with an em dash and no space: the word stops at the dash.
  const std::vector<lsp::SelectionRange> dashed = world.at(
      "entity E {}\n/* alpha\xE2\x80\x94"
      "beta */\n",
      {1, 4});
  CHECK(dashed.size() == 1);
  CHECK(!dashed.empty() && spans(dashed.front().range, 1, 3, 8));
}

}  // namespace

void run() {
  a_selection_grows_from_the_word_to_the_document();
  a_caret_nothing_covers_answers_a_clamped_empty_range();
  a_document_without_a_tree_answers_every_cursor();
  a_word_past_ascii_is_read_by_scalar();
}

}  // namespace selection_range_provider

namespace semantic_token_provider {

namespace {

namespace ct = cursiv::test;

// Semantic tokens are off by default: this language turns them on.
struct World : support::LspWorkspace {
  World()
      : WorkspaceWorld([](cursiv::LspServices& language) {
          language.semanticTokenProvider =
              std::make_unique<const cursiv::GrammarSemanticTokenProvider>(
                  language);
        }) {}
};

// One token, absolute: what the client reconstructs from the deltas.
struct Decoded {
  lsp::Uint line = 0;
  lsp::Uint start = 0;
  lsp::Uint length = 0;
  lsp::Uint type = 0;
  lsp::Uint modifiers = 0;
  bool operator==(const Decoded&) const = default;
};

// Neither ending before the range nor starting after it.
[[nodiscard]] bool touches(const Decoded& token, const lsp::Range& range) {
  const bool before = token.line < range.start.line ||
                      (token.line == range.start.line &&
                       token.start + token.length <= range.start.character);
  const bool after =
      token.line > range.end.line ||
      (token.line == range.end.line && token.start >= range.end.character);
  return !before && !after;
}

[[nodiscard]] std::vector<Decoded> decode(const lsp::SemanticTokens& tokens) {
  std::vector<Decoded> out;
  lsp::Uint line = 0;
  lsp::Uint start = 0;
  for (std::size_t at = 0; at + 4 < tokens.data.size(); at += 5) {
    const lsp::Uint deltaLine = tokens.data[at];
    const lsp::Uint deltaStart = tokens.data[at + 1];
    line += deltaLine;
    start = deltaLine == 0 ? start + deltaStart : deltaStart;
    out.push_back({line, start, tokens.data[at + 2], tokens.data[at + 3],
                   tokens.data[at + 4]});
  }
  return out;
}

[[nodiscard]] bool carries(const std::vector<ct::DecodedSemanticToken>& tokens,
                           std::string_view type) {
  for (const ct::DecodedSemanticToken& token : tokens)
    if (token.tokenType == type) return true;
  return false;
}

[[nodiscard]] lsp::SemanticTokensParams whole(
    const cursiv::Document& document) {
  lsp::SemanticTokensParams params;
  params.textDocument.uri = lsp::Uri::parse(document.uri);
  return params;
}

[[nodiscard]] lsp::SemanticTokensRangeParams cut(
    const cursiv::Document& document, const lsp::Range& range) {
  lsp::SemanticTokensRangeParams params;
  params.textDocument.uri = lsp::Uri::parse(document.uri);
  params.range = range;
  return params;
}

// The index of `name` in the protocol's legend, which the grammar provider
// uses.
[[nodiscard]] lsp::Uint row(std::string_view name) {
  return static_cast<lsp::Uint>(
      std::ranges::find(cursiv::kSemanticTokenTypes, name) -
      cursiv::kSemanticTokenTypes.begin());
}

// The checks below number its lines from 0.
constexpr std::string_view kModel =
    "/* a comment */\n"
    "datatype String\n"
    "entity Person {\n"
    "  name : String\n"
    "}\n";

void every_expected_category_is_coloured() {
  const World world;
  // Each marked span is one category: the comment, a declaration owning
  // nothing, one owning a feature, its feature, and a reference.
  const ct::DecodedSemanticTokensWithRanges coloured =
      ct::highlightHelper(*world.ws,
                          "<|/* a comment */|>\n"
                          "datatype <|String|>\n"
                          "entity <|Person|> {\n"
                          "  <|name|> : <|String|>\n"
                          "}\n");
  CHECK(!coloured.tokens.empty());

  const std::string_view expected[] = {"comment", "property", "type",
                                       "property", "variable"};
  std::size_t marked = 0;
  for (const std::string_view type : expected)
    ct::expectSemanticToken(
        coloured, {.rangeIndex = marked++, .tokenType = std::string(type)});

  // `datatype`/`entity` are words the grammar reserved; `{`/`:` are not.
  CHECK(carries(coloured.tokens, "keyword"));
  CHECK(carries(coloured.tokens, "operator"));
}

// The encoding cannot place a token before the previous one, so tokens come
// in source order and never overlap: a byte coloured twice gets either colour.
void the_tokens_are_ordered_and_never_overlap() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const lsp::SemanticTokens raw =
      world.ws->semanticTokenProvider->semanticHighlight(
          *document, whole(*document), cursiv::kNoCancel);
  // Five integers per token, exactly.
  CHECK(raw.data.size() % 5 == 0);

  const std::vector<Decoded> tokens = decode(raw);
  for (std::size_t at = 1; at < tokens.size(); ++at) {
    const Decoded& previous = tokens[at - 1];
    const Decoded& current = tokens[at];
    const bool ordered = current.line > previous.line ||
                         (current.line == previous.line &&
                          current.start >= previous.start + previous.length);
    CHECK(ordered);
  }
  // The grammar's colours claim no modifier.
  for (const Decoded& token : tokens) CHECK(token.modifiers == 0);
}

// Every type index must be inside the legend sent to the client. By default
// the legend is the protocol's, with `full` and `range` but no `delta`.
void every_index_names_a_row_of_the_legend() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const cursiv::SemanticTokenProvider& provider =
      *world.ws->semanticTokenProvider;
  const std::vector<Decoded> tokens = decode(provider.semanticHighlight(
      *document, whole(*document), cursiv::kNoCancel));
  const lsp::SemanticTokensOptions options = provider.semanticTokensOptions();
  CHECK(std::ranges::equal(options.legend.tokenTypes,
                           cursiv::kSemanticTokenTypes));
  CHECK(std::ranges::equal(options.legend.tokenModifiers,
                           cursiv::kSemanticTokenModifiers));
  for (const Decoded& token : tokens)
    CHECK(token.type < options.legend.tokenTypes.size());
  const auto* const full =
      options.full.has_value()
          ? std::get_if<lsp::SemanticTokensFullDelta>(&*options.full)
          : nullptr;
  CHECK(full != nullptr && !full->delta.value_or(false));
  CHECK(options.range.has_value());
  // Without `delta`, no answer has a result id.
  CHECK(!provider.semanticHighlight(*document, whole(*document))
             .resultId.has_value());
}

// A range answers the tokens that touch it, nothing beyond: the full answer
// cut to the range, for whole lines, a cut word, or everything.
void a_range_is_the_full_answer_cut_to_it() {
  const World world;
  cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  auto counting = std::make_unique<CountingReferences>(
      std::move(document->parseResult.references));
  const CountingReferences& walked = *counting;
  document->parseResult.references = std::move(counting);
  const std::vector<Decoded> full =
      decode(world.ws->semanticTokenProvider->semanticHighlight(
          *document, whole(*document), cursiv::kNoCancel));
  const lsp::Range ranges[] = {
      {{2, 0}, {4, 0}},
      {{3, 3}, {3, 5}},
      {{2, 0}, {2, 9}},
      {{0, 0}, {5, 0}},
  };
  for (const lsp::Range& range : ranges) {
    std::vector<Decoded> expected;
    for (const Decoded& token : full)
      if (touches(token, range)) expected.push_back(token);
    CHECK(!expected.empty());
    const int walks = walked.walks;
    const std::vector<Decoded> part =
        decode(world.ws->semanticTokenProvider->semanticHighlightRange(
            *document, cut(*document, range), cursiv::kNoCancel));
    CHECK(part == expected);
    // Short of the whole document, only the references in the range are read.
    if (&range != &ranges[3]) CHECK(walked.walks == walks);
  }
  // The word alone, cut through: coloured whole.
  const std::vector<Decoded> word =
      decode(world.ws->semanticTokenProvider->semanticHighlightRange(
          *document, cut(*document, ranges[1]), cursiv::kNoCancel));
  CHECK(word.size() == 1 && word.front().line == 3 && word.front().start == 2 &&
        word.front().length == 4);
}

// Columns count UTF-16 units even after non-ASCII comments. Newline spelling
// does not affect the answer, and a multiline comment omits its empty line.
void unicode_and_multiline_tokens_keep_their_positions() {
  const auto comment = row("comment");
  const auto keyword = row("keyword");
  const auto property = row("property");
  const auto owner = row("type");
  const auto op = row("operator");
  const auto variable = row("variable");
  const std::vector<Decoded> expected = {
      {0, 0, 10, comment},  {0, 11, 8, keyword}, {0, 20, 6, property},
      {1, 0, 8, comment},   {3, 0, 14, comment}, {3, 15, 6, keyword},
      {3, 22, 6, owner},    {3, 29, 1, op},      {4, 2, 7, comment},
      {4, 10, 4, property}, {4, 15, 1, op},      {4, 17, 6, variable},
      {5, 0, 1, op},
  };
  for (const std::string eol : {"\n", "\r\n", "\r"}) {
    const World world;
    const auto* document =
        world.built("file:///w/unicode.ws",
                    "/* é 😀 */ datatype String" + eol + "/* début" + eol +
                        eol + "  中文 😀 fin */ entity Person {" + eol +
                        "  /* é */ name : String" + eol + "}" + eol);
    const auto full = decode(world.ws->semanticTokenProvider->semanticHighlight(
        *document, whole(*document), cursiv::kNoCancel));
    CHECK(full == expected);
    const lsp::Range ranges[] = {
        {{0, 4}, {0, 6}}, {{1, 1}, {3, 4}}, {{4, 11}, {4, 12}}};
    const std::vector<Decoded> cuts[] = {
        {expected[0]}, {expected[3], expected[4]}, {expected[9]}};
    for (std::size_t i = 0; i < std::size(ranges); ++i)
      CHECK(decode(world.ws->semanticTokenProvider->semanticHighlightRange(
                *document, cut(*document, ranges[i]), cursiv::kNoCancel)) ==
            cuts[i]);
  }
}

// A language's own legend: the grammar's colours keep the types it names, at
// its indices, and drop the rest; a derived provider marks each declaration.
struct OwnLegend final : cursiv::GrammarSemanticTokenProvider {
  using GrammarSemanticTokenProvider::GrammarSemanticTokenProvider;

  static constexpr std::array<std::string_view, 4> kTypes = {
      "keyword", "property", "type", "label"};
  static constexpr std::array<std::string_view, 2> kModifiers = {"readonly",
                                                                 "declaration"};

  [[nodiscard]] std::span<const std::string_view> tokenTypes() const override {
    return kTypes;
  }
  [[nodiscard]] std::span<const std::string_view> tokenModifiers()
      const override {
    return kModifiers;
  }

 protected:
  void computeHighlighting(
      const cursiv::Document& document, cursiv::TextOffset begin,
      cursiv::TextOffset end, cursiv::SemanticTokenAcceptor accept,
      const cursiv::CancellationToken& token) const override {
    const std::uint32_t declaration = tokenModifier("declaration");
    const std::optional<std::uint32_t> keyword = tokenType("keyword");
    GrammarSemanticTokenProvider::computeHighlighting(
        document, begin, end,
        [&](cursiv::SemanticToken coloured) {
          if (coloured.type != keyword) coloured.modifiers |= declaration;
          accept(coloured);
        },
        token);
  }
};

void a_language_declares_its_own_types_and_modifiers() {
  const World world;
  const OwnLegend provider(*world.ws);
  const lsp::SemanticTokensOptions options = provider.semanticTokensOptions();
  CHECK(std::ranges::equal(options.legend.tokenTypes, OwnLegend::kTypes));
  CHECK(
      std::ranges::equal(options.legend.tokenModifiers, OwnLegend::kModifiers));
  CHECK(provider.tokenType("label") == 3u);
  CHECK(!provider.tokenType("comment").has_value());
  CHECK(provider.tokenModifier("declaration") == 2u);
  CHECK(provider.tokenModifier("static") == 0u);

  // The protocol's integers stop below 2^31: the 31st modifier has the last
  // bit, and one more has none.
  struct LongLegend final : cursiv::GrammarSemanticTokenProvider {
    using GrammarSemanticTokenProvider::GrammarSemanticTokenProvider;
    std::array<std::string, 32> names;
    std::array<std::string_view, 32> views;
    [[nodiscard]] std::span<const std::string_view> tokenModifiers()
        const override {
      return views;
    }
  };
  LongLegend longer(*world.ws);
  for (std::size_t i = 0; i != longer.names.size(); ++i) {
    longer.names[i] = "m" + std::to_string(i);
    longer.views[i] = longer.names[i];
  }
  CHECK(longer.tokenModifier("m30") == std::uint32_t{1} << 30);
  CHECK(longer.tokenModifier("m31") == 0u);

  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  // No comment, operator or reference: the legend does not name them.
  const std::vector<Decoded> expected = {
      {1, 0, 8, 0, 0}, {1, 9, 6, 1, 2}, {2, 0, 6, 0, 0},
      {2, 7, 6, 2, 2}, {3, 2, 4, 1, 2},
  };
  CHECK(decode(provider.semanticHighlight(*document, whole(*document))) ==
        expected);
}

// The grammar's colours, with `delta` on.
struct DeltaColours final : cursiv::GrammarSemanticTokenProvider {
  using GrammarSemanticTokenProvider::GrammarSemanticTokenProvider;

  [[nodiscard]] lsp::SemanticTokensOptions semanticTokensOptions()
      const override {
    lsp::SemanticTokensOptions options =
        GrammarSemanticTokenProvider::semanticTokensOptions();
    lsp::SemanticTokensFullDelta full;
    full.delta = true;
    options.full = std::move(full);
    return options;
  }
};

struct DeltaWorld : support::LspWorkspace {
  DeltaWorld()
      : WorkspaceWorld([](cursiv::LspServices& language) {
          language.semanticTokenProvider =
              std::make_unique<const DeltaColours>(language);
        }) {}

  [[nodiscard]] cursiv::LanguageServer& server() const {
    return *shared.languageServer;
  }
};

// `data` with `edits` applied, as the client applies them.
[[nodiscard]] std::vector<lsp::Uint> applied(
    std::vector<lsp::Uint> data, const lsp::SemanticTokensDelta& delta) {
  for (auto it = delta.edits.rbegin(); it != delta.edits.rend(); ++it) {
    const auto at = data.begin() + it->start;
    data.erase(at, at + it->deleteCount);
    if (it->data.has_value())
      data.insert(data.begin() + it->start, it->data->begin(), it->data->end());
  }
  return data;
}

// With `delta` on, a full answer has a result id, and a delta from it sends
// only the edits. A delta from an id not kept gets a full answer; a range
// answer has no id.
void a_delta_sends_only_what_changed() {
  constexpr std::string_view kA = "file:///w/a.ws";
  constexpr std::string_view kB = "file:///w/b.ws";
  const DeltaWorld world;
  (void)world.server().initialize({});
  support::open(world.server(), kA, "workspace", kModel);

  lsp::SemanticTokensParams full;
  full.textDocument.uri = lsp::Uri::parse(std::string(kA));
  const lsp::SemanticTokens first = world.server().semanticTokensFull(full);
  CHECK(first.resultId.has_value() && !first.data.empty());
  if (!first.resultId.has_value()) return;

  lsp::SemanticTokensDeltaParams since;
  since.textDocument = full.textDocument;
  since.previousResultId = *first.resultId;
  const auto same = world.server().semanticTokensFullDelta(since);
  const auto* const unchanged = std::get_if<lsp::SemanticTokensDelta>(&same);
  CHECK(unchanged != nullptr && unchanged->edits.empty() &&
        unchanged->resultId.has_value() &&
        unchanged->resultId != first.resultId);
  if (unchanged == nullptr || !unchanged->resultId.has_value()) return;

  // One more feature, in the middle of the document.
  support::change(world.server(), kA, 2,
                  "/* a comment */\n"
                  "datatype String\n"
                  "entity Person {\n"
                  "  name : String\n"
                  "  nick : String\n"
                  "}\n");
  since.previousResultId = *unchanged->resultId;
  const auto moved = world.server().semanticTokensFullDelta(since);
  const auto* const edited = std::get_if<lsp::SemanticTokensDelta>(&moved);
  CHECK(edited != nullptr && edited->edits.size() == 1);
  if (edited == nullptr || edited->edits.empty()) return;
  const std::vector<lsp::Uint> now =
      world.server().semanticTokensFull(full).data;
  CHECK(applied(first.data, *edited) == now);
  CHECK(edited->edits.front().data.has_value() &&
        edited->edits.front().data->size() < now.size());

  // An answer older than the last one is not kept.
  const auto stale = world.server().semanticTokensFullDelta(since);
  const auto* const replaced = std::get_if<lsp::SemanticTokens>(&stale);
  CHECK(replaced != nullptr && replaced->data == now &&
        replaced->resultId.has_value());
  if (replaced == nullptr || !replaced->resultId.has_value()) return;

  lsp::SemanticTokensRangeParams range;
  range.textDocument = full.textDocument;
  range.range = {{0, 0}, {2, 0}};
  CHECK(!world.server().semanticTokensRange(range).resultId.has_value());

  // Closed, the document leaves, and so does its last answer: reopened, it
  // is coloured whole.
  lsp::DidCloseTextDocumentParams closing;
  closing.textDocument.uri = full.textDocument.uri;
  world.server().didClose(closing);
  support::open(world.server(), kB, "workspace", kModel);
  lsp::SemanticTokensParams other;
  other.textDocument.uri = lsp::Uri::parse(std::string(kB));
  (void)world.server().semanticTokensFull(other);
  support::open(world.server(), kA, "workspace", kModel);
  since.previousResultId = *replaced->resultId;
  const auto reopened = world.server().semanticTokensFullDelta(since);
  CHECK(std::holds_alternative<lsp::SemanticTokens>(reopened));

  // A document no tab holds keeps no answer: once opened, it is coloured
  // whole again.
  constexpr std::string_view kC = "file:///w/c.ws";
  const cursiv::Document* const closed = world.built(kC, std::string(kModel));
  lsp::SemanticTokensParams unopened;
  unopened.textDocument.uri = lsp::Uri::parse(std::string(kC));
  const lsp::SemanticTokens kept =
      world.ws->semanticTokenProvider->semanticHighlight(*closed, unopened);
  CHECK(!closed->isOpen && kept.resultId.has_value());
  if (!kept.resultId.has_value()) return;
  support::open(world.server(), kC, "workspace", kModel);
  since.textDocument = unopened.textDocument;
  since.previousResultId = *kept.resultId;
  CHECK(std::holds_alternative<lsp::SemanticTokens>(
      world.server().semanticTokensFullDelta(since)));
}

// A request cancelled once its colours are computed keeps nothing: the answer
// the client holds stays the base of its next delta.
void a_cancelled_answer_keeps_the_last_base() {
  // The grammar's colours with `delta` on; once `cancel` is set, it cancels
  // its request and colours nothing.
  struct Cancels final : cursiv::GrammarSemanticTokenProvider {
    using GrammarSemanticTokenProvider::GrammarSemanticTokenProvider;
    mutable cursiv::CancellationTokenSource* cancel = nullptr;
    [[nodiscard]] lsp::SemanticTokensOptions semanticTokensOptions()
        const override {
      lsp::SemanticTokensOptions options =
          GrammarSemanticTokenProvider::semanticTokensOptions();
      lsp::SemanticTokensFullDelta full;
      full.delta = true;
      options.full = std::move(full);
      return options;
    }

   protected:
    void computeHighlighting(
        const cursiv::Document& document, cursiv::TextOffset begin,
        cursiv::TextOffset end, cursiv::SemanticTokenAcceptor accept,
        const cursiv::CancellationToken& token) const override {
      if (cancel != nullptr) {
        cancel->request_stop();
        return;
      }
      GrammarSemanticTokenProvider::computeHighlighting(document, begin, end,
                                                        accept, token);
    }
  };
  const Cancels* provider = nullptr;
  const support::LspWorkspace world([&](cursiv::LspServices& language) {
    auto held = std::make_unique<const Cancels>(language);
    provider = held.get();
    language.semanticTokenProvider = std::move(held);
  });
  constexpr std::string_view kA = "file:///w/a.ws";
  (void)world.shared.languageServer->initialize({});
  support::open(*world.shared.languageServer, kA, "workspace", kModel);
  const cursiv::Document& document = *world.shared.documents->getDocument(kA);
  lsp::SemanticTokensParams full;
  full.textDocument.uri = lsp::Uri::parse(std::string(kA));
  const lsp::SemanticTokens first =
      provider->semanticHighlight(document, full, cursiv::kNoCancel);
  CHECK(first.resultId.has_value() && !first.data.empty());
  if (!first.resultId.has_value()) return;
  lsp::SemanticTokensDeltaParams since;
  since.textDocument = full.textDocument;
  since.previousResultId = *first.resultId;

  cursiv::CancellationTokenSource source;
  provider->cancel = &source;
  bool cancelled = false;
  try {
    (void)provider->semanticHighlightDelta(document, since, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  provider->cancel = nullptr;
  CHECK(cancelled);
  const auto next =
      provider->semanticHighlightDelta(document, since, cursiv::kNoCancel);
  const auto* const delta = std::get_if<lsp::SemanticTokensDelta>(&next);
  CHECK(delta != nullptr && delta->edits.empty());
}

// A document that never parsed is answered with an empty array, which the
// client reads as "no colouring" -- never with an error.
void a_document_with_no_tree_is_coloured_with_nothing() {
  const World world;
  cursiv::Document* const document = world.built("file:///w/empty.ws", "");
  const lsp::SemanticTokens tokens =
      world.ws->semanticTokenProvider->semanticHighlight(
          *document, whole(*document), cursiv::kNoCancel);
  CHECK(tokens.data.empty());
}

}  // namespace

void run() {
  every_expected_category_is_coloured();
  the_tokens_are_ordered_and_never_overlap();
  every_index_names_a_row_of_the_legend();
  a_range_is_the_full_answer_cut_to_it();
  unicode_and_multiline_tokens_keep_their_positions();
  a_language_declares_its_own_types_and_modifiers();
  a_delta_sends_only_what_changed();
  a_cancelled_answer_keeps_the_last_base();
  a_document_with_no_tree_is_coloured_with_nothing();
}

}  // namespace semantic_token_provider

// The signature help provider gets the whole request: its context tells a
// retrigger from a first request, and its trigger characters reach the
// handshake.
namespace signature_help_provider {

namespace {

constexpr std::string_view kUri = "file:///w/a.ws";

// One signature, labelled by whether the popup was reopened.
struct ContextEcho final : cursiv::SignatureHelpProvider {
  [[nodiscard]] lsp::SignatureHelpOptions signatureHelpOptions()
      const override {
    lsp::SignatureHelpOptions options;
    options.triggerCharacters = lsp::Array<lsp::String>{"("};
    options.retriggerCharacters = lsp::Array<lsp::String>{","};
    return options;
  }

  [[nodiscard]] std::optional<lsp::SignatureHelp> provideSignatureHelp(
      const cursiv::Document&, const lsp::SignatureHelpParams& params,
      const cursiv::CancellationToken&) const override {
    lsp::SignatureHelp help;
    lsp::SignatureInformation signature;
    const bool reopened =
        params.context.has_value() && params.context->isRetrigger;
    signature.label = reopened ? "again" : "first";
    help.signatures.push_back(std::move(signature));
    return help;
  }
};

struct SilentClient final : cursiv::LanguageClient {
  void publishDiagnostics(lsp::PublishDiagnosticsParams) override {}
};

struct World : support::LspWorkspace {
  World()
      : WorkspaceWorld([](cursiv::LspServices& language) {
          language.signatureHelpProvider = std::make_unique<ContextEcho>();
        }) {
    shared.languageClient = std::make_unique<SilentClient>();
  }

  [[nodiscard]] cursiv::LanguageServer& server() const {
    return *shared.languageServer;
  }

  void open(std::string_view text) const {
    support::open(server(), kUri, "workspace", text);
  }
};

void the_triggers_reach_the_handshake() {
  const World world;
  const lsp::InitializeParams handshake;
  const lsp::InitializeResult result = world.server().initialize(handshake);
  CHECK(result.capabilities.signatureHelpProvider.has_value());
  if (!result.capabilities.signatureHelpProvider.has_value()) return;
  const lsp::SignatureHelpOptions& announced =
      *result.capabilities.signatureHelpProvider;
  CHECK(announced.triggerCharacters.has_value() &&
        announced.triggerCharacters->front() == "(");
  CHECK(announced.retriggerCharacters.has_value() &&
        announced.retriggerCharacters->front() == ",");
}

void the_context_reaches_the_provider() {
  const World world;
  const lsp::InitializeParams handshake;
  (void)world.server().initialize(handshake);
  world.open("entity A {\n}\n");

  lsp::SignatureHelpParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(kUri));
  params.position = lsp::Position{0, 7};
  const std::optional<lsp::SignatureHelp> first =
      world.server().signatureHelp(params, cursiv::kNoCancel);
  CHECK(first.has_value() && !first->signatures.empty() &&
        first->signatures.front().label == "first");

  lsp::SignatureHelpContext context;
  context.triggerKind = lsp::SignatureHelpTriggerKindEnum(
      lsp::SignatureHelpTriggerKind::TriggerCharacter);
  context.isRetrigger = true;
  context.triggerCharacter = std::string(",");
  params.context = std::move(context);
  const std::optional<lsp::SignatureHelp> again =
      world.server().signatureHelp(params, cursiv::kNoCancel);
  CHECK(again.has_value() && !again->signatures.empty() &&
        again->signatures.front().label == "again");
}

}  // namespace

void run() {
  the_triggers_reach_the_handshake();
  the_context_reaches_the_provider();
}

}  // namespace signature_help_provider

namespace code_action_provider {

namespace {

using World = support::LspWorkspace;

// A missing `:` that the parser repairs, giving one diagnostic with a quick
// fix.
constexpr const char* kBroken = "datatype String\nentity A { x  String }\n";

[[nodiscard]] lsp::CodeActionParams over(const cursiv::Document& document) {
  lsp::CodeActionParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(document.uri));
  params.range = {{0, 0}, {2, 0}};
  return params;
}

[[nodiscard]] bool has_kind(const std::vector<lsp::CodeAction>& actions,
                            lsp::CodeActionKind kind) {
  return std::ranges::any_of(actions, [kind](const lsp::CodeAction& action) {
    return action.kind.has_value() && *action.kind == kind;
  });
}

// A language's own action, of a kind the built-in fix never has.
struct WithSourceAction final : cursiv::DefaultCodeActionProvider {
  void appendCodeActions(const cursiv::Document&, const lsp::CodeActionParams&,
                         std::vector<lsp::CodeAction>& actions,
                         const cursiv::CancellationToken&) const override {
    lsp::CodeAction own;
    own.title = "organize";
    own.kind = lsp::CodeActionKind::Source;
    actions.push_back(std::move(own));
  }
};

// No `only`, or one naming quick fixes: the fix is offered.
void the_built_in_fix_answers_a_request_that_accepts_quick_fixes() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", kBroken);
  const cursiv::DefaultCodeActionProvider provider;

  lsp::CodeActionParams params = over(*document);
  const auto actions =
      provider.getCodeActions(*document, params, cursiv::kNoCancel);
  CHECK(has_kind(actions, lsp::CodeActionKind::QuickFix));
  CHECK(!actions.empty());
  if (!actions.empty()) {
    const lsp::CodeAction& action = actions.front();
    CHECK(action.kind == lsp::CodeActionKind::QuickFix);
    CHECK(action.edit.has_value() && action.edit->changes.has_value());
  }

  params.context.only = lsp::Array<lsp::CodeActionKindEnum>{
      lsp::CodeActionKindEnum(lsp::CodeActionKind::QuickFix)};
  CHECK(has_kind(provider.getCodeActions(*document, params, cursiv::kNoCancel),
                 lsp::CodeActionKind::QuickFix));
}

void an_only_of_another_kind_withholds_the_fix_and_still_asks_the_hook() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", kBroken);
  const WithSourceAction provider;

  lsp::CodeActionParams params = over(*document);
  params.context.only = lsp::Array<lsp::CodeActionKindEnum>{
      lsp::CodeActionKindEnum(lsp::CodeActionKind::Source)};
  const std::vector<lsp::CodeAction> actions =
      provider.getCodeActions(*document, params, cursiv::kNoCancel);
  CHECK(actions.size() == 1);
  CHECK(has_kind(actions, lsp::CodeActionKind::Source));
  CHECK(!has_kind(actions, lsp::CodeActionKind::QuickFix));

  // With no `only`, both are offered, the built-in fix first.
  params.context.only = std::nullopt;
  const std::vector<lsp::CodeAction> both =
      provider.getCodeActions(*document, params, cursiv::kNoCancel);
  CHECK(both.size() == 2);
  CHECK(both.front().kind == lsp::CodeActionKind::QuickFix);
  CHECK(both.back().kind == lsp::CodeActionKind::Source);
}

// A fix is dropped once a new text document replaces the one it was computed
// on, since its offsets may be stale. A new version number alone keeps it.
void a_row_of_another_revision_is_dropped() {
  const World world;
  cursiv::Document* const document = world.built("file:///w/a.ws", kBroken);
  const cursiv::DefaultCodeActionProvider provider;
  const lsp::CodeActionParams params = over(*document);
  CHECK(!provider.getCodeActions(*document, params, cursiv::kNoCancel).empty());

  world.shared.documentFactory->attachVersion(*document, 7);
  CHECK(!provider.getCodeActions(*document, params, cursiv::kNoCancel).empty());

  world.shared.documentFactory->attachTextDocument(
      *document,
      cursiv::TextDocument::create(
          document->uri, std::string(document->textDocument().languageId()), 8,
          std::string(document->textDocument().getText())));
  CHECK(provider.getCodeActions(*document, params, cursiv::kNoCancel).empty());
}

void a_misspelt_reference_is_offered_its_neighbour() {
  const World world;
  const cursiv::Document* const document = world.built(
      "file:///w/b.ws", "datatype String\nentity A { x : Strng }\n");
  const cursiv::DefaultCodeActionProvider provider;
  bool offered = false;
  for (const lsp::CodeAction& action :
       provider.getCodeActions(*document, over(*document), cursiv::kNoCancel)) {
    if (action.title != "Replace with 'String'") continue;
    offered = true;
    CHECK(action.edit.has_value() && action.edit->changes.has_value());
    if (!action.edit.has_value() || !action.edit->changes.has_value()) break;
    for (const auto& [uri, edits] : *action.edit->changes)
      for (const lsp::TextEdit& edit : edits) CHECK(edit.newText == "String");
  }
  CHECK(offered);
}

// One request over every linking error walks the document's references once,
// however many errors it answers: the fixes are linear in the request.
void a_request_walks_the_references_once() {
  const World world;
  std::string text = "datatype String\n";
  for (int i = 0; i < 40; ++i)
    text += "entity E" + std::to_string(i) + " { x : Strng }\n";
  cursiv::Document* const document = world.built("file:///w/c.ws", text);
  auto counting = std::make_unique<CountingReferences>(
      std::move(document->parseResult.references));
  const CountingReferences& walked = *counting;
  document->parseResult.references = std::move(counting);

  lsp::CodeActionParams params = over(*document);
  params.range = {{0, 0}, {42, 0}};
  const cursiv::DefaultCodeActionProvider provider;
  const std::vector<lsp::CodeAction> actions =
      provider.getCodeActions(*document, params, cursiv::kNoCancel);
  CHECK(std::ranges::count(actions, std::string("Replace with 'String'"),
                           &lsp::CodeAction::title) == 40);
  CHECK(walked.walks == 1);
}

void a_range_touching_no_finding_answers_nothing() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", kBroken);
  const cursiv::DefaultCodeActionProvider provider;
  lsp::CodeActionParams params = over(*document);
  params.range = {{0, 0}, {0, 5}};
  CHECK(provider.getCodeActions(*document, params, cursiv::kNoCancel).empty());
}

struct ObservedActions final : cursiv::DefaultCodeActionProvider {
  mutable bool called = false;

  void appendCodeActions(const cursiv::Document&, const lsp::CodeActionParams&,
                         std::vector<lsp::CodeAction>&,
                         const cursiv::CancellationToken&) const override {
    called = true;
  }
};

void cancellation_precedes_a_filter_that_excludes_quick_fixes() {
  const cursiv::Document document(
      cursiv::TextDocument::create("file:///cancel.ws", "workspace", 1, ""));
  const ObservedActions provider;
  lsp::CodeActionParams params;
  params.context.only = lsp::Array<lsp::CodeActionKindEnum>{
      lsp::CodeActionKindEnum(lsp::CodeActionKind::Source)};
  cursiv::CancellationTokenSource source;
  source.request_stop();
  CHECK(support::throws<cursiv::OperationCancelled>([&] {
    (void)provider.getCodeActions(document, params, source.get_token());
  }));
  CHECK(!provider.called);
  CHECK(provider.getCodeActions(document, params).empty());
  CHECK(provider.called);
}

void an_action_preserves_the_matching_diagnostic_and_rejects_stale_payloads() {
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///diagnostic.ws", "workspace", 1, "a"));
  cursiv::Diagnostic diagnostic;
  diagnostic.range = {0, 1};
  diagnostic.message = "replace a";
  diagnostic.source = "audit";
  diagnostic.severity = cursiv::DiagnosticSeverity::Warning;
  diagnostic.code = cursiv::DiagnosticCode("audit.replace");
  cursiv::JsonValue::Object data;
  data[std::string(cursiv::detail::kDefaultCodeActionsKey)] =
      cursiv::JsonValue::Array{cursiv::detail::make_action_row(
          cursiv::detail::kReplaceEdit, "replace", {0, 1}, "b", 0)};
  data["numbers"] = cursiv::JsonValue::Array{
      cursiv::JsonValue(1.0), cursiv::JsonValue(cursiv::JsonValue::Object{
                                  {"nested", cursiv::JsonValue(2.0)}})};
  diagnostic.data = cursiv::JsonValue(std::move(data));
  document.diagnostics.push_back(std::move(diagnostic));

  lsp::CodeActionParams params;
  params.range = {{0, 0}, {0, 1}};
  lsp::Diagnostic echo;
  echo.range = params.range;
  echo.message = std::string("replace a");
  echo.severity = lsp::DiagnosticSeverity::Warning;
  echo.source = "audit";
  echo.code = std::string("audit.replace");
  echo.data = cursiv::detail::to_lsp_any(*document.diagnostics.front().data);
  echo.tags =
      lsp::Array<lsp::DiagnosticTagEnum>{lsp::DiagnosticTag::Deprecated};
  lsp::CodeDescription description;
  description.href = lsp::Uri::parse("https://example.test/diagnostic");
  echo.codeDescription = std::move(description);
  lsp::DiagnosticRelatedInformation related;
  related.location.uri = lsp::Uri::parse("file:///related.ws");
  related.location.range = {{2, 3}, {2, 4}};
  related.message = "related";
  echo.relatedInformation =
      lsp::Array<lsp::DiagnosticRelatedInformation>{std::move(related)};
  params.context.diagnostics.push_back(echo);

  const cursiv::DefaultCodeActionProvider provider;
  const auto actions = provider.getCodeActions(document, params);
  CHECK(actions.size() == 1);
  if (actions.size() != 1) return;
  CHECK(actions.front().diagnostics.has_value());
  if (!actions.front().diagnostics.has_value()) return;
  CHECK(actions.front().diagnostics->size() == 1);
  if (actions.front().diagnostics->size() != 1) return;
  const lsp::Diagnostic& about = actions.front().diagnostics->front();
  CHECK(about.severity == lsp::DiagnosticSeverity::Warning);
  CHECK(about.source == echo.source);
  CHECK(about.code == echo.code);
  CHECK(about.data == echo.data);
  CHECK(about.codeDescription.has_value());
  CHECK(about.tags.has_value() && about.tags->size() == 1);
  CHECK(about.relatedInformation.has_value() &&
        about.relatedInformation->size() == 1);
  if (about.relatedInformation.has_value() &&
      !about.relatedInformation->empty())
    CHECK(about.relatedInformation->front().message == "related");

  auto& numbers = params.context.diagnostics.front().data->object()["numbers"];
  numbers.array().front() = lsp::LSPAny(1);
  numbers.array().back().object()["nested"] = lsp::LSPAny(2);
  const auto normalized = provider.getCodeActions(document, params);
  CHECK(normalized.size() == 1);
  if (normalized.size() == 1 && normalized.front().diagnostics.has_value() &&
      !normalized.front().diagnostics->empty()) {
    CHECK(normalized.front().diagnostics->front().data ==
          params.context.diagnostics.front().data);
    CHECK(normalized.front().diagnostics->front().tags.has_value());
  } else {
    CHECK(false);
  }

  numbers.array().front() = lsp::LSPAny(3);
  const auto changed = provider.getCodeActions(document, params);
  CHECK(changed.size() == 1);
  if (changed.size() == 1 && changed.front().diagnostics.has_value() &&
      !changed.front().diagnostics->empty())
    CHECK(!changed.front().diagnostics->front().data.has_value());
  else
    CHECK(false);

  params.context.diagnostics.front() = echo;
  auto& duplicate = params.context.diagnostics.front().data->object();
  duplicate.remove("numbers");
  duplicate.append(std::string(cursiv::detail::kDefaultCodeActionsKey),
                   duplicate.get(cursiv::detail::kDefaultCodeActionsKey));
  const auto duplicated = provider.getCodeActions(document, params);
  CHECK(duplicated.size() == 1);
  if (duplicated.size() == 1 && duplicated.front().diagnostics.has_value() &&
      duplicated.front().diagnostics->size() == 1)
    CHECK(!duplicated.front().diagnostics->front().data.has_value());
  else
    CHECK(false);

  params.context.diagnostics.front().data = lsp::LSPAny("stale");
  const auto fresh = provider.getCodeActions(document, params);
  CHECK(fresh.size() == 1);
  if (fresh.size() != 1) return;
  CHECK(fresh.front().diagnostics.has_value());
  if (!fresh.front().diagnostics.has_value()) return;
  CHECK(fresh.front().diagnostics->size() == 1);
  if (fresh.front().diagnostics->size() != 1) return;
  CHECK(!fresh.front().diagnostics->front().data.has_value());
}

}  // namespace

void run() {
  the_built_in_fix_answers_a_request_that_accepts_quick_fixes();
  an_only_of_another_kind_withholds_the_fix_and_still_asks_the_hook();
  a_row_of_another_revision_is_dropped();
  a_range_touching_no_finding_answers_nothing();
  a_misspelt_reference_is_offered_its_neighbour();
  a_request_walks_the_references_once();
  cancellation_precedes_a_filter_that_excludes_quick_fixes();
  an_action_preserves_the_matching_diagnostic_and_rejects_stale_payloads();
}

}  // namespace code_action_provider

// Virtual documents: a registered scheme must match the URI exactly, and a
// document in the store is served by URI.
namespace text_document_content_provider {

namespace {

using World = support::CoreWorkspace;

[[nodiscard]] std::optional<lsp::TextDocumentContentResult> ask(
    const cursiv::TextDocumentContentProvider& provider,
    const std::string& uri) {
  lsp::TextDocumentContentParams params;
  params.uri = lsp::Uri::parse(uri);
  return provider.provideTextDocumentContent(params);
}

void a_held_document_is_served_by_uri() {
  const World world;
  world.add("builtin:///lib.ws", "datatype String\n");
  const cursiv::DefaultTextDocumentContentProvider provider(world.shared,
                                                            {"builtin"});
  const std::optional<lsp::TextDocumentContentResult> answer =
      ask(provider, "builtin:///lib.ws");
  CHECK(answer.has_value() && answer->text == "datatype String\n");
  CHECK(!ask(provider, "builtin:///other.ws").has_value());
}

void a_scheme_matches_whatever_its_case() {
  const World world;
  world.add("builtin:///lib.ws", "datatype String\n");
  const cursiv::DefaultTextDocumentContentProvider provider(world.shared,
                                                            {"BuiltIn"});
  CHECK(ask(provider, "builtin:///lib.ws").has_value());
  CHECK(ask(provider, "BuiltIn:///lib.ws").has_value());
  CHECK(!ask(provider, "built:///lib.ws").has_value());
  CHECK(provider.schemes().size() == 1 &&
        provider.schemes().front() == "BuiltIn");
}

void an_undeclared_scheme_answers_nothing() {
  const World world;
  world.add("file:///w/a.ws", "datatype String\n");
  const cursiv::DefaultTextDocumentContentProvider provider(world.shared,
                                                            {"builtin"});
  CHECK(!ask(provider, "file:///w/a.ws").has_value());
  const cursiv::DefaultTextDocumentContentProvider none(world.shared, {});
  CHECK(!ask(none, "file:///w/a.ws").has_value());
}

}  // namespace

void run() {
  a_held_document_is_served_by_uri();
  a_scheme_matches_whatever_its_case();
  an_undeclared_scheme_answers_nothing();
}

}  // namespace text_document_content_provider

int main() {
  cursiv::test::count_expectations();
  folding_range_provider::run();
  symbol_edge_cases::run();
  selection_range_provider::run();
  semantic_token_provider::run();
  signature_help_provider::run();
  code_action_provider::run();
  text_document_content_provider::run();
  return cursiv::test::report("presentation");
}
