// Documentation comments: the comment a provider binds to a node, the parse of
// a doc comment, and the markdown the documentation provider renders.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "comments/generated/Comments/Module.hpp"
#include "core/documentation/CommentsGrammar.hpp"
#include "cursiv/core/documentation/DefaultCommentProvider.hpp"
#include "cursiv/core/documentation/DefaultDocumentationProvider.hpp"
#include "cursiv/core/documentation/DocComment.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/test/Check.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/core/ast.hpp"

// Pins the CommentProvider contract: the image is returned as written, only
// the rule the provider is bound to answers, the root reads its whole leading
// trivia, and a node without a document or a CST answers nothing.
//
// The grammar is the test's own: this lane exercises core and links core.
namespace comment_provider {

namespace {

namespace ast = tcomment::ast;

// A parser in the services, because the provider asks the grammar which rule
// wrote a hidden record.
cursiv::CoreServices& services() {
  static cursiv::SharedCoreServices shared;
  static cursiv::CoreServices language(shared);
  static const bool ready = [] {
    language.languageMetaData = {"t", {".t"}, {}};
    language.parser = tcomment::generated::Comments::create_parser();
    return true;
  }();
  (void)ready;
  return language;
}

const cursiv::CommentProvider& provider() {
  static const cursiv::DefaultCommentProvider instance(services());
  return instance;
}

// A real document, because that is what a caller has: the tree the node's id
// indexes lives in it, and the AST borrows every identifier it holds.
struct Parsed {
  cursiv::Document document;
  cursiv::ParseResult& result;

  explicit Parsed(std::string text)
      : document(cursiv::TextDocument::create("file:///t.t", "t", 1,
                                              std::move(text))),
        result(document.parseResult) {
    result = services().parser->parse(document.textDocument().getText());
    if (result.value != nullptr) result.value->parent = &document;
  }

