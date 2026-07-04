#include <gtest/gtest.h>

#include <pegium/core/ParseJsonTestSupport.hpp>

#include "JsonConverterTestSupport.hpp"

namespace pegium::converter {
namespace {
TEST(AstJsonConverterTest, ConvertsBuiltAstToReferenceJson) {
  auto shared = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*shared);
  ASSERT_TRUE(test::register_language(*shared));

  const auto document = test::open_reference_document(*shared);
  ASSERT_NE(document, nullptr);

  const auto expected = R"({
  "$type": "Model",
  "greetings": [
    {
      "$type": "Greeting",
      "person": {
        "$ref": "#1",
        "$refText": "John"
      }
    }
  ],
  "persons": [
    {
      "$type": "Person",
      "name": "John"
    }
  ]
})";

  pegium::test::ExpectAst(*document, expected);
}

TEST(AstJsonConverterTest,
     LeavesReferenceTextUnresolvedBeforeComputedScopesWithoutObservations) {
  auto shared = test::make_empty_shared_core_services();
  auto recordingSink =
      std::make_shared<pegium::test::RecordingObservabilitySink>();
  shared->observabilitySink = recordingSink;
  pegium::installDefaultSharedCoreServices(*shared);
  ASSERT_TRUE(test::register_language(*shared));

  const auto document = test::open_reference_document(*shared);
  ASSERT_NE(document, nullptr);
  for (const auto &handle : document->parseResult.references) {
    handle.get()->clearLinkState();
  }
  document->state = workspace::DocumentState::Parsed;

  const auto observationCount = recordingSink->observations().size();
  const auto json =
      pegium::AstJsonConverter::convert(*document->parseResult.value)
          .toJsonString();

  const auto expected = R"({
  "$type": "Model",
  "greetings": [
    {
      "$type": "Greeting",
      "person": {
        "$refText": "John"
      }
    }
  ],
  "persons": [
    {
      "$type": "Person",
      "name": "John"
    }
  ]
})";

  EXPECT_EQ(json, expected);
  EXPECT_EQ(recordingSink->observations().size(), observationCount);
}

} // namespace
} // namespace pegium::converter
