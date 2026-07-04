#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/core/syntax-tree/AstArena.hpp>
#include <string>
#include <vector>

#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/execution/TaskScheduler.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/validation/DefaultDocumentValidator.hpp>
#include <pegium/core/validation/DefaultValidationRegistry.hpp>
#include <pegium/core/validation/DocumentValidator.hpp>
#include <pegium/core/workspace/Document.hpp>

#include "ValidationTestUtils.hpp"

namespace pegium::validation {
namespace {

using namespace pegium::parser;
using test::ValidationNodeA;
using test::ValidationRefNode;

struct ValidationRootNode : pegium::AstNode {
  vector<pointer<ValidationNodeA>> nodes;
};

std::unique_ptr<pegium::SharedCoreServices> make_validation_shared_services(
    std::optional<std::size_t> workerCount = std::nullopt) {
  auto sharedServices = test::make_empty_shared_core_services();
  pegium::installDefaultSharedCoreServices(*sharedServices);
  sharedServices->observabilitySink =
      std::make_shared<test::RecordingObservabilitySink>();
  if (workerCount.has_value()) {
    sharedServices->taskScheduler =
        std::make_shared<execution::TaskScheduler>(*workerCount);
  }
  return sharedServices;
}

TEST(DefaultDocumentValidatorTest, SupportsBuiltInAndCustomCategories) {
  auto sharedServices = make_validation_shared_services();
  pegium::CoreServices languageServices(*sharedServices);
  languageServices.languageMetaData.languageId = "mini";
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  registry->registerCheck<ValidationRefNode>(
      [](const ValidationRefNode &, const ValidationAcceptor &acceptor) {
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Warning;
        diagnostic.message = "Custom warning";
        diagnostic.begin = 12;
        diagnostic.end = 14;
        acceptor(std::move(diagnostic));
      },
      "fast");
  languageServices.validation.validationRegistry = std::move(registry);
  pegium::installDefaultCoreServices(languageServices);
  DefaultDocumentValidator validator(languageServices);

  ParserRule<ValidationRefNode> root{
      "Root", "prefix"_kw + ":"_kw +
                  assign<&ValidationRefNode::ref>("UnknownSymbol"_kw)};

  workspace::Document document(test::make_text_document(
      "file:///validation.pg", "mini", "prefix:UnknownSymbol"));
  document.id = 1u;
  pegium::test::RuleParser parser(languageServices, root,
                                  SkipperBuilder().build());
  pegium::test::apply_parse_result(
      document, parser.parse(document.textDocument().getText()));
  ASSERT_TRUE(document.parseResult.value != nullptr);
  ASSERT_FALSE(document.parseResult.references.empty());
  auto *reference = dynamic_cast<AbstractSingleReference *>(
      document.parseResult.references.front().get());
  ASSERT_NE(reference, nullptr);
  document.state = workspace::DocumentState::Parsed;
  EXPECT_EQ(reference->resolve(), nullptr);
  EXPECT_FALSE(reference->hasError());

  ValidationOptions builtInOnly;
  builtInOnly.categories = {"built-in"};
  EXPECT_TRUE(validator.validateDocument(document, builtInOnly, {}).empty());

  document.state = workspace::DocumentState::ComputedScopes;
  EXPECT_EQ(reference->resolve(), nullptr);
  ASSERT_TRUE(reference->hasError());

  const auto builtInDiagnostics =
      validator.validateDocument(document, builtInOnly, {});
  ASSERT_EQ(builtInDiagnostics.size(), 1u);
  EXPECT_EQ(builtInDiagnostics.front().message,
            "Could not resolve reference to ValidationNodeA named "
            "'UnknownSymbol'.");
  EXPECT_EQ(builtInDiagnostics.front().source, "mini");
  EXPECT_EQ(builtInDiagnostics.front().begin, 7);
  EXPECT_EQ(builtInDiagnostics.front().end, 20);

  ValidationOptions customOnly;
  customOnly.categories = {"fast"};
  const auto customDiagnostics =
      validator.validateDocument(document, customOnly, {});
  ASSERT_EQ(customDiagnostics.size(), 1u);
  EXPECT_EQ(customDiagnostics.front().message, "Custom warning");
  EXPECT_EQ(customDiagnostics.front().source, "mini");
  EXPECT_EQ(customDiagnostics.front().severity,
            pegium::DiagnosticSeverity::Warning);
  EXPECT_EQ(customDiagnostics.front().begin, 12);
  EXPECT_EQ(customDiagnostics.front().end, 14);

  ValidationOptions unrelatedOnly;
  unrelatedOnly.categories = {"slow"};
  EXPECT_TRUE(validator.validateDocument(document, unrelatedOnly, {}).empty());
}

TEST(DefaultDocumentValidatorTest, RunsBeforeAndAfterDocumentHooks) {
  auto sharedServices = make_validation_shared_services();
  pegium::CoreServices languageServices(*sharedServices);
  languageServices.languageMetaData.languageId = "mini";
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  std::vector<std::string> phases;
  registry->registerBeforeDocument(
      [&phases](const pegium::AstNode &, const ValidationAcceptor &acceptor,
                std::span<const std::string> categories,
                const utils::CancellationToken &) {
        phases.push_back("before");
        EXPECT_EQ(categories.size(), 1u);
        EXPECT_EQ(categories.front(), "fast");
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Information;
        diagnostic.message = "Before";
        diagnostic.begin = 0;
        diagnostic.end = 0;
        acceptor(std::move(diagnostic));
      });
  registry->registerCheck<ValidationNodeA>(
      [&phases](const ValidationNodeA &, const ValidationAcceptor &acceptor) {
        phases.push_back("node");
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Warning;
        diagnostic.message = "Node";
        diagnostic.begin = 1;
        diagnostic.end = 2;
        acceptor(std::move(diagnostic));
      },
      "fast");
  registry->registerAfterDocument([&phases](const pegium::AstNode &,
                                            const ValidationAcceptor &acceptor,
                                            std::span<const std::string>,
                                            const utils::CancellationToken &) {
    phases.push_back("after");
    pegium::Diagnostic diagnostic;
    diagnostic.severity = pegium::DiagnosticSeverity::Hint;
    diagnostic.message = "After";
    diagnostic.begin = 3;
    diagnostic.end = 3;
    acceptor(std::move(diagnostic));
  });
  languageServices.validation.validationRegistry = std::move(registry);
  DefaultDocumentValidator validator(languageServices);

  workspace::Document document(
      test::make_text_document("file:///validation-hooks.pg", "mini", {}));
  document.id = 2u;
  document.parseResult.cst = std::make_unique<RootCstNode>(
      pegium::text::TextSnapshot::copy(""));
  document.parseResult.astArena =
      std::make_unique<pegium::AstArena>(*document.parseResult.cst);
  document.parseResult.astArena->attachDocument(document);
  document.parseResult.value =
      document.parseResult.astArena->create<ValidationNodeA>();

  ValidationOptions options;
  options.categories = {"fast"};
  const auto diagnostics = validator.validateDocument(document, options, {});

  ASSERT_EQ(phases, (std::vector<std::string>{"before", "node", "after"}));
  ASSERT_EQ(diagnostics.size(), 3u);
  EXPECT_EQ(diagnostics[0].message, "Before");
  EXPECT_EQ(diagnostics[1].message, "Node");
  EXPECT_EQ(diagnostics[2].message, "After");
}

TEST(DefaultDocumentValidatorTest, UsesReferenceNodeRangeWhenAvailable) {
  auto sharedServices = make_validation_shared_services();
  pegium::CoreServices languageServices(*sharedServices);
  languageServices.languageMetaData.languageId = "mini";
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  languageServices.validation.validationRegistry = std::move(registry);
  pegium::installDefaultCoreServices(languageServices);
  DefaultDocumentValidator validator(languageServices);

  ParserRule<ValidationRefNode> root{
      "Root", "prefix"_kw + ":"_kw +
                  assign<&ValidationRefNode::ref>("UnknownSymbol"_kw)};

  workspace::Document document(test::make_text_document(
      "file:///validation-ref.pg", "mini", "prefix:UnknownSymbol"));
  document.id = 3u;
  pegium::test::RuleParser parser(languageServices, root,
                                  SkipperBuilder().build());
  pegium::test::apply_parse_result(
      document, parser.parse(document.textDocument().getText()));
  ASSERT_TRUE(document.parseResult.value != nullptr);
  ASSERT_FALSE(document.parseResult.references.empty());
  auto *reference = dynamic_cast<AbstractSingleReference *>(
      document.parseResult.references.front().get());
  ASSERT_NE(reference, nullptr);
  document.state = workspace::DocumentState::Parsed;
  EXPECT_EQ(reference->resolve(), nullptr);
  EXPECT_FALSE(reference->hasError());

  ValidationOptions builtInOnly;
  builtInOnly.categories = {"built-in"};
  EXPECT_TRUE(validator.validateDocument(document, builtInOnly, {}).empty());

  document.state = workspace::DocumentState::ComputedScopes;
  EXPECT_EQ(reference->resolve(), nullptr);
  ASSERT_TRUE(reference->hasError());

  const auto diagnostics =
      validator.validateDocument(document, builtInOnly, {});
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().message,
            "Could not resolve reference to ValidationNodeA named "
            "'UnknownSymbol'.");
  EXPECT_EQ(diagnostics.front().begin, 7u);
  EXPECT_EQ(diagnostics.front().end, 20u);
}

TEST(DefaultDocumentValidatorTest, LinkingDiagnosticCarriesStructuredData) {
  auto sharedServices = make_validation_shared_services();
  pegium::CoreServices languageServices(*sharedServices);
  languageServices.languageMetaData.languageId = "mini";
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  languageServices.validation.validationRegistry = std::move(registry);
  pegium::installDefaultCoreServices(languageServices);
  DefaultDocumentValidator validator(languageServices);

  ParserRule<ValidationRefNode> root{
      "Root", "prefix"_kw + ":"_kw +
                  assign<&ValidationRefNode::ref>("UnknownSymbol"_kw)};

  workspace::Document document(test::make_text_document(
      "file:///validation-ref-data.pg", "mini", "prefix:UnknownSymbol"));
  document.id = 4u;
  pegium::test::RuleParser parser(languageServices, root,
                                  SkipperBuilder().build());
  pegium::test::apply_parse_result(
      document, parser.parse(document.textDocument().getText()));
  ASSERT_FALSE(document.parseResult.references.empty());

  document.state = workspace::DocumentState::ComputedScopes;
  auto *reference = dynamic_cast<AbstractSingleReference *>(
      document.parseResult.references.front().get());
  ASSERT_NE(reference, nullptr);
  EXPECT_EQ(reference->resolve(), nullptr);
  ASSERT_TRUE(reference->hasError());

  ValidationOptions builtInOnly;
  builtInOnly.categories = {"built-in"};
  const auto diagnostics = validator.validateDocument(document, builtInOnly, {});
  ASSERT_EQ(diagnostics.size(), 1u);

  ASSERT_TRUE(diagnostics.front().data.has_value());
  const auto &data = *diagnostics.front().data;
  ASSERT_TRUE(data.isObject());
  const auto &object = data.object();
  const auto field = [&](std::string_view key) -> std::string {
    const auto it = object.find(std::string(key));
    return it == object.end() ? std::string{} : it->second.string();
  };
  EXPECT_EQ(field("code"), "linking-error");
  EXPECT_EQ(field("containerType"), "ValidationRefNode");
  EXPECT_EQ(field("property"), "ref");
  EXPECT_EQ(field("refText"), "UnknownSymbol");
}

TEST(DefaultDocumentValidatorTest,
     BuiltInParseDiagnosticsUseLanguageIdAsSourceAndStopCustomValidation) {
  auto parser = std::make_unique<test::FakeParser>();
  parser->fullMatch = false;
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  parser::ParseDiagnostic insertedDiagnostic;
  insertedDiagnostic.kind = ParseDiagnosticKind::Inserted;
  insertedDiagnostic.offset = 0;
  insertedDiagnostic.element = std::addressof(id);
  parser->parseDiagnostics.push_back(insertedDiagnostic);

  auto sharedServicesPtr = make_validation_shared_services();
  pegium::CoreServices languageServices(*sharedServicesPtr);
  languageServices.languageMetaData.languageId = "mini";
  languageServices.parser = std::move(parser);
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  bool customValidationRan = false;
  registry->registerCheck<ValidationNodeA>(
      [&customValidationRan](const ValidationNodeA &,
                             const ValidationAcceptor &acceptor) {
        customValidationRan = true;
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Warning;
        diagnostic.message = "Custom warning";
        acceptor(std::move(diagnostic));
      },
      "fast");
  languageServices.validation.validationRegistry = std::move(registry);
  DefaultDocumentValidator validator(languageServices);

  workspace::Document document(test::make_text_document(
      "file:///validation-parse-source.pg", "mini", {}));
  document.id = 4u;
  document.parseResult.cst = std::make_unique<RootCstNode>(
      pegium::text::TextSnapshot::copy(""));
  document.parseResult.astArena =
      std::make_unique<pegium::AstArena>(*document.parseResult.cst);
  document.parseResult.astArena->attachDocument(document);
  document.parseResult.value =
      document.parseResult.astArena->create<ValidationNodeA>();
  document.parseResult.parseDiagnostics.push_back(insertedDiagnostic);

  ValidationOptions options;
  options.stopAfterParsingErrors = true;
  const auto diagnostics = validator.validateDocument(document, options, {});

  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().source, "mini");
  EXPECT_EQ(std::get<std::string>(*diagnostics.front().code), "parse.inserted");
  EXPECT_FALSE(customValidationRan);
}

