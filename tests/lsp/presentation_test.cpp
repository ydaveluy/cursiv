// What an editor draws over a document and offers beside it: the folds, the
// selections and the colours, the signature popup, the quick fixes, and the
// virtual documents a scheme serves.

#include <lsp/messages.h>
#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/MultiRefItems.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"
#include "cursiv/lsp/actions/JsonBridge.hpp"
#include "cursiv/lsp/completion/FuzzyMatcher.hpp"
#include "cursiv/lsp/content/DefaultTextDocumentContentProvider.hpp"
#include "cursiv/lsp/content/TextDocumentContentProvider.hpp"
#include "cursiv/lsp/ranges/DefaultFoldingRangeProvider.hpp"
#include "cursiv/lsp/ranges/FoldingRangeProvider.hpp"
#include "cursiv/lsp/ranges/SelectionRangeProvider.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/semantic/SignatureHelpProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/lsp/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/DecodedSemanticTokens.hpp"
#include "lsp/uri.h"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/core/ast.hpp"

namespace {

// A URI of its own for each document, so one lane's documents never meet.
struct MintingWorld : support::LspWorkspace {
  mutable unsigned minted = 0;

  cursiv::Document* built(std::string text) const {
    return WorkspaceWorld::built(
        "file:///w/a" + std::to_string(minted++) + ".ws", std::move(text));
  }
};

}  // namespace

