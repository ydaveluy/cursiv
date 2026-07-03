#include <gtest/gtest.h>
#include <pegium/core/ParseJsonTestSupport.hpp>
#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/CstSearch.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/syntax-tree/AstArena.hpp>
#include <pegium/core/syntax-tree/RootCstNode.hpp>
#include <pegium/core/text/TextSnapshot.hpp>

#include <memory>
#include <type_traits>

using namespace pegium::parser;

namespace {

struct DummyAstNode : pegium::AstNode {};

struct LeafNode : pegium::AstNode {
  string name;
};

struct RootNode : pegium::AstNode {
  pointer<LeafNode> leaf;
};

struct ChainNode : pegium::AstNode {
  string token;
  pointer<ChainNode> previous;
};

template <typename RuleType>
ParseResult parse_rule(const RuleType &rule, std::string_view text,
                       const Skipper &skipper = SkipperBuilder().build(),
                       const ParseOptions &options = {}) {
  return pegium::test::parse_rule_result(rule, text, skipper, options);
}

} // namespace

static_assert(
    std::is_same_v<decltype(std::declval<pegium::Reference<DummyAstNode> &>()
                                .operator->()),
                   const DummyAstNode *>);
static_assert(std::is_same_v<
              decltype(std::declval<const pegium::Reference<DummyAstNode> &>()
                           .operator->()),
              const DummyAstNode *>);

TEST(ParserRuleTest, ParseRequiresFullConsumption) {
  ParserRule<DummyAstNode> rule{"Rule", ":"_kw + create<DummyAstNode>()};

  {
    auto result = pegium::test::ExpectParsedAst(rule, ":",
                                                R"json({
  "$type": "DummyAstNode"
})json");
    EXPECT_TRUE(result.fullMatch);
  }

  {
    auto result = parse_rule(rule, ":abc");
    EXPECT_FALSE(result.fullMatch);
    EXPECT_TRUE(result.value);
    ASSERT_EQ(result.parseDiagnostics.size(), 1u);
    EXPECT_EQ(result.parseDiagnostics.front().kind,
              ParseDiagnosticKind::Incomplete);
  }
}

TEST(ParserRuleTest, SuperCarriesForwardOverriddenAlternatives) {
  ParserRule<DummyAstNode> rule{"R", "a"_kw + create<DummyAstNode>()};
  EXPECT_TRUE(parse_rule(rule, "a").fullMatch);
  EXPECT_FALSE(parse_rule(rule, "b").fullMatch);

  // Override extending the rule: super() folds in the prior body, so the
  // inherited alternative keeps matching alongside the newly added one.
  rule = rule.super() | ("b"_kw + create<DummyAstNode>());
  EXPECT_TRUE(parse_rule(rule, "a").fullMatch);
  EXPECT_TRUE(parse_rule(rule, "b").fullMatch);
  EXPECT_FALSE(parse_rule(rule, "c").fullMatch);

  // super() accumulates across successive overrides.
  rule = rule.super() | ("c"_kw + create<DummyAstNode>());
  EXPECT_TRUE(parse_rule(rule, "a").fullMatch);
  EXPECT_TRUE(parse_rule(rule, "b").fullMatch);
  EXPECT_TRUE(parse_rule(rule, "c").fullMatch);

  // Reassigning without super() drops the accumulated alternatives.
  rule = "d"_kw + create<DummyAstNode>();
  EXPECT_FALSE(parse_rule(rule, "a").fullMatch);
  EXPECT_TRUE(parse_rule(rule, "d").fullMatch);
}

TEST(ParserRuleTest, SuperExtensionIsVisibleThroughAReferencingRule) {
  // A sub-rule referenced by a parent rule — the shape a derived parser
  // extends when it inherits a base grammar.
  ParserRule<LeafNode> leaf{"Leaf", assign<&LeafNode::name>("a"_kw)};
  ParserRule<RootNode> root{"Root", assign<&RootNode::leaf>(leaf)};

  EXPECT_TRUE(parse_rule(root, "a").fullMatch);
  EXPECT_FALSE(parse_rule(root, "b").fullMatch);

  // Extend the sub-rule in place, exactly as a derived parser's constructor
  // would: `leaf = leaf.super() | <new alternative>`.
  leaf = leaf.super() | assign<&LeafNode::name>("b"_kw);

  // The parent rule sees the extension because it references `leaf` by address.
  EXPECT_TRUE(parse_rule(root, "a").fullMatch);
  EXPECT_TRUE(parse_rule(root, "b").fullMatch);
}

TEST(ParserRuleTest, NestedParserRuleAssignmentSetsContainer) {
  ParserRule<LeafNode> leafRule{"Leaf", assign<&LeafNode::name>("leaf"_kw)};
  ParserRule<RootNode> rootRule{"Root", assign<&RootNode::leaf>(leafRule)};

  auto result = pegium::test::ExpectParsedAst(rootRule, "leaf",
                                              R"json({
  "$type": "RootNode",
  "leaf": {
    "$type": "LeafNode",
    "name": "leaf"
  }
})json");
  auto *typed = pegium::ast_ptr_cast<RootNode>(result.value);
  ASSERT_TRUE(typed != nullptr);
  ASSERT_TRUE(typed->leaf != nullptr);
  EXPECT_EQ(typed->leaf->getContainer(), typed);
}

