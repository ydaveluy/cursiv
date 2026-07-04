#include <gtest/gtest.h>

#include <memory>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <requirements/cli/CliUtils.hpp>
#include <requirements/cli/Generator.hpp>
#include <requirements/core/validation/RequirementsValidator.hpp>
#include <requirements/core/validation/TestsValidator.hpp>
#include <requirements/lsp/LspModule.hpp>
#include <requirements/lsp/RequirementsFormatter.hpp>
#include <requirements/lsp/LspServices.hpp>

#include <pegium/core/references/DefaultNameProvider.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace requirements::tests {
namespace {
std::filesystem::path fixture_root() {
  return pegium::test::current_source_directory() / "files";
}

std::filesystem::path example_root() {
  return pegium::test::current_source_directory().parent_path() / "example";
}

std::filesystem::path make_temp_directory() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto path = std::filesystem::temp_directory_path() /
              ("pegium-requirements-tests-" + suffix);
  std::filesystem::create_directories(path);
  return path;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::string read_fixture(std::string_view relativePath) {
  const auto path = fixture_root() / std::filesystem::path(relativePath);
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << path;
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::string expected_example_html() {
  const auto requirementPath =
      std::filesystem::absolute(example_root() / "requirements.req").string();
  const auto testsPart1Path =
      std::filesystem::absolute(example_root() / "tests_part1.tst").string();
  const auto testsPart2Path =
      std::filesystem::absolute(example_root() / "tests_part2.tst").string();
  std::string html;
  html += "<html>\n";
  html += "<body>\n";
  html += "<h1>Requirement coverage (demo generator)</h1>\n";
  html += "<div>Source: " + requirementPath + "</div>\n";
  html += "<table border=\"1\">\n";
  html += "<TR><TH>Requirement ID</TH><TH>Testcase ID</TH></TR>\n";
  html += "<TR><TD>ReqId000</TD><TD>\n";
  html += "<div>T005_generator (from " + testsPart2Path + ")</div>\n";
  html += "</TD></TR>\n";
  html += "<TR><TD>ReqId001_tstID</TD><TD>\n";
  html += "<div>T001_good_case (from " + testsPart1Path + ")</div>\n";
  html += "<div>T003_badTstId (from " + testsPart1Path + ")</div>\n";
  html += "</TD></TR>\n";
  html += "<TR><TD>ReqId002_reqID</TD><TD>\n";
  html += "<div>T001_good_case (from " + testsPart1Path + ")</div>\n";
  html += "<div>T002_badReqId (from " + testsPart1Path + ")</div>\n";
  html += "</TD></TR>\n";
  html += "<TR><TD>ReqId003_reqCov</TD><TD>\n";
  html += "<div>T004_cov (from " + testsPart1Path + ")</div>\n";
  html += "</TD></TR>\n";
  html += "</table>\n";
  html += "</body>\n";
  html += "</html>\n";
  return html;
}

std::shared_ptr<pegium::workspace::Document>
open_fixture_document(pegium::SharedServices &shared,
                      std::string_view fixtureSet, std::string_view fileName,
                      std::string languageId) {
  return pegium::test::open_and_build_document(
      shared,
      pegium::test::make_file_uri(std::string(fixtureSet) + "/" +
                                  std::string(fileName)),
      std::move(languageId),
      read_fixture(std::string(fixtureSet) + "/" + std::string(fileName)));
}

struct FixtureDocuments {
  std::shared_ptr<pegium::workspace::Document> requirements;
  std::shared_ptr<pegium::workspace::Document> testsPart1;
  std::shared_ptr<pegium::workspace::Document> testsPart2;
};

FixtureDocuments open_fixture_set(pegium::SharedServices &shared,
                                  std::string_view fixtureSet) {
  FixtureDocuments documents{
      .requirements = open_fixture_document(shared, fixtureSet, "requirements.req",
                                            "requirements-lang"),
      .testsPart1 = open_fixture_document(shared, fixtureSet, "tests_part1.tst",
                                          "tests-lang"),
      .testsPart2 = open_fixture_document(shared, fixtureSet, "tests_part2.tst",
                                          "tests-lang"),
  };

  const std::array changedDocumentIds{
      shared.workspace.documents->getOrCreateDocumentId(documents.requirements->uri),
      shared.workspace.documents->getOrCreateDocumentId(documents.testsPart1->uri),
      shared.workspace.documents->getOrCreateDocumentId(documents.testsPart2->uri),
  };
  (void)shared.workspace.documentBuilder->update(changedDocumentIds, {});

  documents.requirements =
      shared.workspace.documents->getDocument(documents.requirements->uri);
  documents.testsPart1 =
      shared.workspace.documents->getDocument(documents.testsPart1->uri);
  documents.testsPart2 =
      shared.workspace.documents->getDocument(documents.testsPart2->uri);
  return documents;
}

const pegium::Diagnostic *
find_diagnostic_containing(const pegium::workspace::Document &document,
                           std::string_view needle) {
  for (const auto &diagnostic : document.diagnostics) {
    if (diagnostic.message.find(needle) != std::string::npos) {
      return &diagnostic;
    }
  }
  return nullptr;
}

std::string apply_text_edits(const pegium::workspace::Document &document,
                             std::vector<::lsp::TextEdit> edits) {
  const auto &textDocument = document.textDocument();
  auto text = std::string(textDocument.getText());
  std::ranges::sort(edits, [&textDocument](const auto &left, const auto &right) {
    return textDocument.offsetAt(left.range.start) >
           textDocument.offsetAt(right.range.start);
  });
  for (const auto &edit : edits) {
    const auto begin = textDocument.offsetAt(edit.range.start);
    const auto end = textDocument.offsetAt(edit.range.end);
    text.replace(begin, end - begin, edit.newText);
  }
  return text;
}

TEST(RequirementsModuleTest, ExposesTypedServicesAndDedicatedValidators) {
  static_assert(std::is_class_v<requirements::RequirementsServices>);
  static_assert(std::is_class_v<requirements::TestsServices>);
  static_assert(std::is_class_v<
                requirements::validation::RequirementsValidator>);
  static_assert(std::is_class_v<
                requirements::validation::TestsValidator>);
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);

  auto services =
      requirements::createRequirementsAndTestsLspServices(*shared);
  auto &requirementsServices = services.requirements;
  auto &testsServices = services.tests;

  ASSERT_NE(requirementsServices, nullptr);
  ASSERT_NE(testsServices, nullptr);
  ASSERT_NE(requirementsServices->validator, nullptr);
  ASSERT_NE(testsServices->validator, nullptr);
  EXPECT_NE(dynamic_cast<pegium::references::DefaultNameProvider *>(
                requirementsServices->references.nameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<pegium::references::DefaultNameProvider *>(
                testsServices->references.nameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<requirements::RequirementsFormatter *>(
                requirementsServices->lsp.formatter.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<requirements::TestsFormatter *>(
                testsServices->lsp.formatter.get()),
            nullptr);

  static_assert(std::is_base_of_v<pegium::NamedAstNode,
                                  requirements::ast::Requirement>);
  requirements::ast::Requirement requirement;
  requirement.name = "REQ-1";
  EXPECT_EQ(requirementsServices->references.nameProvider->getName(requirement),
            (std::optional<std::string>{"REQ-1"}));

  static_assert(std::is_base_of_v<pegium::NamedAstNode, requirements::ast::Test>);
  requirements::ast::Test test;
  test.name = "T-1";
  EXPECT_EQ(testsServices->references.nameProvider->getName(test),
            (std::optional<std::string>{"T-1"}));
}

TEST(RequirementsModuleTest, RegistersRequirementsAndTestsLanguages) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(requirements::registerRequirementsLspServices(*shared));

  const auto &requirementsCoreServices =
      shared->serviceRegistry->getServices(pegium::test::make_file_uri("model.req"));
  const auto &testsCoreServices =
      shared->serviceRegistry->getServices(pegium::test::make_file_uri("suite.tst"));
  const auto *requirementsServices = dynamic_cast<const pegium::Services *>(&requirementsCoreServices);
  const auto *testsServices = dynamic_cast<const pegium::Services *>(&testsCoreServices);
  ASSERT_NE(requirementsServices, nullptr);
  ASSERT_NE(testsServices, nullptr);
  using requirements::RequirementsServices;
  using requirements::TestsServices;
  EXPECT_NE(dynamic_cast<const RequirementsServices *>(requirementsServices),
            nullptr);
  EXPECT_NE(dynamic_cast<const TestsServices *>(testsServices), nullptr);
  // ...and a container of one language must NOT be mistaken for the other:
  // a plain dynamic_cast disambiguates the two sibling languages on the registry.
  EXPECT_EQ(dynamic_cast<const TestsServices *>(requirementsServices), nullptr);
  EXPECT_EQ(dynamic_cast<const RequirementsServices *>(testsServices), nullptr);
}

TEST(RequirementsModuleTest, GoodFixturePublishesNoDiagnosticsAcrossDocuments) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(requirements::registerRequirementsLspServices(*shared));

  auto documents = open_fixture_set(*shared, "good");

  ASSERT_NE(documents.requirements, nullptr);
  ASSERT_NE(documents.testsPart1, nullptr);
  ASSERT_NE(documents.testsPart2, nullptr);
  EXPECT_TRUE(documents.requirements->diagnostics.empty());
  EXPECT_TRUE(documents.testsPart1->diagnostics.empty());
  EXPECT_TRUE(documents.testsPart2->diagnostics.empty());
}

TEST(RequirementsModuleTest, Bad1FixturePublishesExpectedSingleWarnings) {
  struct Case {
    const char *name;
    std::shared_ptr<pegium::workspace::Document> FixtureDocuments::*document;
    std::string_view needle;
    unsigned expectedLine;
  };
  static const std::array cases{
      Case{"BadRequirementIdentifier", &FixtureDocuments::requirements,
           "Requirement name ReqIdABC_reqID should contain a number.", 2u},
      Case{"BadTestIdentifier", &FixtureDocuments::testsPart1,
           "Test name TA should contain a number.", 1u},
      Case{"UncoveredRequirement", &FixtureDocuments::requirements,
           "Requirement ReqId004_unicorn not covered by a test.", 4u},
  };

  for (const auto &testCase : cases) {
    SCOPED_TRACE(testCase.name);
    auto shared = std::make_unique<requirements::RequirementsSharedServices>();
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
    ASSERT_TRUE(requirements::registerRequirementsLspServices(*shared));

    auto documents = open_fixture_set(*shared, "bad1");

    ASSERT_NE(documents.requirements, nullptr);
    ASSERT_NE(documents.testsPart1, nullptr);
    ASSERT_NE(documents.testsPart2, nullptr);

    const auto &document = documents.*(testCase.document);
    const auto *diagnostic =
        find_diagnostic_containing(*document, testCase.needle);
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(document->textDocument().positionAt(diagnostic->begin).line,
              testCase.expectedLine);
  }
}

TEST(RequirementsModuleTest,
     InvalidEnvironmentFixturePublishesExpectedWarnings) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(requirements::registerRequirementsLspServices(*shared));

  auto documents = open_fixture_set(*shared, "bad2");

  ASSERT_NE(documents.requirements, nullptr);
  ASSERT_NE(documents.testsPart1, nullptr);
  ASSERT_NE(documents.testsPart2, nullptr);

  const auto *firstDiagnostic = find_diagnostic_containing(
      *documents.testsPart1,
      "Test T002_badReqId references environment Linux_x86 which is not used by "
      "any referenced requirement.");
  ASSERT_NE(firstDiagnostic, nullptr);
  EXPECT_EQ(documents.testsPart1->textDocument().positionAt(
                firstDiagnostic->begin)
                .line,
            3u);

  const auto *secondDiagnostic = find_diagnostic_containing(
      *documents.testsPart1,
      "Test T004_cov references environment Linux_x86 which is not used by any "
      "referenced requirement.");
  ASSERT_NE(secondDiagnostic, nullptr);
  EXPECT_EQ(documents.testsPart1->textDocument().positionAt(
                secondDiagnostic->begin)
                .line,
            5u);
}

TEST(RequirementsModuleTest, RequirementsFormatterFormatsCompactModel) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      requirements::createRequirementsLspServices(*shared,
                                                  "requirements-lang");

