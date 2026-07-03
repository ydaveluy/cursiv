#include <gtest/gtest.h>

#include <pegium/core/documentation/DocComment.hpp>

namespace pegium::documentation {
namespace {

TEST(DocCommentTest, IsDocCommentRecognizesDelimiters) {
  EXPECT_TRUE(is_doc_comment("/** x */"));
  EXPECT_TRUE(is_doc_comment("/**\n * x\n */"));
  EXPECT_FALSE(is_doc_comment("/* x */"));
  EXPECT_FALSE(is_doc_comment("// x"));
  EXPECT_FALSE(is_doc_comment(""));
}

TEST(DocCommentTest, ToStringRetainsInteriorLeadingSpaceAndTrimsOverall) {
  EXPECT_EQ(parse_doc_comment("/** A \n * B \n */").toString(), "A\n B");
  EXPECT_EQ(parse_doc_comment("/** A \n B */").toString(), "A\n B");
}

TEST(DocCommentTest, ParsesParagraphsLinesAndBreaks) {
  const auto comment = parse_doc_comment("/** A \n   *   B  \n*C \n\n D*/");
  ASSERT_EQ(comment.elements.size(), 2u);

  const auto &text = comment.elements.front();
  ASSERT_EQ(text.kind, DocCommentNode::Kind::Paragraph);
  ASSERT_EQ(text.children.size(), 4u);
  EXPECT_EQ(text.children[0].text, " A");
  EXPECT_EQ(text.children[1].text, "   B");
  EXPECT_EQ(text.children[2].text, "C");
  EXPECT_EQ(text.children[3].text, "");

  const auto &second = comment.elements.back();
  ASSERT_EQ(second.kind, DocCommentNode::Kind::Paragraph);
  ASSERT_EQ(second.children.size(), 1u);
  EXPECT_EQ(second.children[0].text, " D");
}

TEST(DocCommentTest, ParsesBlockTags) {
  const auto comment = parse_doc_comment("/** A \n   *  @B  \n* @C D \n*/");
  ASSERT_EQ(comment.elements.size(), 3u);

  EXPECT_EQ(comment.elements[0].kind, DocCommentNode::Kind::Paragraph);

  const auto &bTag = comment.elements[1];
  ASSERT_EQ(bTag.kind, DocCommentNode::Kind::Tag);
  EXPECT_EQ(bTag.name, "B");
  EXPECT_FALSE(bTag.inlineTag);
  EXPECT_TRUE(bTag.children.empty());

  const auto &cTag = comment.elements[2];
  ASSERT_EQ(cTag.kind, DocCommentNode::Kind::Tag);
  EXPECT_EQ(cTag.name, "C");
  ASSERT_EQ(cTag.children.size(), 1u);
  EXPECT_EQ(cTag.children[0].text, "D");
}

TEST(DocCommentTest, ParsesInlineTag) {
  const auto comment = parse_doc_comment("/** A {@link B} C */");
  ASSERT_EQ(comment.elements.size(), 1u);
  const auto &paragraph = comment.elements.front();
  ASSERT_EQ(paragraph.children.size(), 3u);
  EXPECT_EQ(paragraph.children[0].text, " A ");
  EXPECT_EQ(paragraph.children[1].kind, DocCommentNode::Kind::Tag);
  EXPECT_EQ(paragraph.children[1].name, "link");
  EXPECT_TRUE(paragraph.children[1].inlineTag);
  ASSERT_EQ(paragraph.children[1].children.size(), 1u);
  EXPECT_EQ(paragraph.children[1].children[0].text, "B");
  EXPECT_EQ(paragraph.children[2].text, " C");
}

TEST(DocCommentTest, RendersInlineLinksToMarkdown) {
  EXPECT_EQ(parse_doc_comment("/** {@link https://example.org/} */").toMarkdown(),
            "[https://example.org/](https://example.org/)");
  EXPECT_EQ(parse_doc_comment("/** {@linkcode https://example.org/} */").toMarkdown(),
            "[`https://example.org/`](https://example.org/)");

  DocCommentRenderOptions codeLinks;
  codeLinks.link = DocCommentRenderOptions::LinkStyle::Code;
  EXPECT_EQ(parse_doc_comment("/** {@link https://example.org/} */").toMarkdown(codeLinks),
            "[`https://example.org/`](https://example.org/)");

  // A non-URI link target falls back to its display text.
  EXPECT_EQ(parse_doc_comment("/** {@link Value} */").toMarkdown(), "Value");

  // The hook-less path must split target|display on '|' (matching the provider's
  // split_link_content), not only on whitespace.
  EXPECT_EQ(parse_doc_comment("/** {@link https://x/|shown} */").toMarkdown(),
            "[shown](https://x/)");
}

TEST(DocCommentTest, InlineLinkTrimsContentAndFallsBackOnEmptyDisplay) {
  // Trailing whitespace before `}` must not swallow the display: a single token
  // becomes both target and display (matching the provider's split_link_content),
  // so the hook-less path renders it rather than an empty string.
  EXPECT_EQ(parse_doc_comment("/** {@link Value } */").toMarkdown(), "Value");
  // A target|display split with surrounding whitespace trims both sides instead
  // of leaking it into the rendered label.
  EXPECT_EQ(parse_doc_comment("/** {@link a|b } */").toMarkdown(), "b");
}

TEST(DocCommentTest, RendersBlockTagToMarkdownAndString) {
  const auto comment = parse_doc_comment("/** @deprecated Since 1.0 */");
  EXPECT_EQ(comment.toMarkdown(), "*@deprecated* — Since 1.0");
  EXPECT_EQ(comment.toString(), "@deprecated Since 1.0");

  DocCommentRenderOptions bold;
  bold.tag = DocCommentRenderOptions::TagStyle::Bold;
  EXPECT_EQ(comment.toMarkdown(bold), "**@deprecated** — Since 1.0");
}

TEST(DocCommentTest, GetTagAndGetTags) {
  const auto comment = parse_doc_comment("/**\n"
                                   " * @param a\n"
                                   " * @param b\n"
                                   " * @returns c\n"
                                   " */");
  EXPECT_EQ(comment.getTags("param").size(), 2u);
  const auto *returns = comment.getTag("returns");
  ASSERT_NE(returns, nullptr);
  EXPECT_EQ(returns->name, "returns");
  EXPECT_EQ(comment.getTag("missing"), nullptr);
}

TEST(DocCommentTest, NodeRangesAreAnchoredToLines) {
  const auto comment = parse_doc_comment("/** A \n * B \n */");
  ASSERT_EQ(comment.elements.size(), 1u);
  const auto &paragraph = comment.elements.front();
  ASSERT_EQ(paragraph.children.size(), 2u);
  // " B" starts on line 1 right after the `*` marker.
  EXPECT_EQ(paragraph.children[1].text, " B");
  EXPECT_EQ(paragraph.children[1].range.start.line, 1u);
  EXPECT_EQ(paragraph.children[1].range.start.character, 2u);
}

TEST(DocCommentTest, ParseStartPositionOffsetsRanges) {
  const auto comment = parse_doc_comment("/** A */", DocCommentPosition{4, 7});
  ASSERT_EQ(comment.elements.size(), 1u);
  // Single line, so the content range stays on the start line, offset by the
  // start character plus the `/**` marker.
  EXPECT_EQ(comment.elements.front().range.start.line, 4u);
  EXPECT_GE(comment.elements.front().range.start.character, 7u);
}

TEST(DocCommentTest, CustomTagRendererTakesPrecedence) {
  // renderTag wins over the default rendering for every tag, inline or not.
  DocCommentRenderOptions options;
  options.renderTag = [](std::string_view name, std::string_view content,
                         bool inlineTag) -> std::optional<std::string> {
    (void)inlineTag;
    return "<" + std::string(name) + ":" + std::string(content) + ">";
  };

  EXPECT_EQ(parse_doc_comment("/** @deprecated soon */").toMarkdown(options),
            "<deprecated:soon>");
  EXPECT_EQ(parse_doc_comment("/** {@link target shown} */").toMarkdown(options),
            "<link:target shown>");
}

TEST(DocCommentTest, CustomLinkRendererForInlineLinks) {
  // Without a renderTag, an inline link falls through to renderLink, which
  // receives the split target and display.
  DocCommentRenderOptions options;
  options.renderLink =
      [](std::string_view target,
         std::string_view display) -> std::optional<std::string> {
    return "[[" + std::string(target) + "|" + std::string(display) + "]]";
  };

  EXPECT_EQ(parse_doc_comment("/** {@link target shown} */").toMarkdown(options),
            "[[target|shown]]");
}

} // namespace
} // namespace pegium::documentation