  [[nodiscard]] const cursiv::AstNode* element(std::size_t at) const {
    const auto* root = static_cast<const ast::Doc*>(result.value);
    return root->elements.size() > at ? root->elements[at] : nullptr;
  }
};

void a_doc_comment_answers_as_written() {
  const Parsed p("/** The person. */\nentity Person { }\n");
  const cursiv::AstNode* const entity = p.element(0);
  CHECK(entity != nullptr);

  const std::optional<std::string> comment =
      provider().getComment(*entity, &p.document);
  CHECK(comment.has_value());
  // The source image, delimiters included: what lets a reader downstream
  // still recognize a documentation comment.
  CHECK(*comment == "/** The person. */");
}

void the_node_alone_finds_its_document() {
  const Parsed p("/** The person. */\nentity Person { }\n");
  const std::optional<std::string> comment =
      provider().getComment(*p.element(0));
  CHECK(comment.has_value() && *comment == "/** The person. */");
}

// A plain block is the same ML_COMMENT rule: the provider answers it as
// written and the consumer decides what is documentation.
void a_plain_block_answers_as_written() {
  const Parsed p("/* plain */\nentity Person { }\n");
  const std::optional<std::string> comment =
      provider().getComment(*p.element(0), &p.document);
  CHECK(comment.has_value() && *comment == "/* plain */");
}

// A line comment is another rule: never an answer.
void a_line_comment_does_not_answer() {
  const Parsed p("// nope\nentity Person { }\n");
  CHECK(!provider().getComment(*p.element(0), &p.document));
}

// The rule names are the binding: a provider bound to another name sees
// nothing, one bound to the line rule reads line comments, and one bound to
// both reads whichever stands nearest.
void the_rule_name_decides() {
  const Parsed p("/** Doc. */\n// line\nentity Person { }\n");
  const cursiv::DefaultCommentProvider elsewhere(services(), {"BLOCK_COMMENT"});
  CHECK(!elsewhere.getComment(*p.element(0), &p.document));
  const cursiv::DefaultCommentProvider lines(services(), {"SL_COMMENT"});
  const std::optional<std::string> line =
      lines.getComment(*p.element(0), &p.document);
  CHECK(line.has_value() && *line == "// line");
  const cursiv::DefaultCommentProvider both(services(),
                                            {"ML_COMMENT", "SL_COMMENT"});
  const std::optional<std::string> nearest =
      both.getComment(*p.element(0), &p.document);
  CHECK(nearest.has_value() && *nearest == "// line");
}

void each_declaration_finds_its_own() {
  const Parsed p("/** A. */\ndatatype A\n/** B. */\nentity B { }\n");
  const std::optional<std::string> first =
      provider().getComment(*p.element(0), &p.document);
  const std::optional<std::string> second =
      provider().getComment(*p.element(1), &p.document);
  CHECK(first.has_value() && *first == "/** A. */");
  CHECK(second.has_value() && *second == "/** B. */");
}

void only_the_immediate_neighbour_answers() {
  // The stray comment two declarations up never leaks in.
  const Parsed p("/** Doc. */\ndatatype Name\nentity Person { }\n");
  CHECK(!provider().getComment(*p.element(1), &p.document));
}

// Two comments stacked: the nearest one wins.
void the_nearest_comment_wins() {
  const Parsed p("/** First. */\n/** Second. */\nentity A { }\n");
  const std::optional<std::string> comment =
      provider().getComment(*p.element(0), &p.document);
  CHECK(comment.has_value() && *comment == "/** Second. */");
}

// A comment trailing the previous declaration's line still belongs to what
// follows it, never to what it trails.
void a_trailing_comment_belongs_to_the_next() {
  const Parsed p("entity A { } /** Trail. */\nentity B { }\n");
  CHECK(!provider().getComment(*p.element(0), &p.document));
  const std::optional<std::string> comment =
      provider().getComment(*p.element(1), &p.document);
  CHECK(comment.has_value() && *comment == "/** Trail. */");
}

// The root's neighbourhood is the whole leading trivia: a line comment in
// between does not hide the head comment from it, but does from the first
// declaration, which owns exactly one step.
void the_root_reads_the_leading_trivia() {
  const Parsed p("/** Head. */\n// noise\nentity Person { }\n");
  const std::optional<std::string> head =
      provider().getComment(*p.result.value, &p.document);
  CHECK(head.has_value() && *head == "/** Head. */");
  CHECK(!provider().getComment(*p.element(0), &p.document));
}

// Only the leading trivia is the root's: a comment inside its body belongs to
// what follows it, never to the root.
void the_root_ignores_a_comment_in_its_body() {
  const Parsed p("entity A { }\n/** Mid. */\nentity B { }\n");
  CHECK(!provider().getComment(*p.result.value, &p.document));
  const std::optional<std::string> mid =
      provider().getComment(*p.element(1), &p.document);
  CHECK(mid.has_value() && *mid == "/** Mid. */");
}

// A nested node reads its own predecessor, mid-line included.
void a_nested_node_reads_its_predecessor() {
  const Parsed p("entity Person { /** The age. */ age : String }\n");
  const auto* entity = static_cast<const ast::Entity*>(p.element(0));
  CHECK(entity != nullptr && !entity->features.empty());
  const std::optional<std::string> doc =
      provider().getComment(*entity->features[0], &p.document);
  CHECK(doc.has_value() && *doc == "/** The age. */");
}

// A root with no visible content owns nothing, however commented the trivia.
void an_empty_root_owns_nothing() {
  const Parsed p("/** only */\n");
  CHECK(p.result.value != nullptr);
  CHECK(!provider().getComment(*p.result.value, &p.document));
}

// Adjacency is not a gap: a comment touching the declaration still answers.
void an_adjacent_comment_answers() {
  const Parsed p("/** Doc. */entity Person { }\n");
  const std::optional<std::string> comment =
      provider().getComment(*p.element(0), &p.document);
  CHECK(comment.has_value() && *comment == "/** Doc. */");
}

// The degenerate marker: the image is still returned as written, so the
// answer is never the empty string.
void a_degenerate_marker_is_still_the_image() {
  const Parsed p("/**/\nentity Person { }\n");
  const std::optional<std::string> comment =
      provider().getComment(*p.element(0), &p.document);
  CHECK(comment.has_value() && *comment == "/**/");
}

void a_homeless_node_answers_nothing() {
  const Parsed p("entity Person { }\n");
  ast::Entity made;
  made.type = &cursiv::ast_type_tag<ast::Entity>;
  // No document to climb to.
  CHECK(!provider().getComment(made));
  // Homed, but no CST record.
  made.parent = p.result.value;
  CHECK(made.cstNodeId == cursiv::kNoNode);
  CHECK(!provider().getComment(made));
}

}  // namespace

void run() {
  a_doc_comment_answers_as_written();
  the_node_alone_finds_its_document();
  a_plain_block_answers_as_written();
  a_line_comment_does_not_answer();
  the_rule_name_decides();
  each_declaration_finds_its_own();
  only_the_immediate_neighbour_answers();
  the_nearest_comment_wins();
  a_trailing_comment_belongs_to_the_next();
  the_root_reads_the_leading_trivia();
  the_root_ignores_a_comment_in_its_body();
  a_nested_node_reads_its_predecessor();
  an_empty_root_owns_nothing();
  an_adjacent_comment_answers();
  a_degenerate_marker_is_still_the_image();
  a_homeless_node_answers_nothing();
}

}  // namespace comment_provider

