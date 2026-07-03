#include <gtest/gtest.h>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>

#include <cstddef>

using namespace pegium::parser;

namespace {

struct InnerNode : pegium::AstNode {};

struct OuterNode : pegium::AstNode {};

// Recursively checks that every CST node exposes a well-formed half-open
// [begin, end) span. An inverted span (end < begin) makes
// CstNodeView::getText() underflow end-begin (unsigned TextOffset) into a
// multi-gigabyte out-of-bounds string_view, so the getText() length is only
// dereferenced once the span is known to be valid.
void expect_well_formed_spans(const pegium::CstNodeView &node) {
  EXPECT_LE(node.getBegin(), node.getEnd())
      << "inverted CST span [" << node.getBegin() << ", " << node.getEnd()
      << ")";
  if (node.getBegin() <= node.getEnd()) {
    EXPECT_EQ(node.getText().size(),
              static_cast<std::size_t>(node.getEnd() - node.getBegin()));
  }
  for (const auto child : node) {
    expect_well_formed_spans(child);
  }
}

} // namespace

// A nullable rule entered after trailing trivia must not produce an inverted
// CST span: the skipper advances the cursor over the trailing whitespace while
// the node's end stays at the last visible offset, so the entry checkpoint can
// sit past the end.
TEST(CstSpanInvariantTest, NullableRuleAfterTrailingTriviaHasWellFormedSpan) {
  TerminalRule<> ws{"WS", some(s)};
  ParserRule<InnerNode, true> inner{"Inner", option("x"_kw)};
  ParserRule<OuterNode> outer{"Outer", "a"_kw + inner};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  // "a" then trailing spaces: the skipper consumes the spaces as trivia,
  // advancing the cursor to EOF while the last visible offset stays at 1, then
  // the nullable Inner rule enters at the post-trivia cursor and matches empty.
  auto result = pegium::test::parse_rule_result(outer, "a   ", skipper);
  EXPECT_TRUE(result.fullMatch);
  ASSERT_TRUE(result.value != nullptr);
  ASSERT_TRUE(result.cst != nullptr);

  for (const auto top : *result.cst) {
    expect_well_formed_spans(top);
  }
}