// What `textDocument/foldingRange` answers: one fold per AST node under the
// root spanning three lines, the closer left visible, one per comment record
// of the same size -- and the hooks a language folds otherwise through.
namespace folding_range_provider {

namespace {

struct World : MintingWorld {
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

// Under three lines nothing folds, whatever the record ends in; and the
// document's own root, which spans everything, is never a fold.
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

// A comment folds per record: a stack of line comments is three records of
// one line each, and none of them folds.
void a_stack_of_line_comments_is_not_a_fold() {
  const World world;
  const std::vector<lsp::FoldingRange> ranges =
      world.folds("// one\n// two\n// three\nentity E {}\n");
  CHECK(std::ranges::none_of(ranges, is_comment));
  // ...where one block comment of three lines is, before the entry as well.
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

  // Refusing the package's content skips the entity inside it, and the
  // package itself still folds; the comment is not content.
  const ClosedPackages closed(*world.ws);
  const std::vector<lsp::FoldingRange> shallow =
      world.folds(std::string(kModel), &closed);
  CHECK(starting(shallow, 5) == nullptr);
  CHECK(starting(shallow, 0) != nullptr);
  CHECK(std::ranges::any_of(shallow, is_comment));
}

// The last-line hook sees the record and the kind: a language may keep a
// closer inside its fold.
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

}  // namespace

void run() {
  a_fold_carries_its_lines_and_its_columns();
  a_record_under_three_lines_and_the_root_do_not_fold();
  a_document_with_lone_cr_line_breaks_folds();
  a_stack_of_line_comments_is_not_a_fold();
  a_language_chooses_which_nodes_fold();
  a_language_may_keep_the_closer_in_its_fold();
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
  for (cursiv::AstNode* node : document->parseResult.nodes)
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

// What `textDocument/selectionRange` answers: the word under the caret, then
// each containing record up to the document -- and what a caret nothing
// covers, or a word past ASCII, gets.
namespace selection_range_provider {

namespace {

struct World : MintingWorld {
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

// Expand-selection is a chain and not a range: the word, then the records
// around it, up to the document.
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

  // ...and the chain widens strictly, ending on something that covers the
  // whole text.
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

// A caret nothing covers still gets its entry, and that entry is inside the
// document: the position is clamped the way every other range is.
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

// A word past ASCII is one word, and a dash past ASCII ends one: the
// innermost step is what an ASCII boundary would have cut.
void a_word_past_ascii_is_read_by_scalar() {
  const World world;
  // `Über` inside a comment: four scalars, four UTF-16 units.
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

// The colours, read back off the wire: one flat array of integers, five per
// token, each a delta from the one before it. What is checked here is the
// decoding -- the absolute spans it yields, their order and disjointness, and
// that every type index names a row of the announced legend.
namespace semantic_token_provider {

namespace {

namespace ct = cursiv::test;

using World = support::LspWorkspace;

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
                           cursiv::SemanticTokenType type) {
  for (const ct::DecodedSemanticToken& token : tokens)
    if (token.tokenType == type) return true;
  return false;
}

// 0: /* a comment */
// 1: datatype String
// 2: entity Person {
// 3:   name : String
// 4: }
constexpr std::string_view kModel =
    "/* a comment */\n"
    "datatype String\n"
    "entity Person {\n"
    "  name : String\n"
    "}\n";

// The three questions the walk answers, on one text: which declarations own
// others and which do not, which names stand for a declaration elsewhere, and
// which records are the author's prose.
void every_expected_category_is_coloured() {
  const World world;
  // Each marked span is one category: the comment, a declaration owning
  // nothing, one owning a feature, its feature, and the name standing for a
  // declaration elsewhere.
  const ct::DecodedSemanticTokensWithRanges coloured =
      ct::highlightHelper(*world.ws,
                          "<|/* a comment */|>\n"
                          "datatype <|String|>\n"
                          "entity <|Person|> {\n"
                          "  <|name|> : <|String|>\n"
                          "}\n");
  CHECK(!coloured.tokens.empty());

  const cursiv::SemanticTokenType expected[] = {
      cursiv::SemanticTokenType::Comment, cursiv::SemanticTokenType::Property,
      cursiv::SemanticTokenType::Type, cursiv::SemanticTokenType::Property,
      cursiv::SemanticTokenType::Variable};
  std::size_t marked = 0;
  for (const cursiv::SemanticTokenType type : expected)
    ct::expectSemanticToken(coloured,
                            {.rangeIndex = marked++, .tokenType = type});

  // `datatype`/`entity` are words the grammar reserved; `{`/`:` are not.
  CHECK(carries(coloured.tokens, cursiv::SemanticTokenType::Keyword));
  CHECK(carries(coloured.tokens, cursiv::SemanticTokenType::Operator));
}

// The encoding has no way to state a token before the one it follows, so the
// walk owes the client source order -- and disjointness, since a byte coloured
// twice is a colour the client picks at random.
void the_tokens_are_ordered_and_never_overlap() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const lsp::SemanticTokens raw =
      world.ws->semanticTokenProvider->semanticHighlight(*document,
                                                         cursiv::kNoCancel);
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
  // No modifiers are declared, so none is claimed.
  for (const Decoded& token : tokens) CHECK(token.modifiers == 0);
}

// A type index is an index into the announced legend: one that ran past it
// would colour with a row the client never received.
void every_index_names_a_row_of_the_legend() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const std::vector<Decoded> tokens =
      decode(world.ws->semanticTokenProvider->semanticHighlight(
          *document, cursiv::kNoCancel));
  const std::size_t rows = cursiv::semantic_tokens_legend().tokenTypes.size();
  CHECK(rows > 0);
  for (const Decoded& token : tokens) CHECK(token.type < rows);
  // ...and the legend declares no modifier, which is what the zero above says.
  CHECK(cursiv::semantic_tokens_legend().tokenModifiers.empty());
}

// A range answers the tokens that touch it and nothing beyond, on the same
// encoding: decoded, it is the full answer cut to the range, whether the range
// takes whole lines, cuts through a word, or covers everything.
void a_range_is_the_full_answer_cut_to_it() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const std::vector<Decoded> full =
      decode(world.ws->semanticTokenProvider->semanticHighlight(
          *document, cursiv::kNoCancel));
  const lsp::Range ranges[] = {
      {{2, 0}, {4, 0}},
      {{3, 3}, {3, 5}},
      {{0, 0}, {5, 0}},
  };
  for (const lsp::Range& range : ranges) {
    std::vector<Decoded> expected;
    for (const Decoded& token : full)
      if (touches(token, range)) expected.push_back(token);
    CHECK(!expected.empty());
    const std::vector<Decoded> cut =
        decode(world.ws->semanticTokenProvider->semanticHighlightRange(
            *document, range, cursiv::kNoCancel));
    CHECK(cut == expected);
  }
  // The word alone, cut through: coloured whole.
  const std::vector<Decoded> word =
      decode(world.ws->semanticTokenProvider->semanticHighlightRange(
          *document, ranges[1], cursiv::kNoCancel));
  CHECK(word.size() == 1 && word.front().line == 3 && word.front().start == 2 &&
        word.front().length == 4);
}

// Columns count UTF-16 units even after non-ASCII comments. Newline spelling
// does not affect the answer, and a multiline comment omits its empty line.
void unicode_and_multiline_tokens_keep_their_positions() {
  const auto type = [](cursiv::SemanticTokenType value) {
    return static_cast<lsp::Uint>(value);
  };
  const auto comment = type(cursiv::SemanticTokenType::Comment);
  const auto keyword = type(cursiv::SemanticTokenType::Keyword);
  const auto property = type(cursiv::SemanticTokenType::Property);
  const auto owner = type(cursiv::SemanticTokenType::Type);
  const auto op = type(cursiv::SemanticTokenType::Operator);
  const auto variable = type(cursiv::SemanticTokenType::Variable);
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
        *document, cursiv::kNoCancel));
    CHECK(full == expected);
    const lsp::Range ranges[] = {
        {{0, 4}, {0, 6}}, {{1, 1}, {3, 4}}, {{4, 11}, {4, 12}}};
    const std::vector<Decoded> cuts[] = {
        {expected[0]}, {expected[3], expected[4]}, {expected[9]}};
    for (std::size_t i = 0; i < std::size(ranges); ++i)
      CHECK(decode(world.ws->semanticTokenProvider->semanticHighlightRange(
                *document, ranges[i], cursiv::kNoCancel)) == cuts[i]);
  }
}

// A document that never parsed is answered with an empty array, which the
// client reads as "no colouring" -- never with an error.
void a_document_with_no_tree_is_coloured_with_nothing() {
  const World world;
  cursiv::Document* const document = world.built("file:///w/empty.ws", "");
  const lsp::SemanticTokens tokens =
      world.ws->semanticTokenProvider->semanticHighlight(*document,
                                                         cursiv::kNoCancel);
  CHECK(tokens.data.empty());
}

}  // namespace

void run() {
  every_expected_category_is_coloured();
  the_tokens_are_ordered_and_never_overlap();
  every_index_names_a_row_of_the_legend();
  a_range_is_the_full_answer_cut_to_it();
  unicode_and_multiline_tokens_keep_their_positions();
  a_document_with_no_tree_is_coloured_with_nothing();
}

}  // namespace semantic_token_provider