// Pins the documentation-comment model, function by function as the audit
// walks it. Pure strings: nothing here needs a service, a document or a CST.
namespace doc_comment {

namespace {

[[nodiscard]] bool is_doc(std::string_view text) {
  return cursiv::is_doc_comment(text);
}

// The delimiters decide: first line starts with `start`, last line ends with
// `end`, whitespace tolerated on both sides, middle lines never examined.
void the_delimiters_decide() {
  CHECK(is_doc("/** x */"));
  CHECK(is_doc("/**\n * x\n */"));
  CHECK(is_doc(" /** x */"));
  CHECK(is_doc("/** A \n B */"));
  CHECK(!is_doc("/* x */"));
  CHECK(!is_doc(""));
  CHECK(!is_doc("/** x"));
  CHECK(!is_doc("x */"));
  CHECK(!is_doc("/**"));
  // A captured final newline puts the end marker on the wrong line.
  CHECK(!is_doc("/** x */\n"));
}

// Overlapping markers are legal: nothing forbids the end starting inside the
// start on a single line.
void overlapping_markers_are_legal() { CHECK(is_doc("/**/")); }

// The whitespace class is spelled bytes: ASCII blanks are tolerated around
// the markers, a Unicode blank is not one, and CRLF splits lines.
void whitespace_is_bytes() {
  CHECK(is_doc("/** x */  "));
  CHECK(is_doc("/**\r\n * x\r\n */"));
  CHECK(!is_doc("  "));
  // U+00A0, a blank to a Unicode reader, is not one here -- and the strongest
  // manifestation is rendered text: a trailing NBSP stays in the line.
  CHECK(!is_doc("\xC2\xA0/** x */"));
  CHECK(cursiv::parse_doc_comment("/** A\xC2\xA0 */").toString() ==
        "A\xC2\xA0");
}

// The markers are configurable, field by field: an omitted field keeps its
// default rather than disabling the test.
void the_markers_are_configurable() {
  const cursiv::DocCommentParseOptions xml{.start = "<!--", .end = "-->"};
  CHECK(cursiv::is_doc_comment("<!-- A \n B -->", xml));
  CHECK(!cursiv::is_doc_comment("/** x */", xml));

  const cursiv::DocCommentParseOptions partial{.start = "/**"};
  CHECK(cursiv::is_doc_comment("/** x */", partial));
}

// An empty marker disables its half of the test.
void an_empty_marker_disables_its_half() {
  const cursiv::DocCommentParseOptions noEnd{.end = ""};
  CHECK(cursiv::is_doc_comment("/** x", noEnd));
  const cursiv::DocCommentParseOptions noStart{.start = ""};
  CHECK(cursiv::is_doc_comment("x */", noStart));
}

// The first of the name wins, all of them come in document order, and only
// top-level block tags answer: an inline tag lives inside its paragraph.
void the_tag_queries_read_the_top_level() {
  const cursiv::DocComment comment = cursiv::parse_doc_comment(
      "/**\n * @param a\n * @param b {@link Value}\n * @returns c\n */");
  CHECK(comment.getTags("param").size() == 2);
  const cursiv::DocCommentNode* const first = comment.getTag("param");
  CHECK(first != nullptr && first == comment.getTags("param")[0]);
  const cursiv::DocCommentNode* const returns = comment.getTag("returns");
  CHECK(returns != nullptr && returns->name == "returns");
  CHECK(comment.getTag("missing") == nullptr);
  // The inline {@link} is a child, never an element.
  CHECK(comment.getTag("link") == nullptr);
  // Exact match: case sensitive, and the name carries no `@`.
  CHECK(comment.getTag("Param") == nullptr);
  CHECK(comment.getTag("@param") == nullptr);
}

// Only paragraphs and block tags live at the top level -- the invariant the
// tag queries rely on without re-testing `inlineTag`.
void the_top_level_holds_paragraphs_and_block_tags() {
  const cursiv::DocComment comment = cursiv::parse_doc_comment(
      "/** {@link X}\n * @param a\n * text\n * \n * more\n */");
  CHECK(!comment.elements.empty());
  for (const cursiv::DocCommentNode& element : comment.elements) {
    CHECK(element.kind == cursiv::DocCommentNode::Kind::Paragraph ||
          (element.kind == cursiv::DocCommentNode::Kind::Tag &&
           !element.inlineTag));
  }
}

// Children hold only lines and inline tags: a paragraph never nests, a block
// tag always ends its paragraph, and an inline tag holds at most one line.
void children_hold_lines_and_inline_tags() {
  const cursiv::DocComment comment = cursiv::parse_doc_comment(
      "/** pre {@link A B} post\n * @param x the @deprecated thing\n */");
  CHECK(comment.elements.size() == 2);
  for (const cursiv::DocCommentNode& element : comment.elements)
    for (const cursiv::DocCommentNode& child : element.children) {
      CHECK(child.kind != cursiv::DocCommentNode::Kind::Paragraph);
      CHECK(child.kind != cursiv::DocCommentNode::Kind::Tag || child.inlineTag);
      if (child.kind == cursiv::DocCommentNode::Kind::Tag) {
        CHECK(child.children.size() <= 1);
        if (!child.children.empty())
          CHECK(child.children[0].kind == cursiv::DocCommentNode::Kind::Line);
      }
    }
}

// An empty {@link} still parses: an inline tag holding an empty line.
void an_empty_inline_link_still_parses() {
  const cursiv::DocComment comment =
      cursiv::parse_doc_comment("/** pre {@link} post */");
  const std::string text = comment.toString();
  CHECK(text.find("pre") != std::string::npos);
  CHECK(text.find("post") != std::string::npos);
}

void unfinished_inline_tags_leave_the_following_line_readable() {
  std::string unfinished;
  for (unsigned i = 0; i < 4096; ++i) unfinished += "{@link x ";
  unfinished.pop_back();
  const auto comment = cursiv::parse_doc_comment(
      "/** " + unfinished + "\n * {@link https://example.org shown}\n */");
  const std::string rendered = comment.toMarkdown();
  CHECK(rendered.starts_with(unfinished));
  CHECK(rendered.ends_with("[shown](https://example.org)"));
  CHECK(cursiv::parse_doc_comment("/** {@link x\r {@link https://a label} */")
            .toMarkdown().ends_with("[label](https://a)"));
  // A lone CR ends the line as LF does, so the tag it splits stays text.
  for (const std::string_view eol : {"\n", "\r"})
    CHECK(cursiv::parse_doc_comment("/** {@bad x {@link " + std::string(eol) +
                                    "https://a label} */")
              .toMarkdown() == "{@bad x {@link\nhttps://a label}");
}

// LF, CRLF and a lone CR each end a line, as `TextDocument` counts them: the
// text and every range read the same under the three.
void every_line_ending_splits_lines() {
  const auto parse = [](std::string_view eol) {
    std::string text;
    for (const std::string_view line :
         {"/**", " * first", " *", " * @see second", " */"}) {
      if (!text.empty()) text += eol;
      text += line;
    }
    return std::pair{
        cursiv::parse_doc_comment(text),
        cursiv::TextDocument::create("file:///doc.ws", "ws", 1, text)
            .positionAt(static_cast<cursiv::TextOffset>(text.find("first")))};
  };
  const auto same = [](const cursiv::DocCommentRange& a,
                       const cursiv::DocCommentRange& b) {
    return a.start.line == b.start.line &&
           a.start.character == b.start.character && a.end.line == b.end.line &&
           a.end.character == b.end.character;
  };
  const auto [lf, at] = parse("\n");
  CHECK(lf.toMarkdown() == "first\n\n*@see* — second");
  CHECK(lf.elements.size() == 2);
  CHECK(lf.range.start.line == at.line && at.line == 1);
  for (const std::string_view eol : {"\r\n", "\r"}) {
    const auto [other, otherAt] = parse(eol);
    CHECK(other.toMarkdown() == lf.toMarkdown());
    CHECK(otherAt.line == at.line && otherAt.character == at.character);
    CHECK(same(other.range, lf.range));
    CHECK(other.elements.size() == lf.elements.size());
    for (std::size_t i = 0; i < other.elements.size() && i < lf.elements.size();
         ++i)
      CHECK(same(other.elements[i].range, lf.elements[i].range));
  }
}

// Elements join with a blank line, the second paragraph keeps its leading
// space, and the whole render is trimmed.
void paragraphs_join_with_a_blank_line() {
  const cursiv::DocComment comment =
      cursiv::parse_doc_comment("/**\n * A\n * B\n * \n * C\n */");
  CHECK(comment.toString() == "A\n B\n\n C");
  CHECK(comment.toMarkdown() == "A\n B\n\n C");
}

// Blank lines traverse the render untouched: newlines come out as written.
void empty_lines_are_preserved() {
  const cursiv::DocComment comment =
      cursiv::parse_doc_comment("/** ```\nA\n\n\nB\nC\n\n``` */");
  CHECK(comment.toMarkdown() == "```\nA\n\n\nB\nC\n\n```");
}

// The line marker is decoration: present, absent, or custom, the text reads
// the same.
void the_render_survives_the_markers() {
  CHECK(cursiv::parse_doc_comment("/** A \n * B \n */").toString() == "A\n B");
  CHECK(cursiv::parse_doc_comment("/** A \n B */").toString() == "A\n B");
  const cursiv::DocCommentParseOptions xml{.start = "<!--", .end = "-->"};
  CHECK(cursiv::parse_doc_comment("<!-- A \n B -->", {}, xml).toString() ==
        "A\n B");
}

// The range spans the elements, not the delimiters; a block tag anchors on
// the `@` itself, never on the blank before it; an empty comment degenerates
// to its start.
void the_range_spans_the_elements() {
  const cursiv::DocComment tagged =
      cursiv::parse_doc_comment("/** A \n   *  @B  \n* @C D \n*/");
  CHECK(tagged.elements.size() == 3);
  CHECK(tagged.elements[0].kind == cursiv::DocCommentNode::Kind::Paragraph);
  CHECK(tagged.elements[0].children.size() == 1);
  CHECK(tagged.elements[0].children[0].text == " A");
  CHECK(tagged.elements[1].name == "B" && !tagged.elements[1].inlineTag);
  CHECK(tagged.elements[1].children.empty());
  CHECK(tagged.elements[1].range.start.line == 1);
  CHECK(tagged.elements[1].range.start.character == 6);
  CHECK(tagged.elements[1].range.end.character == 8);
  CHECK(tagged.elements[2].name == "C");
  CHECK(tagged.elements[2].range.start.line == 2);
  CHECK(tagged.elements[2].range.start.character == 2);
  CHECK(tagged.elements[2].range.end.character == 6);
  CHECK(tagged.elements[2].children.size() == 1);
  CHECK(tagged.elements[2].children[0].text == "D");
  CHECK(tagged.range.start.line == 0 && tagged.range.start.character == 3);
  CHECK(tagged.range.end.line == 2 && tagged.range.end.character == 6);

  const cursiv::DocComment empty = cursiv::parse_doc_comment("/** */");
  CHECK(empty.range.start.line == empty.range.end.line &&
        empty.range.start.character == empty.range.end.character);
}

// Every line carries its own span: start just past the line marker (a
// paragraph keeps the leading blank inside), end at the last non-blank; an
// appended empty line is a zero-width child the parent's range still covers.
void the_multiline_text_carries_line_by_line_ranges() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/** A \n   *   B  \n*C \n\n D*/");
  CHECK(c.elements.size() == 2);
  CHECK(c.range.start.line == 0 && c.range.start.character == 3);
  CHECK(c.range.end.line == 4 && c.range.end.character == 2);