  ASSERT_NE(registeredServices, nullptr);
  shared->serviceRegistry->registerServices(std::move(registeredServices));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("requirements-format.req"),
      "requirements-lang",
      "contact:\"team\" environment prod:\"Production\" environment staging:\"Staging\" "
      "req Req1 \"Users can login\" applicable for prod,staging");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.formatter, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;
  const auto edits = services->lsp.formatter->formatDocument(
      *document, params, pegium::utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*document, edits),
            "contact: \"team\"\n"
            "environment prod: \"Production\"\n"
            "environment staging: \"Staging\"\n"
            "req Req1 \"Users can login\" applicable for prod, staging");
}

TEST(RequirementsModuleTest, TestsFormatterFormatsCompactModel) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      requirements::createTestsLspServices(*shared, "tests-lang");

  ASSERT_NE(registeredServices, nullptr);
  shared->serviceRegistry->registerServices(std::move(registeredServices));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("tests-format.tst"), "tests-lang",
      "contact:\"qa\" tst T1 testfile=\"suite.ts\" tests Req1,Req2 applicable for prod,staging");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.formatter, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;
  const auto edits = services->lsp.formatter->formatDocument(
      *document, params, pegium::utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*document, edits),
            "contact: \"qa\"\n"
            "tst T1 testfile = \"suite.ts\" tests Req1, Req2 applicable for prod, staging");
}

