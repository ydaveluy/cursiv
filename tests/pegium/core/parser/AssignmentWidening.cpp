#include <gtest/gtest.h>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/PegiumParser.hpp>

#include <cstdint>

using namespace pegium::parser;

namespace {

struct WideningNode : pegium::AstNode {
  std::int64_t value = 0;
};

} // namespace

// A terminal whose value type widens to the attribute type (an int NUMBER
// feeding an int64 field) satisfies the assign<> admissibility gate, so building
// the AST must perform the widening conversion rather than throwing at runtime.
TEST(AssignmentWideningTest, IntTerminalWidensToInt64Field) {
  TerminalRule<int> number{"Number", some(d)};
  ParserRule<WideningNode> root{"Root", assign<&WideningNode::value>(number)};

  pegium::parser::ParseResult result;
  ASSERT_NO_THROW(result = pegium::test::parse_rule_result(
                      root, "123", SkipperBuilder().build()));
  EXPECT_TRUE(result.fullMatch);
  auto *typed = pegium::ast_ptr_cast<WideningNode>(result.value);
  ASSERT_TRUE(typed != nullptr);
  EXPECT_EQ(typed->value, 123);
}