TEST(ParserRuleTest, ParseAndGetValueExposeTypedAstValue) {
  ParserRule<LeafNode> rule{"Leaf", assign<&LeafNode::name>("leaf"_kw)};

  auto result = pegium::test::ExpectParsedAst(rule, "leaf",
                                              R"json({
  "$type": "LeafNode",
  "name": "leaf"
})json");
  ASSERT_TRUE(result.cst != nullptr);

  auto node = detail::findFirstMatchingNode(*result.cst, std::addressof(rule));
  ASSERT_TRUE(node.has_value());

  pegium::RootCstNode dummyCst{pegium::text::TextSnapshot::copy("")}; pegium::AstArena arena{dummyCst};
  pegium::parser::ValueBuildContext ctx{};
  ctx.arena = &arena;
  auto *value = rule.getValue(*node, ctx);
  auto *typed = pegium::ast_ptr_cast<LeafNode>(value);
  ASSERT_TRUE(typed != nullptr);
  EXPECT_EQ(typed->name, "leaf");
  EXPECT_FALSE(rule.getTypeName().empty());
}

TEST(ParserRuleTest, DirectParserRuleCompositionUsesParserRuleChildValue) {
  ParserRule<LeafNode> inner{"Inner", assign<&LeafNode::name>("leaf"_kw)};
  ParserRule<LeafNode> outer{"Outer", inner};

  pegium::test::ExpectAst(outer, "leaf",
                          R"json({
  "$type": "LeafNode",
  "name": "leaf"
})json");
}

TEST(ParserRuleTest, RuleWithoutActionsBuildsDefaultAstNode) {
  ParserRule<DummyAstNode> rule{"Rule", ":"_kw};

  pegium::test::ExpectAst(rule, ":", "{}", {.includeType = false});
}

TEST(ParserRuleTest, ParseRuleAddsNodeOnSuccessAndKeepsLocalFailureState) {
  ParserRule<LeafNode> rule{"Leaf", assign<&LeafNode::name>("leaf"_kw)};
  auto skipper = SkipperBuilder().build();

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("leaf");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};

    auto ok = parse(rule, ctx);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ctx.cursor() - input.begin(), 4);

    auto root = builder.getRootCstNode();
    auto node = root->begin();
    ASSERT_NE(node, root->end());
    EXPECT_EQ((*node).getGrammarElement(), std::addressof(rule));
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("xyz");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};

    auto ko = parse(rule, ctx);
    EXPECT_FALSE(ko);
    EXPECT_EQ(ctx.cursor(), input.begin());

    auto root = builder.getRootCstNode();
    EXPECT_NE(root->begin(), root->end());
  }
}

TEST(ParserRuleTest, TopLevelParseFailureLeavesEmptyCst) {
  ParserRule<DummyAstNode> rule{"Rule", "service"_kw};

  auto result = parse_rule(rule, "");
  ASSERT_NE(result.cst, nullptr);
  EXPECT_EQ(result.cst->begin(), result.cst->end());
  EXPECT_FALSE(result.fullMatch);
  EXPECT_FALSE(result.value);
}

TEST(ParserRuleTest, ConstructorOptionSetsLocalSkipper) {
  TerminalRule<> ws{"WS", some(s)};
  DataTypeRule<std::string> token{"Token", "a"_kw + "b"_kw};
  ParseOptions strictNoRecovery;
  strictNoRecovery.recoveryEnabled = false;

  ParserRule<LeafNode> withoutLocalSkipper{"Leaf",
                                           assign<&LeafNode::name>(token)};
  auto baseline =
      parse_rule(withoutLocalSkipper, "a   b", SkipperBuilder().build(),
                 strictNoRecovery);
  EXPECT_FALSE(baseline.value);

  ParserRule<LeafNode> withLocalSkipper{
      "Leaf", assign<&LeafNode::name>(token),
      opt::with_skipper(SkipperBuilder().ignore(ws).build())};

  pegium::test::ExpectAst(withLocalSkipper, "a   b",
                          R"json({
  "$type": "LeafNode",
  "name": "ab"
})json",
                          strictNoRecovery);
}

TEST(ParserRuleTest, NewActionCreatesCurrentNodeAndAppliesQueuedAssignments) {
  ParserRule<ChainNode> rule{
      "Chain", assign<&ChainNode::token>("x"_kw) + create<ChainNode>() +
                   nest<ChainNode, &ChainNode::previous>()};

  auto result = parse_rule(rule, "x");
  ASSERT_TRUE(result.value);
  auto *typed = pegium::ast_ptr_cast<ChainNode>(result.value);
  ASSERT_TRUE(typed != nullptr);

  EXPECT_TRUE(typed->token.empty());
  ASSERT_TRUE(typed->previous != nullptr);
  EXPECT_EQ(typed->previous->token, "x");
  EXPECT_EQ(typed->previous->getContainer(), typed);
}

TEST(ParserRuleTest, InitActionBuildsImplicitCurrentWhenMissing) {
  ParserRule<ChainNode> rule{"Chain",
                             assign<&ChainNode::token>("x"_kw) +
                                 nest<ChainNode, &ChainNode::previous>()};

  auto result = parse_rule(rule, "x");
  ASSERT_TRUE(result.value);
  auto *typed = pegium::ast_ptr_cast<ChainNode>(result.value);
  ASSERT_TRUE(typed != nullptr);
  ASSERT_TRUE(typed->previous != nullptr);
  EXPECT_EQ(typed->previous->token, "x");
  EXPECT_EQ(typed->previous->getContainer(), typed);
}
