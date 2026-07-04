#include <gtest/gtest.h>

#include <memory>

#include <arithmetics/lsp/LspModule.hpp>
#include <arithmetics/core/CoreModule.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <sstream>
#include <string_view>

#include "LanguageTestSupport.hpp"

namespace arithmetics::tests {
namespace {

constexpr std::string_view kAngleGarbageClusterText =
    "Module basicMath\n"
    "\n"
    "def a: 5;\n"
    "def b: 3;\n"
    "def c: a + b; // 8\n"
    "def d: (a ^ b); // 164\n"
    "\n"
    "def root(x, y):\n"
    "    x^(1/y);\n"
    "\n"
    "def sqrt(x):\n"
    "    root(x, 2);\n"
    "\n"
    "2 * c; // 16\n"
    "b % 2; // 1\n"
    "\n"
    "// This language is case-insensitive regarding symbol names\n"
    "Root(D, 3); // 32\n"
    "Root(64, 3); // 4\n"
    "\n"
    "\n"
    "xxxxx\n"
    "xxxxxxx\n"
    "*\n"
    "xxxxxxxxx\n"
    ";\n"
    "\n"
    "<<<<<<<<<<<<\n"
    "\n"
    "\n"
    "sada;\n"
    "\n"
    "\n"
    "Sqrt(81/0); // 9\n";

// dump_parse_diagnostics is shared from LanguageTestSupport.hpp.

std::string dump_document_diagnostics(const pegium::workspace::Document &document) {
  std::string dump;
  for (const auto &diagnostic : document.diagnostics) {
    if (!dump.empty()) {
      dump += " | ";
    }
    std::ostringstream current;
    current << diagnostic.message << "@" << diagnostic.begin << "-"
            << diagnostic.end;
    dump += current.str();
  }
  return dump;
}

TEST(ArithmeticParserConstructionTest,
     StandaloneFactoryCreatesInitializedEntryRule) {
  auto parser = createArithmeticsParser();
  ASSERT_NE(parser, nullptr);
  EXPECT_EQ(parser->getEntryRule().getName(), "Module");
}

TEST(ArithmeticParserConstructionTest,
     StandaloneFactoryParserRecoversAngleGarbageCluster) {
  auto parser = createArithmeticsParser();
  ASSERT_NE(parser, nullptr);
  const auto result = parser->parse(kAngleGarbageClusterText);
  const auto dump = dump_parse_diagnostics(result.parseDiagnostics);

  EXPECT_TRUE(result.value) << dump;
  EXPECT_TRUE(result.fullMatch) << dump;
  EXPECT_TRUE(result.recoveryReport.hasRecovered) << dump;
}

TEST(ArithmeticParserConstructionTest,
     OpenAndBuildDocumentRecoversAngleGarbageCluster) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("open-build-angle-garbage.calc"),
      "arithmetics", std::string(kAngleGarbageClusterText));
  ASSERT_NE(document, nullptr);

  const auto dump = dump_document_diagnostics(*document);
  EXPECT_TRUE(document->parseSucceeded()) << dump;
  EXPECT_TRUE(document->parseRecovered()) << dump;
}

TEST(ArithmeticParserConstructionTest,
     DidChangeContentRecoversAngleGarbageCluster) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto uri = pegium::test::make_file_uri("did-change-angle-garbage.calc");
  auto documents = pegium::test::text_documents(*shared);
  ASSERT_NE(documents, nullptr);
  auto textDocument = pegium::test::set_text_document(
      *documents, uri, "arithmetics", std::string(kAngleGarbageClusterText), 1);

  shared->lsp.documentUpdateHandler->didChangeContent({.document = textDocument});

  ASSERT_TRUE(pegium::test::wait_until(
      [&]() {
        const auto documentId = shared->workspace.documents->getDocumentId(uri);
        auto document = shared->workspace.documents->getDocument(documentId);
        return document != nullptr &&
               document->state >= pegium::workspace::DocumentState::Validated;
      },
      std::chrono::milliseconds(3000)));

  const auto documentId = shared->workspace.documents->getDocumentId(uri);
  auto document = shared->workspace.documents->getDocument(documentId);
  ASSERT_NE(document, nullptr);

  const auto dump = dump_document_diagnostics(*document);
  EXPECT_TRUE(document->parseSucceeded()) << dump;
  EXPECT_TRUE(document->parseRecovered()) << dump;
}

TEST(ArithmeticParserConstructionTest,
     ServicesParserRecoversAngleGarbageCluster) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto &services =
      shared->serviceRegistry->getServices(
          pegium::test::make_file_uri("service-angle-garbage.calc"));
  ASSERT_NE(services.parser, nullptr);

  const auto result = services.parser->parse(kAngleGarbageClusterText);
  const auto dump = dump_parse_diagnostics(result.parseDiagnostics);

  EXPECT_TRUE(result.value) << dump;
  EXPECT_TRUE(result.fullMatch) << dump;
  EXPECT_TRUE(result.recoveryReport.hasRecovered) << dump;
}

} // namespace
} // namespace arithmetics::tests
