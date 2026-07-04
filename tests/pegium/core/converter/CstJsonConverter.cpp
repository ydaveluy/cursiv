#include <gtest/gtest.h>

#include <pegium/core/ParseJsonTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>

#include "JsonConverterTestSupport.hpp"

namespace pegium::converter {
namespace {

using namespace pegium::parser;

struct AssignmentFormattingNode : pegium::AstNode {
  string name;
  bool isAbstract = false;
  vector<string> modifiers;
};

class AssignmentFormattingParser final : public PegiumParser {
protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return Root;
  }

  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  Terminal<> WS{"WS", some(s)};
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};
  Skipper skipper = SkipperBuilder().ignore(WS).build();
  Rule<AssignmentFormattingNode> Root{
      "Root",
      assign<&AssignmentFormattingNode::name>(ID) + ":"_kw +
          enable_if<&AssignmentFormattingNode::isAbstract>("abstract"_kw) +
          ":"_kw +
          some(append<&AssignmentFormattingNode::modifiers>("public"_kw |
                                                            "private"_kw),
               ","_kw)};
#pragma clang diagnostic pop
};

TEST(CstJsonConverterTest, ConvertsBuiltCstToReferenceJson) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  ASSERT_TRUE(test::register_language(*shared));

  const auto document = test::open_reference_cst_document(*shared);
  ASSERT_NE(document, nullptr);

  const auto expected = R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "content": [
            {
              "begin": 0,
              "end": 6,
              "grammarSource": "Literal",
              "text": "person"
            },
            {
              "begin": 7,
              "end": 11,
              "grammarSource": "name='John'",
              "text": "John"
            }
          ],
          "end": 11,
          "grammarSource": "persons+=Person"
        },
        {
          "begin": 12,
          "end": 25,
          "grammarSource": "Rule(COMMENT)",
          "hidden": true,
          "text": "/* comment */"
        },
        {
          "begin": 26,
          "content": [
            {
              "begin": 26,
              "end": 31,
              "grammarSource": "Literal",
              "text": "hello"
            },
            {
              "begin": 32,
              "end": 36,
              "grammarSource": "person='John'",
              "text": "John"
            }
          ],
          "end": 36,
          "grammarSource": "greetings+=Greeting"
        }
      ],
      "end": 36,
      "grammarSource": "Rule(Model)"
    }
  ]
})json";

  pegium::test::ExpectCst(*document, expected);
}

TEST(CstJsonConverterTest, FormatsAssignmentsWithFeatureOperatorAndElement) {
  AssignmentFormattingParser parser;
  const auto result = parser.parse("Thing : abstract : public , private");

  const auto expected = R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 5,
          "grammarSource": "name=ID",
          "text": "Thing"
        },
        {
          "begin": 6,
          "end": 7,
          "grammarSource": "Literal",
          "text": ":"
        },
        {
          "begin": 8,
          "end": 16,
          "grammarSource": "isAbstract?='abstract'",
          "text": "abstract"
        },
        {
          "begin": 17,
          "end": 18,
          "grammarSource": "Literal",
          "text": ":"
        },
        {
          "begin": 19,
          "content": [
            {
              "begin": 19,
              "end": 25,
              "grammarSource": "Literal",
              "text": "public"
            }
          ],
          "end": 25,
          "grammarSource": "modifiers+=('public'|'private')"
        },
        {
          "begin": 26,
          "end": 27,
          "grammarSource": "Literal",
          "text": ","
        },
        {
          "begin": 28,
          "content": [
            {
              "begin": 28,
              "end": 35,
              "grammarSource": "Literal",
              "text": "private"
            }
          ],
          "end": 35,
          "grammarSource": "modifiers+=('public'|'private')"
        }
      ],
      "end": 35,
      "grammarSource": "Rule(Root)"
    }
  ]
})json";

  pegium::test::ExpectCst(result, expected);
}

} // namespace
} // namespace pegium::converter
