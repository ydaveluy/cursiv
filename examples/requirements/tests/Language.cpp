#include <gtest/gtest.h>

#include <memory>

#include <requirements/core/CoreModule.hpp>
#include <requirements/lsp/LspServices.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/examples/RecoverySampleTestSupport.hpp>

namespace requirements::tests {
namespace {

TEST(RequirementsLanguageTest, ParsesRequirementModel) {
  auto parser = createRequirementsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "contact: \"team\"\n"
      "environment prod: \"Production\"\n"
      "req login \"Users can login\"\n",
      pegium::test::make_file_uri("requirements.req"), "requirements-lang");

  ASSERT_TRUE(document->parseSucceeded());
  auto *model =
      dynamic_cast<ast::RequirementModel *>(document->parseResult.value);
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(model->requirements.size(), 1u);
}

TEST(RequirementsLanguageTest, LinksEnvironmentMultiReferencesAndReportsUnresolvedOnes) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(requirements::registerRequirementsCoreServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("linking.req"), "requirements-lang",
      "environment prod: \"Production\"\n"
      "environment staging: \"Staging\"\n"
      "req login \"Users can login\" applicable for prod, staging, missing\n");

  ASSERT_NE(document, nullptr);
  const auto &parsed = document->parseResult;
  const auto parseDump =
      pegium::test::dump_parse_diagnostics(parsed.parseDiagnostics);
  auto *model =
      dynamic_cast<ast::RequirementModel *>(parsed.value);
  ASSERT_NE(model, nullptr) << "fullMatch=" << parsed.fullMatch
                            << " recovered=" << parsed.recoveryReport.hasRecovered
                            << " parsedLength=" << parsed.parsedLength << " "
                            << parseDump;
  ASSERT_EQ(model->requirements.size(), 1u);

  auto *requirement = model->requirements.front();
  ASSERT_NE(requirement, nullptr);
  ASSERT_EQ(requirement->environments.size(), 3u);

  ASSERT_TRUE(requirement->environments[0]);
  ASSERT_TRUE(requirement->environments[1]);
  EXPECT_EQ(requirement->environments[0]->name, "prod");
  EXPECT_EQ(requirement->environments[1]->name, "staging");

  EXPECT_FALSE(requirement->environments[2]);
  EXPECT_TRUE(requirement->environments[2].hasError());
  EXPECT_NE(requirement->environments[2].getErrorMessage().find("missing"),
            std::string::npos);
  EXPECT_TRUE(pegium::test::has_diagnostic_message(*document,
                                                   "named 'missing'"));
}

} // namespace
} // namespace requirements::tests