TEST(RequirementsModuleTest, GeneratorMatchesExpectedCoverageRows) {
  auto shared = std::make_unique<requirements::RequirementsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      requirements::createRequirementsAndTestsLspServices(*shared);
  auto &requirementsServices = *services.requirements;
  shared->serviceRegistry->registerServices(std::move(services.requirements));
  shared->serviceRegistry->registerServices(std::move(services.tests));

  const auto extracted = requirements::extract_requirement_model_with_test_models(
      std::filesystem::absolute(example_root() / "requirements.req").string(),
      requirementsServices);

  const auto html = requirements::generate_summary_file_html_content(
      *extracted.requirementModel, extracted.testModels);
  EXPECT_EQ(html, expected_example_html());
}

TEST(RequirementsModuleTest, ExtractDestinationAndNameUsesDefaultCliConvention) {
  const auto data = requirements::extract_destination_and_name(
      "/tmp/some-dir/requirements.req", std::nullopt);
  EXPECT_EQ(data.destination, "generated");
  EXPECT_EQ(data.name, "requirements");

  const auto overridden = requirements::extract_destination_and_name(
      "/tmp/some-dir/requirements.req", "/tmp/out");
  EXPECT_EQ(overridden.destination, "/tmp/out");
  EXPECT_EQ(overridden.name, "requirements");
}

TEST(RequirementsModuleTest, CliGenerateCreatesExpectedHtmlReport) {
  const auto tempDirectory = make_temp_directory();
  const auto outputDirectory = tempDirectory / "generated-output";
  const auto inputPath = std::filesystem::absolute(example_root() / "requirements.req");
  const auto outputFile = tempDirectory / "requirements-cli.out";
  const std::string command =
      std::string("\"") + PEGIUM_EXAMPLE_REQUIREMENTS_CLI_PATH +
      "\" generate \"" + inputPath.string() + "\" -d \"" +
      outputDirectory.string() + "\" > \"" + outputFile.string() +
      "\" 2>&1";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 0);

  const auto generatedFile = outputDirectory / "requirements.html";
  ASSERT_TRUE(std::filesystem::exists(generatedFile));
  const auto html = read_file(generatedFile);
  EXPECT_NE(html.find("Requirement coverage (demo generator)"),
            std::string::npos);
  EXPECT_NE(html.find("ReqId000"), std::string::npos);
  EXPECT_NE(html.find("T005_generator"), std::string::npos);

  std::filesystem::remove_all(tempDirectory);
}

} // namespace
} // namespace requirements::tests