// Builds a document with enough descendants to cross the parallelChunked
// threshold, validates it with a @p workerCount-worker scheduler, and asserts
// the diagnostics keep document order regardless of how the chunks are
// scheduled. With several workers the per-node chunks run concurrently, so the
// thread sanitizer observes the runChecks fan-out.
void expect_parallel_validation_preserves_diagnostic_order(
    std::size_t workerCount) {
  SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
  auto sharedServices = make_validation_shared_services(workerCount);
  pegium::CoreServices languageServices(*sharedServices);
  languageServices.languageMetaData.languageId = "mini";
  auto registry = std::make_unique<DefaultValidationRegistry>(languageServices);
  registry->registerCheck<ValidationRootNode>(
      [](const ValidationRootNode &, const ValidationAcceptor &acceptor) {
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Information;
        diagnostic.message = "root";
        diagnostic.begin = 0;
        diagnostic.end = 0;
        acceptor(std::move(diagnostic));
      },
      "fast");
  registry->registerCheck<ValidationNodeA>(
      [](const ValidationNodeA &node, const ValidationAcceptor &acceptor) {
        pegium::Diagnostic diagnostic;
        diagnostic.severity = pegium::DiagnosticSeverity::Warning;
        diagnostic.message = std::to_string(node.getCstNode().getBegin());
        diagnostic.begin = node.getCstNode().getBegin();
        diagnostic.end = node.getCstNode().getEnd();
        acceptor(std::move(diagnostic));
      },
      "fast");
  languageServices.validation.validationRegistry = std::move(registry);
  DefaultDocumentValidator validator(languageServices);

  ParserRule<ValidationNodeA> nodeRule{"Node", ","_kw};
  ParserRule<ValidationRootNode> rootRule{
      "Root", some(append<&ValidationRootNode::nodes>(nodeRule))};

  // 260 nodes (>= parallelChunked's 256-node threshold) so a multi-worker
  // scheduler splits the per-node checks into several concurrent chunks.
  workspace::Document document(test::make_text_document(
      "file:///validation-order.pg", "mini", std::string(260, ',')));
  document.id = 5u;
  pegium::test::parse_rule(rootRule, document, SkipperBuilder().build());
  ASSERT_TRUE(document.parseResult.value != nullptr);
  const auto *rootNode = dynamic_cast<const ValidationRootNode *>(
      document.parseResult.value);
  ASSERT_NE(rootNode, nullptr);
  std::vector<std::string> expectedMessages;
  for (const auto *node : rootNode->getAllContent<ValidationNodeA>()) {
    expectedMessages.push_back(std::to_string(node->getCstNode().getBegin()));
  }

  ValidationOptions options;
  options.categories = {"fast"};
  const auto diagnostics = validator.validateDocument(document, options, {});

  ASSERT_EQ(diagnostics.size(), expectedMessages.size() + 1U);
  EXPECT_EQ(diagnostics.front().message, "root");
  for (std::size_t index = 1; index < diagnostics.size(); ++index) {
    EXPECT_EQ(diagnostics[index].message, expectedMessages[index - 1U]);
  }
}

TEST(DefaultDocumentValidatorTest,
     PreservesDiagnosticOrderWhenValidatingDescendantsInParallel) {
  // One worker: the chunks run serially, pinning the deterministic ascending
  // concatenation of the per-chunk diagnostic buffers.
  expect_parallel_validation_preserves_diagnostic_order(1);
}

TEST(DefaultDocumentValidatorTest,
     PreservesDiagnosticOrderWhenChunksRunConcurrently) {
  // Several workers: the per-node checks fan out across chunks that execute at
  // the same time, so the order must hold independently of execution order.
  expect_parallel_validation_preserves_diagnostic_order(4);
}

} // namespace
} // namespace pegium::validation