  const cursiv::DocCommentNode& first = c.elements[0];
  CHECK(first.children.size() == 4);
  CHECK(first.range.start.line == 0 && first.range.start.character == 3);
  CHECK(first.range.end.line == 3 && first.range.end.character == 0);
  CHECK(first.children[0].text == " A");
  CHECK(first.children[0].range.start.character == 3);
  CHECK(first.children[0].range.end.character == 5);
  CHECK(first.children[1].text == "   B");
  CHECK(first.children[1].range.start.line == 1);
  CHECK(first.children[1].range.start.character == 4);
  CHECK(first.children[1].range.end.character == 8);
  CHECK(first.children[2].text == "C");
  CHECK(first.children[2].range.start.character == 1);
  CHECK(first.children[3].text == "");
  CHECK(first.children[3].range.start.line == 3);
  CHECK(first.children[3].range.start.character == 0);
  CHECK(first.children[3].range.end.character == 0);

  CHECK(c.elements[1].children.size() == 1);
  CHECK(c.elements[1].children[0].text == " D");
}

void the_line_children_of_a_paragraph_are_individually_ranged() {
  const cursiv::DocComment c = cursiv::parse_doc_comment("/** A \n * B \n */");
  CHECK(c.elements.size() == 1);
  CHECK(c.elements[0].children.size() == 2);
  CHECK(c.elements[0].children[0].text == " A");
  CHECK(c.elements[0].children[0].range.start.character == 3);
  CHECK(c.elements[0].children[1].text == " B");
  CHECK(c.elements[0].children[1].range.start.line == 1);
  CHECK(c.elements[0].children[1].range.start.character == 2);
  CHECK(c.elements[0].range.end.line == 1);
  CHECK(c.elements[0].range.end.character == 4);
}

// The start position offsets the first line by its character, later lines by
// nothing.
void a_start_position_offsets_every_range() {
  const cursiv::DocComment c = cursiv::parse_doc_comment("/** A */", {4, 7});
  CHECK(c.elements.size() == 1);
  CHECK(c.elements[0].range.start.line == 4);
  CHECK(c.elements[0].range.start.character == 10);
  CHECK(c.elements[0].range.end.line == 4);
  CHECK(c.elements[0].range.end.character == 12);
}

void the_inline_tag_splits_into_line_tag_line() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/** A {@link B} C */");
  CHECK(c.elements.size() == 1);
  const cursiv::DocCommentNode& para = c.elements[0];
  CHECK(para.children.size() == 3);
  CHECK(para.children[0].text == " A ");
  CHECK(para.children[1].kind == cursiv::DocCommentNode::Kind::Tag);
  CHECK(para.children[1].name == "link" && para.children[1].inlineTag);
  CHECK(para.children[1].children.size() == 1);
  CHECK(para.children[1].children[0].text == "B");
  CHECK(para.children[2].text == " C");
}