// The signature popup is asked with the request whole: a provider reads the
// context that tells a retrigger from a first request, and its trigger
// characters reach the handshake.
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

// What `textDocument/codeAction` answers over the validator's stamped rows:
// which requests the built-in quick fix answers, which rows it refuses, and
// that a language's own hook runs whatever kind the client asked for.
namespace code_action_provider {

namespace {

using World = support::LspWorkspace;

// A missing `:` the parser repairs, which stamps one quick-fix row.
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

// No `only`, or one naming quick fixes: the stamped row is offered.
void the_built_in_fix_answers_a_request_that_accepts_quick_fixes() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", kBroken);
  const cursiv::DefaultCodeActionProvider provider;

  lsp::CodeActionParams params = over(*document);
  CHECK(has_kind(provider.getCodeActions(*document, params, cursiv::kNoCancel),
                 lsp::CodeActionKind::QuickFix));

  params.context.only = lsp::Array<lsp::CodeActionKindEnum>{
      lsp::CodeActionKindEnum(lsp::CodeActionKind::QuickFix)};
  CHECK(has_kind(provider.getCodeActions(*document, params, cursiv::kNoCancel),
                 lsp::CodeActionKind::QuickFix));
}

// An `only` naming another kind withholds the built-in fix -- and still asks
// the language, which decides its own kinds.
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

  // ...and with nothing asked, both stand, the runtime's first.
  params.context.only = std::nullopt;
  const std::vector<lsp::CodeAction> both =
      provider.getCodeActions(*document, params, cursiv::kNoCancel);
  CHECK(both.size() == 2);
  CHECK(both.front().kind == lsp::CodeActionKind::QuickFix);
  CHECK(both.back().kind == lsp::CodeActionKind::Source);
}

// A row stamped against another revision of the text names offsets that may
// no longer hold: it is dropped rather than applied. A version stamped on the
// same bytes is not another revision.
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

// A misspelt reference's neighbour reaches the client as a quick fix that
// replaces the name.
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

// Counts the walks over a document's failed references; forwards the rest.
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
  void forceAll(const cursiv::CancellationToken& token) override {
    inner->forceAll(token);
  }
  void forceAt(const cursiv::Cst& cst, cursiv::TextOffset offset,
               const cursiv::CancellationToken& token) override {
    inner->forceAt(cst, offset, token);
  }
  void visitResolved(const cursiv::Cst& cst,
                     cursiv::ResolvedReferenceVisitor visitor) const override {
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
};

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

// The request's range selects the findings: one that touches nothing stamped
// is answered nothing.
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
  bool cancelled = false;
  try {
    (void)provider.getCodeActions(document, params, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
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
  data[std::string(cursiv::kDefaultCodeActionsKey)] = cursiv::JsonValue::Array{
      cursiv::make_action_row(cursiv::kReplaceEdit, "replace", {0, 1}, "b", 0)};
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
  echo.data = cursiv::to_lsp_any(*document.diagnostics.front().data);
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
  duplicate.append(std::string(cursiv::kDefaultCodeActionsKey),
                   duplicate.get(cursiv::kDefaultCodeActionsKey));
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

// The virtual-document door: an announced scheme is matched exactly against
// the parsed URI, and a document the store holds is served back by URI.
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
