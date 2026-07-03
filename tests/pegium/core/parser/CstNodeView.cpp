#include <gtest/gtest.h>

#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>

#include <ostream>
#include <ranges>
#include <vector>

namespace {

struct DummyElement final : pegium::grammar::AbstractElement {
  explicit DummyElement(ElementKind kind) : AbstractElement(kind) {}

  constexpr bool isNullable() const noexcept override { return false; }
  void print(std::ostream &os) const override { os << "dummy"; }
};

} // namespace

namespace pegium {
namespace {

static_assert(std::forward_iterator<ChildIterator>);
static_assert(std::ranges::forward_range<CstNodeView>);
static_assert(std::ranges::forward_range<RootCstNode>);

TEST(CstNodeViewTest, NavigatesNodesAndSiblings) {
  DummyElement literal{grammar::ElementKind::Literal};
  DummyElement group{grammar::ElementKind::Group};

  auto harness = test::makeCstBuilderHarness("abc de");
  auto &builder = harness.builder;

  builder.leaf(0, 1, &literal);
  builder.enter();
  builder.leaf(1, 2, &literal);
  builder.leaf(2, 3, &literal);
  builder.exit(1, 3, &group);
  builder.leaf(4, 5, &literal);

  auto *root = builder.getRootCstNode();
  auto rootIt = root->begin();
  ASSERT_NE(rootIt, root->end());

  const auto first = *rootIt;
  const auto parent = *++rootIt;
  const auto trailing = *++rootIt;

  EXPECT_EQ(first.getText(), "a");
  EXPECT_EQ(parent.getText(), "bc");
  EXPECT_EQ(trailing.getText(), "d");

  EXPECT_EQ(first.next().getText(), "bc");
  EXPECT_EQ(parent.previous().getText(), "a");
  EXPECT_FALSE(first.previous().valid());

  EXPECT_EQ(first.nextSibling().getText(), "bc");
  EXPECT_EQ(parent.nextSibling().getText(), "d");
  EXPECT_FALSE(trailing.nextSibling().valid());

  auto childIt = parent.begin();
  ASSERT_NE(childIt, parent.end());
  const auto leftChild = *childIt;
  const auto rightChild = *++childIt;

  EXPECT_EQ(leftChild.getText(), "b");
  EXPECT_EQ(rightChild.getText(), "c");
  EXPECT_EQ(leftChild.nextSibling().getText(), "c");
}

TEST(CstNodeViewTest, DetectsGapsBeforeAndAfterNodes) {
  DummyElement literal{grammar::ElementKind::Literal};

  auto harness = test::makeCstBuilderHarness(" a b ");
  auto &builder = harness.builder;

  builder.leaf(1, 2, &literal);
  builder.leaf(3, 4, &literal);

  auto *root = builder.getRootCstNode();
  auto it = root->begin();
  ASSERT_NE(it, root->end());
  const auto first = *it;
  const auto second = *++it;

  EXPECT_TRUE(first.hasGapBefore());
  EXPECT_TRUE(first.hasGapAfter());
  EXPECT_TRUE(second.hasGapBefore());
  EXPECT_TRUE(second.hasGapAfter());
}

TEST(CstNodeViewTest, LeafChildrenRangeIsEmpty) {
  DummyElement literal{grammar::ElementKind::Literal};

  auto harness = test::makeCstBuilderHarness("x");
  auto &builder = harness.builder;

  builder.leaf(0, 1, &literal);

  auto *root = builder.getRootCstNode();
  const auto leaf = *root->begin();

  EXPECT_EQ(leaf.begin(), leaf.end());

  std::size_t childCount = 0;
  for (const auto &child : leaf) {
    (void)child;
    ++childCount;
  }
  EXPECT_EQ(childCount, 0u);
}

TEST(CstNodeViewTest, IncludesZeroWidthChildAtParentEnd) {
  DummyElement literal{grammar::ElementKind::Literal};
  DummyElement group{grammar::ElementKind::Group};

  auto harness = test::makeCstBuilderHarness("x");
  auto &builder = harness.builder;

  builder.enter();
  builder.leaf(1, 1, &literal, false, true);
  builder.exit(0, 1, &group);

  auto *root = builder.getRootCstNode();
  auto rootIt = root->begin();
  ASSERT_NE(rootIt, root->end());
  const auto parent = *rootIt;

  auto childIt = parent.begin();
  ASSERT_NE(childIt, parent.end());
  const auto child = *childIt;

  EXPECT_EQ(child.getBegin(), 1u);
  EXPECT_EQ(child.getEnd(), 1u);
  EXPECT_TRUE(child.isRecovered());
  ++childIt;
  EXPECT_EQ(childIt, parent.end());
}

TEST(CstNodeViewTest, RewindedSpeculativeChildrenDoNotLeakIntoFinalNode) {
  DummyElement literal{grammar::ElementKind::Literal};
  DummyElement group{grammar::ElementKind::Group};

  auto harness = test::makeCstBuilderHarness("abc");
  auto &builder = harness.builder;

  builder.enter();
  const auto checkpoint = builder.mark();
  builder.leaf(0, 1, &literal);
  builder.rewind(checkpoint);
  builder.exit(0, 0, &group);
  builder.leaf(1, 2, &literal);

  auto *root = builder.getRootCstNode();
  auto rootIt = root->begin();
  ASSERT_NE(rootIt, root->end());
  const auto parent = *rootIt;
  ASSERT_NE(++rootIt, root->end());
  const auto sibling = *rootIt;

  EXPECT_TRUE(parent.isLeaf());
  EXPECT_EQ(parent.begin(), parent.end());
  EXPECT_EQ(parent.getText(), "");
  EXPECT_EQ(sibling.getText(), "b");
  EXPECT_EQ(parent.begin(), parent.end());
}

TEST(CstNodeViewTest, RewindRestoresChildSiblingLinks) {
  DummyElement literal{grammar::ElementKind::Literal};
  DummyElement group{grammar::ElementKind::Group};

  auto harness = test::makeCstBuilderHarness("abcd");
  auto &builder = harness.builder;

  builder.enter();
  builder.leaf(0, 1, &literal);
  const auto checkpoint = builder.mark();
  builder.leaf(1, 2, &literal);
  builder.rewind(checkpoint);
  builder.exit(0, 1, &group);
  builder.leaf(2, 3, &literal);

  auto *root = builder.getRootCstNode();
  auto rootIt = root->begin();
  ASSERT_NE(rootIt, root->end());
  const auto parent = *rootIt;
  ASSERT_NE(++rootIt, root->end());
  const auto sibling = *rootIt;

  std::vector<std::string_view> childTexts;
  for (const auto child : parent) {
    childTexts.push_back(child.getText());
  }

  EXPECT_EQ(parent.getText(), "a");
  EXPECT_EQ(childTexts, std::vector<std::string_view>{"a"});
  EXPECT_EQ(sibling.getText(), "c");
}

} // namespace
} // namespace pegium