// A node's range covers its children -- a tag whose last inline is a link
// reaches the link's content, and a leading inline belongs to the tag.
void a_range_covers_its_children() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/**\n * @param b {@link Value}\n */");
  CHECK(c.elements.size() == 1);
  const cursiv::DocCommentNode& tag = c.elements[0];
  CHECK(tag.range.start.character == 3 && tag.range.end.character == 24);
  CHECK(tag.children.size() == 2);
  CHECK(tag.children[1].range.end.character == 24);

  const cursiv::DocComment see =
      cursiv::parse_doc_comment("/** @see {@link A} */");
  CHECK(see.elements.size() == 1);
  CHECK(see.elements[0].name == "see");
  CHECK(see.elements[0].children.size() == 1);
  CHECK(see.elements[0].children[0].inlineTag);
  CHECK(see.toMarkdown() == "*@see* — A");
}

// A tag name is ASCII: a name that cannot open stays an empty-named tag with
// its word as content, one that opens cuts at the first non-ASCII byte, and a
// non-ASCII inline marker is plain text.
void a_tag_name_is_ascii() {
  const cursiv::DocComment accent =
      cursiv::parse_doc_comment("/** @\xC3\xA9tat oui */");
  CHECK(accent.getTag("\xC3\xA9tat") == nullptr);
  CHECK(accent.elements.size() == 1);
  CHECK(accent.elements[0].kind == cursiv::DocCommentNode::Kind::Tag);
  CHECK(accent.elements[0].name.empty());
  CHECK(accent.elements[0].children[0].text == "\xC3\xA9tat oui");

  const cursiv::DocComment cut = cursiv::parse_doc_comment(
      "/** @d\xC3\xA9pr\xC3\xA9"
      "ci\xC3\xA9 oui */");
  CHECK(cut.elements[0].name == "d");

  const cursiv::DocComment inlineAccent =
      cursiv::parse_doc_comment("/** pre {@\xC3\xA9tat X} post */");
  CHECK(inlineAccent.elements.size() == 1);
  CHECK(inlineAccent.elements[0].children.size() == 1);
  CHECK(inlineAccent.elements[0].children[0].kind ==
        cursiv::DocCommentNode::Kind::Line);
}

// An empty `line` marker disables its half at the parse too: the line keeps
// its marker as text. And an empty `start` marker leaves the `line` marker to
// apply on line zero.
void an_empty_line_marker_keeps_the_line() {
  cursiv::DocCommentParseOptions noLine;
  noLine.line = "";
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/**\n * a\n */", {}, noLine);
  CHECK(c.toString() == "* a");

  cursiv::DocCommentParseOptions noStart;
  noStart.start = "";
  const cursiv::DocComment d =
      cursiv::parse_doc_comment("* a\n * b\n */", {}, noStart);
  CHECK(d.toString() == "a\n b");
}

void links_render_to_markdown() {
  CHECK(cursiv::parse_doc_comment("/** {@link https://example.org/} */")
            .toMarkdown() == "[https://example.org/](https://example.org/)");
  CHECK(cursiv::parse_doc_comment("/** {@linkcode https://example.org/} */")
            .toMarkdown() == "[`https://example.org/`](https://example.org/)");
  cursiv::DocCommentRenderOptions code;
  code.link = cursiv::DocCommentRenderOptions::LinkStyle::Code;
  CHECK(cursiv::parse_doc_comment("/** {@link https://example.org/} */")
            .toMarkdown(code) ==
        "[`https://example.org/`](https://example.org/)");
  CHECK(cursiv::parse_doc_comment("/** {@link Value} */").toMarkdown() ==
        "Value");
  CHECK(cursiv::parse_doc_comment("/** {@link https://x/|shown} */")
            .toMarkdown() == "[shown](https://x/)");
  // A bare `://` carries no scheme: text, never a link.
  CHECK(cursiv::parse_doc_comment("/** {@link ://x} */").toMarkdown() ==
        "://x");
  CHECK(
      cursiv::parse_doc_comment("/** {@link x:////a label} */").toMarkdown() ==
      "x:////a");
  CHECK(cursiv::parse_doc_comment("/** {@link x:%2f%2fa label} */")
            .toMarkdown() == "x:%2f%2fa");
  CHECK(
      cursiv::parse_doc_comment("/** {@link x:/%2Fa label} */").toMarkdown() ==
      "x:/%2Fa");
  CHECK(cursiv::parse_doc_comment("/** {@link x://host//a label} */")
            .toMarkdown() == "[label](x://host//a)");
  CHECK(cursiv::parse_doc_comment("/** {@link file:relative label} */")
            .toMarkdown() == "[label](file:relative)");
}

void inline_link_trims_and_falls_back_on_empty_display() {
  CHECK(cursiv::parse_doc_comment("/** {@link Value } */").toMarkdown() ==
        "Value");
  // No scheme: the target is written, whatever the display said.
  CHECK(cursiv::parse_doc_comment("/** {@link a|b } */").toMarkdown() == "a");
  CHECK(cursiv::parse_doc_comment("/** {@link\tA\tB} */").toMarkdown() == "A");
  CHECK(cursiv::parse_doc_comment("/** {@link -://x} */").toMarkdown() ==
        "-://x");
  CHECK(cursiv::parse_doc_comment("/** {@link mailto:x y} */").toMarkdown() ==
        "[y](mailto:x)");
}

void a_custom_link_renderer_overrides_the_default() {
  cursiv::DocCommentRenderOptions options;
  options.renderLink = [](std::string_view target, std::string_view display) {
    return std::optional<std::string>("[[" + std::string(target) + "|" +
                                      std::string(display) + "]]");
  };
  CHECK(cursiv::parse_doc_comment("/** {@link target shown} */")
            .toMarkdown(options) == "[[target|shown]]");
}

void single_line_tags_render_with_an_em_dash() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/** @deprecated Since 1.0 */");
  CHECK(c.toMarkdown() == "*@deprecated* — Since 1.0");
  CHECK(c.toString() == "@deprecated Since 1.0");
}

void multi_line_tags_render_each_line_on_its_own_line() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/** @deprecated Since\n1.0*/");
  CHECK(c.toMarkdown() == "*@deprecated*\nSince\n1.0");
  CHECK(c.toString() == "@deprecated\nSince\n1.0");
}

void the_tag_style_changes_only_the_marker() {
  const cursiv::DocComment c =
      cursiv::parse_doc_comment("/** @deprecated Since 1.0 */");
  cursiv::DocCommentRenderOptions bold;
  bold.tag = cursiv::DocCommentRenderOptions::TagStyle::Bold;
  CHECK(c.toMarkdown(bold) == "**@deprecated** — Since 1.0");
  cursiv::DocCommentRenderOptions boldItalic;
  boldItalic.tag = cursiv::DocCommentRenderOptions::TagStyle::BoldItalic;
  CHECK(c.toMarkdown(boldItalic) == "***@deprecated*** — Since 1.0");
  cursiv::DocCommentRenderOptions plain;
  plain.tag = cursiv::DocCommentRenderOptions::TagStyle::Plain;
  CHECK(c.toMarkdown(plain) == "@deprecated — Since 1.0");
}

void a_custom_tag_renderer_overrides_every_tag() {
  cursiv::DocCommentRenderOptions options;
  options.renderTag = [](std::string_view name, std::string_view content,
                         bool) {
    return std::optional<std::string>("<" + std::string(name) + ":" +
                                      std::string(content) + ">");
  };
  CHECK(cursiv::parse_doc_comment("/** @deprecated soon */")
            .toMarkdown(options) == "<deprecated:soon>");
  CHECK(cursiv::parse_doc_comment("/** {@link target shown} */")
            .toMarkdown(options) == "<link:target shown>");
}

}  // namespace

void run() {
  the_delimiters_decide();
  overlapping_markers_are_legal();
  whitespace_is_bytes();
  the_markers_are_configurable();
  an_empty_marker_disables_its_half();
  the_tag_queries_read_the_top_level();
  the_top_level_holds_paragraphs_and_block_tags();
  children_hold_lines_and_inline_tags();
  an_empty_inline_link_still_parses();
  unfinished_inline_tags_leave_the_following_line_readable();
  every_line_ending_splits_lines();
  paragraphs_join_with_a_blank_line();
  empty_lines_are_preserved();
  the_render_survives_the_markers();
  the_range_spans_the_elements();
  the_multiline_text_carries_line_by_line_ranges();
  the_line_children_of_a_paragraph_are_individually_ranged();
  a_start_position_offsets_every_range();
  the_inline_tag_splits_into_line_tag_line();
  a_range_covers_its_children();
  a_tag_name_is_ascii();
  an_empty_line_marker_keeps_the_line();
  links_render_to_markdown();
  inline_link_trims_and_falls_back_on_empty_display();
  a_custom_link_renderer_overrides_the_default();
  single_line_tags_render_with_an_em_dash();
  multi_line_tags_render_each_line_on_its_own_line();
  the_tag_style_changes_only_the_marker();
  a_custom_tag_renderer_overrides_every_tag();
}

}  // namespace doc_comment

// Pins the documentation a hover shows: the author's comment rendered to
// markdown, {@link} targets resolved to their declaration's name site, and
// the grammar's own prose on the element arm.
namespace documentation_provider {

namespace {

struct World : support::CoreWorkspace {
  // A document taken to `ComputedScopes` and indexed, which is what a link
  // resolution reads.
  std::shared_ptr<cursiv::Document> built(const std::string& uri,
                                          std::string text) const {
    auto document = shared.documentFactory->fromString(std::move(text), uri);
    shared.documents->addDocument(document);
    shared.indexManager->updateContent(*document);
    document->localSymbols =
        ws->scopeComputation->collectLocalSymbols(*document);
    document->state = cursiv::DocumentState::ComputedScopes;
    return document;
  }

  [[nodiscard]] const cursiv::AstNode* element(const cursiv::Document& document,
                                               std::size_t at) const {
    const auto* root =
        static_cast<const ws::ast::Workspace*>(document.parseResult.value);
    return root->elements.size() > at ? root->elements[at] : nullptr;
  }
};

void a_doc_comment_renders_to_markdown() {
  const World w;
  const auto doc =
      w.built("file:///w/a.ws", "/** The person. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "The person.");
}

// A comment that is not a documentation comment is nobody's documentation:
// never handed out raw.
void a_plain_block_is_nobodys_documentation() {
  const World w;
  const auto doc =
      w.built("file:///w/a.ws", "/* plain */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  CHECK(!docs.getDocumentation(*w.element(*doc, 0), doc.get()));
}

void a_link_resolves_to_the_declaration() {
  const World w;
  const auto doc = w.built(
      "file:///w/a.ws",
      "/** See {@link String}. */\nentity Person { }\ndatatype String\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "See [String](file:///w/a.ws#L3,10).");
}

// The display text survives an unresolved target: the author chose it.
// What a link named and nothing answered for: the target, as jsdoc.ts writes
// it, so a reader sees which name was meant.
void an_unresolved_link_is_its_target() {
  const World w;
  const auto doc =
      w.built("file:///w/a.ws",
              "/** See {@link Nope missing}. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "See Nope.");
}

void a_link_crosses_documents() {
  const World w;
  const auto lib = w.built("file:///w/lib.ws", "datatype Remote\n");
  const auto doc = w.built("file:///w/b.ws",
                           "/** See {@link Remote}. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "See [Remote](file:///w/lib.ws#L1,10).");
  (void)lib;
}

void a_link_replaces_the_targets_fragment() {
  const World w;
  const auto target =
      w.built("mem:///lib.ws?view=source#old", "datatype Remote\n");
  const auto doc = w.built("mem:///main.ws#current",
                           "/** See {@link Remote}. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const auto text = docs.getDocumentation(*w.element(*doc, 0));
  CHECK(text && *text == "See [Remote](mem:///lib.ws?view=source#L1,10).");
  (void)target;
}

void a_link_uses_the_target_documents_language_id() {
  const World w;
  const auto target = w.shared.documentFactory->fromTextDocument(
      cursiv::TextDocument::create("file:///no-extension", "workspace", 1,
                                   "datatype Remote\n"));
  w.shared.documents->addDocument(target);
  w.shared.indexManager->updateContent(*target);
  const auto doc = w.built("file:///w/b.ws",
      "/** See {@link Remote}. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const auto text = docs.getDocumentation(*w.element(*doc, 0));
  CHECK(text && *text == "See [Remote](file:///no-extension#L1,10).");
}

// The same name declared locally and exported elsewhere: the local one wins.
void a_local_name_beats_a_global_one() {
  const World w;
  const auto lib = w.built("file:///w/lib.ws", "datatype Dup\n");
  const auto doc =
      w.built("file:///w/b.ws",
              "/** See {@link Dup}. */\nentity Person { }\ndatatype Dup\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "See [Dup](file:///w/b.ws#L3,10).");
  (void)lib;
}

// {@linkcode}: the label is code inside the link, styled before rendering.
void a_linkcode_keeps_its_backticks() {
  const World w;
  const auto doc = w.built(
      "file:///w/a.ws",
      "/** See {@linkcode String}. */\nentity Person { }\ndatatype String\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value() && *text == "See [`String`](file:///w/a.ws#L3,10).");
}

// An empty documentation comment says nothing: no raw answer, no markdown.
void an_empty_doc_comment_says_nothing() {
  const World w;
  const auto doc = w.built("file:///w/a.ws", "/**/\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  CHECK(!docs.getDocumentation(*w.element(*doc, 0), doc.get()));
}

// A node without a CST record answers nothing, and one without a document
// answers nothing either.
void a_node_without_a_record_says_nothing() {
  const World w;
  const auto doc = w.built("file:///w/a.ws", "entity Person { }\n");
  ws::ast::Entity made;
  made.type = &cursiv::ast_type_tag<ws::ast::Entity>;
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  CHECK(!docs.getDocumentation(made));
  CHECK(!docs.getDocumentation(made, doc.get()));
}

// The node alone finds its document: the parameter is an accelerator.
void the_node_alone_finds_its_document() {
  const World w;
  const auto doc =
      w.built("file:///w/a.ws", "/** The person. */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0));
  CHECK(text.has_value() && *text == "The person.");
}

void a_tag_renders_with_its_marker() {
  const World w;
  const auto doc =
      w.built("file:///w/a.ws",
              "/** Old.\n * @deprecated use B */\nentity Person { }\n");
  const cursiv::DefaultDocumentationProvider docs(*w.ws);
  const std::optional<std::string> text =
      docs.getDocumentation(*w.element(*doc, 0), doc.get());
  CHECK(text.has_value());
  CHECK(text->find("*@deprecated*") != std::string::npos);
  CHECK(text->find("use B") != std::string::npos);
}

}  // namespace

void run() {
  a_doc_comment_renders_to_markdown();
  a_plain_block_is_nobodys_documentation();
  a_link_resolves_to_the_declaration();
  an_unresolved_link_is_its_target();
  a_link_crosses_documents();
  a_link_replaces_the_targets_fragment();
  a_link_uses_the_target_documents_language_id();
  a_local_name_beats_a_global_one();
  a_linkcode_keeps_its_backticks();
  an_empty_doc_comment_says_nothing();
  a_node_without_a_record_says_nothing();
  the_node_alone_finds_its_document();
  a_tag_renders_with_its_marker();
}

}  // namespace documentation_provider

int main() {
  comment_provider::run();
  doc_comment::run();
  documentation_provider::run();
  return cursiv::test::report("documentation");
}
