#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <arithmetics/core/ast.hpp>
#include <arithmetics/lsp/LspModule.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/core/utils/UriUtils.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace pegium::integration {
namespace {

bool is_parse_diagnostic(const pegium::Diagnostic &diagnostic) {
  if (diagnostic.source == "parse") {
    return true;
  }
  if (!diagnostic.code.has_value()) {
    return false;
  }
  const auto *code = std::get_if<std::string>(&*diagnostic.code);
  return code != nullptr && code->starts_with("parse.");
}

class BlockingBootstrapValidation final {
public:
  void operator()(const AstNode &, const validation::ValidationAcceptor &,
                  const utils::CancellationToken &cancelToken) const {
    bool expected = false;
    if (!_started.compare_exchange_strong(expected, true)) {
      return;
    }

    while (!cancelToken.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    _observedCancellation = true;
    utils::throw_if_cancelled(cancelToken);
  }

  [[nodiscard]] bool
  waitUntilStarted(std::chrono::milliseconds timeout =
                       std::chrono::milliseconds(1000)) const {
    return test::wait_until([this]() { return _started.load(); }, timeout);
  }

  [[nodiscard]] bool observedCancellation() const noexcept {
    return _observedCancellation.load();
  }

private:
  mutable std::atomic<bool> _started = false;
  mutable std::atomic<bool> _observedCancellation = false;
};

class BootstrapResumeIntegrationTest : public ::testing::Test {
protected:
  std::unique_ptr<arithmetics::ArithmeticsSharedServices> shared =
      std::make_unique<arithmetics::ArithmeticsSharedServices>();
  std::shared_ptr<test::FakeFileSystemProvider> fileSystem =
      std::make_shared<test::FakeFileSystemProvider>();
  BlockingBootstrapValidation blockingValidation;

  BootstrapResumeIntegrationTest() {
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    shared->workspace.fileSystemProvider = fileSystem;
  }

  void SetUp() override {
    auto services =
        arithmetics::createArithmeticsLspServices(*shared, "arithmetics");
    services->validation.validationRegistry->registerCheck<AstNode>(
        [this](const AstNode &node,
               const validation::ValidationAcceptor &acceptor,
               const utils::CancellationToken &cancelToken) {
          blockingValidation(node, acceptor, cancelToken);
        },
        validation::kFastValidationCategory);
    shared->serviceRegistry->registerServices(std::move(services));
  }

  void installWorkspaceFile(std::string_view rootPath, std::string_view fileName,
                            std::string text) {
    const auto root = std::string(rootPath);
    const auto file = root + "/" + std::string(fileName);
    fileSystem->directories[root] = {file};
    fileSystem->files[file] = std::move(text);
  }
};

class DocumentPipelineIntegrationTest : public ::testing::Test {
protected:
  std::unique_ptr<arithmetics::ArithmeticsSharedServices> shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();

  DocumentPipelineIntegrationTest() {
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
  }

  void SetUp() override {
    ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));
  }

  void TearDown() override {
    if (shared != nullptr && shared->workspace.workspaceLock != nullptr) {
      auto drain = shared->workspace.workspaceLock->write(
          [](const utils::CancellationToken &, const auto &) {});
      if (drain.valid()) {
        drain.get();
      }
    }
  }

  std::shared_ptr<workspace::Document> updateDocument(std::string_view fileName,
                                                      std::string text,
                                                      std::chrono::milliseconds timeout =
                                                          std::chrono::milliseconds(3000)) {
    const auto uri = test::make_file_uri(fileName);
    auto documents = test::text_documents(*shared);
    auto textDocument = test::set_text_document(
        *documents, uri, "arithmetics", std::move(text), 1);

    shared->lsp.documentUpdateHandler->didChangeContent({.document = textDocument});

    const auto documentId = shared->workspace.documents->getDocumentId(uri);
    EXPECT_NE(documentId, workspace::InvalidDocumentId);
    if (documentId == workspace::InvalidDocumentId) {
      return nullptr;
    }

    const bool ready = test::wait_until([&]() {
      auto document = shared->workspace.documents->getDocument(documentId);
      return document != nullptr &&
             document->state >= workspace::DocumentState::Validated;
    }, timeout);
    if (!ready) {
      shared->workspace.documentBuilder->waitUntil(
          workspace::DocumentState::Validated);
    }

    auto document = shared->workspace.documents->getDocument(uri);
    if (document != nullptr &&
        document->state < workspace::DocumentState::Validated) {
      (void)shared->workspace.documentBuilder->waitUntil(
          workspace::DocumentState::Validated, document->id);
      document = shared->workspace.documents->getDocument(document->id);
    }
    EXPECT_NE(document, nullptr);
    return document;
  }

  static std::vector<const pegium::Diagnostic *>
  parseDiagnostics(const workspace::Document &document) {
    std::vector<const pegium::Diagnostic *> diagnostics;
    for (const auto &diagnostic : document.diagnostics) {
      if (is_parse_diagnostic(diagnostic)) {
        diagnostics.push_back(std::addressof(diagnostic));
      }
    }
    return diagnostics;
  }

  static std::size_t parseDiagnosticCount(const workspace::Document &document) {
    return std::ranges::count_if(document.diagnostics, [](const auto &diagnostic) {
      return is_parse_diagnostic(diagnostic);
    });
  }

  static std::string dumpDiagnostics(
      const std::vector<const pegium::Diagnostic *> &diagnostics) {
    std::string dump;
    for (const auto *diagnostic : diagnostics) {
      if (!dump.empty()) {
        dump += " | ";
      }
      dump += diagnostic->message + "@" + std::to_string(diagnostic->begin) + "-" +
              std::to_string(diagnostic->end);
    }
    return dump;
  }
};

TEST_F(BootstrapResumeIntegrationTest,
       UpdateSupersedesBootstrapBuildAndWaitUntilValidatedResumesOnLatestText) {
  const auto rootPath = std::string("/tmp/pegium-tests/bootstrap-resume");
  const auto rootUri = utils::path_to_file_uri(rootPath);
  const auto filePath = rootPath + "/bootstrap.calc";
  const auto fileUri = utils::path_to_file_uri(filePath);
  const auto initialText =
      std::string("module Demo\n"
                  "def value: 1;\n"
                  "value;\n");
  const auto updatedText =
      std::string("module Demo\n"
                  "def value: 1 / 0;\n"
                  "value;\n");

  installWorkspaceFile(rootPath, "bootstrap.calc", initialText);

  workspace::InitializeParams initializeParams{};
  initializeParams.workspaceFolders.push_back(
      workspace::WorkspaceFolder{.uri = rootUri, .name = "workspace"});
  shared->workspace.workspaceManager->initialize(initializeParams);

  auto initializedFuture =
      shared->workspace.workspaceManager->initialized(workspace::InitializedParams{});

  ASSERT_TRUE(blockingValidation.waitUntilStarted());
  EXPECT_EQ(shared->workspace.workspaceManager->ready().wait_for(
                std::chrono::milliseconds(0)),
            std::future_status::ready);
  EXPECT_EQ(initializedFuture.wait_for(std::chrono::milliseconds(0)),
            std::future_status::timeout);

  const auto documentId = shared->workspace.documents->getDocumentId(fileUri);
  ASSERT_NE(documentId, workspace::InvalidDocumentId);

  auto bootstrapDocument = shared->workspace.documents->getDocument(documentId);
  ASSERT_NE(bootstrapDocument, nullptr);
  EXPECT_LT(bootstrapDocument->state, workspace::DocumentState::Validated);
  EXPECT_EQ(bootstrapDocument->textDocument().getText(), initialText);

  auto documents = test::text_documents(*shared);
  auto textDocument =
      test::set_text_document(*documents, fileUri, "arithmetics", updatedText, 1);
  ASSERT_NE(textDocument, nullptr);

  shared->lsp.documentUpdateHandler->didChangeContent({.document = textDocument});

  auto waitUntilValidated = std::async(std::launch::async, [&]() {
    return shared->workspace.documentBuilder->waitUntil(
        workspace::DocumentState::Validated, documentId);
  });

  ASSERT_EQ(waitUntilValidated.wait_for(std::chrono::seconds(3)),
            std::future_status::ready);
  EXPECT_EQ(waitUntilValidated.get(), documentId);

  auto document = shared->workspace.documents->getDocument(documentId);
  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, workspace::DocumentState::Validated);
  EXPECT_EQ(document->textDocument().getText(), updatedText);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
  EXPECT_TRUE(blockingValidation.observedCancellation());
  EXPECT_NO_THROW(initializedFuture.get());
}

TEST_F(DocumentPipelineIntegrationTest,
       DocumentUpdateHandlerBuildsAndValidatesOpenDocument) {
  auto document = updateDocument(
      "pipeline.calc",
      "module Demo\n"
      "def value: 1 / 0;\n"
      "value;\n");

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, workspace::DocumentState::Validated);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
}

TEST_F(DocumentPipelineIntegrationTest,
       EmptyDocumentPublishesGrammarParseDiagnostic) {
  auto document = updateDocument("pipeline-empty.calc", "");

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, workspace::DocumentState::Validated);

  const auto diagnostics = parseDiagnostics(*document);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front()->message, "Expecting 'module'");
  EXPECT_EQ(diagnostics.front()->begin, 0u);
  EXPECT_EQ(diagnostics.front()->end, 0u);
  ASSERT_EQ(document->parseResult.parseDiagnostics.size(), 1u);
  EXPECT_EQ(document->parseResult.parseDiagnostics.front().offset, 0u);
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSeparatorAfterModuleKeywordPublishesSeparatorDiagnostic) {
  auto document = updateDocument("pipeline-module-gap.calc", "ModulebasicMath\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front()->message, "Expecting separator");
  EXPECT_EQ(diagnostics.front()->begin, 6u);
  EXPECT_EQ(diagnostics.front()->end, 7u);
}

TEST_F(DocumentPipelineIntegrationTest,
       ModuleKeywordTypoPublishesReplaceDiagnosticOnOriginalToken) {
  auto document =
      updateDocument("pipeline-module-keyword-typo.calc", "Modle basicMath\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  ASSERT_EQ(diagnostics.size(), 1u) << parseDump;
  EXPECT_EQ(diagnostics.front()->message, "Expecting 'module' but found `Modle`.")
      << parseDump;
  EXPECT_EQ(diagnostics.front()->begin, 0u) << parseDump;
  EXPECT_EQ(diagnostics.front()->end, 5u) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       IncompleteRecoveredDocumentPublishesParseDiagnostic) {
  auto document = updateDocument(
      "pipeline-incomplete.calc",
      "module mathModule\n"
      "def ");

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, workspace::DocumentState::Validated);

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message == "Expecting ;";
  })) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingModuleNameAtEofAnchorsDiagnosticAfterModuleKeyword) {
  auto document = updateDocument(
      "pipeline-missing-module-name-eof.calc",
      "module\n"
      "\n");

  ASSERT_NE(document, nullptr);

  const auto diagnostics = parseDiagnostics(*document);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front()->message, "Expecting ID");
  EXPECT_EQ(diagnostics.front()->begin, 6u);
  EXPECT_EQ(diagnostics.front()->end, 6u);
}

TEST_F(DocumentPipelineIntegrationTest,
       IdentifierLikeStatementFragmentPublishesMissingSemicolonDiagnostic) {
  auto document = updateDocument(
      "pipeline-partial-def-keyword.calc",
      "module mod\n"
      "de\n");

  ASSERT_NE(document, nullptr);

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  ASSERT_FALSE(diagnostics.empty()) << parseDump;
  EXPECT_TRUE(std::ranges::all_of(diagnostics, [](const auto *diagnostic) {
    return is_parse_diagnostic(*diagnostic);
  })) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message == "Expecting ;";
  })) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       TrailingStatementFragmentPublishesGrammarParseDiagnostic) {
  auto document = updateDocument(
      "pipeline-trailing-expression.calc",
      "module name\n"
      "2   *");

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(document->state, workspace::DocumentState::Validated);

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  ASSERT_FALSE(diagnostics.empty()) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message.find("NUMBER") != std::string::npos ||
           diagnostic->message.find("ID") != std::string::npos ||
           diagnostic->message.find("(") != std::string::npos;
  })) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->begin >= 11u && diagnostic->begin <= 17u;
  })) << parseDump;
  ASSERT_FALSE(document->parseResult.parseDiagnostics.empty());
  EXPECT_EQ(document->parseResult.parseDiagnostics.back().offset, 17u);
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingModuleNameRecoveryStillBuildsValidatedDocument) {
  auto document = updateDocument(
      "pipeline-missing-module-name.calc",
      "module \n"
      "\n"
      "def a :4*6;\n");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->state, workspace::DocumentState::Validated);
  ASSERT_TRUE(document->hasAst());
  ASSERT_TRUE(document->parseRecovered());
  ASSERT_FALSE(document->parseResult.parseDiagnostics.empty());
  ASSERT_FALSE(document->diagnostics.empty());
  EXPECT_GT(parseDiagnosticCount(*document), 0u);
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingDefinitionExpressionBeforeSemicolonLeavesRecoveredHole) {
  auto document = updateDocument(
      "pipeline-missing-definition-expr.calc",
      "module demo\n"
      "\n"
      "def a :;\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  ASSERT_EQ(module->statements.size(), 1u);

  auto *definition =
      dynamic_cast<arithmetics::ast::Definition *>(module->statements.front());
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(definition->name, "a");
  EXPECT_EQ(definition->expr, nullptr);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expression"));
  EXPECT_FALSE(test::has_diagnostic_message(*document, "Expecting `(`"));
  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "token of type 'DeclaredParameter'"));
  EXPECT_FALSE(test::has_diagnostic_message(*document, "Expecting `)`"));
  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unknown reference in function call."));
}

TEST_F(DocumentPipelineIntegrationTest,
       RecoveredFunctionCallKeepsValidArgumentPrefix) {
  auto document = updateDocument(
      "pipeline-recovered-call-prefix.calc",
      "module basicMath\n"
      "\n"
      "def root(x, y):\n"
      "    x^(1/y);\n"
      "\n"
      "def sqrt(x):\n"
      "    root(x, 2);\n"
      "\n"
      "Sqrt(81/);\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());
  EXPECT_FALSE(
      test::has_diagnostic_message(*document,
                                   "Function sqrt expects 1 parameters, but 0 "
                                   "were given."));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Unexpected token `/`"));
}

TEST_F(DocumentPipelineIntegrationTest,
       EmptyFunctionCallWithTrailingCommentPublishesMissingExpressionAndCurrentValidationLeak) {
  auto document = updateDocument(
      "pipeline-empty-call.calc",
      "module basicMath\n"
      "\n"
      "def root(x, y):\n"
      "    x^(1/y);\n"
      "\n"
      "def sqrt(x):\n"
      "    root(x, 2);\n"
      "\n"
      "Sqrt(); // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  EXPECT_EQ(parseDiagnosticCount(*document), 1u) << parseDump;
  EXPECT_EQ(document->diagnostics.size(), 2u) << parseDump;
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expression"))
      << parseDump;
  EXPECT_FALSE(test::has_diagnostic_message(*document, "Expecting )"))
      << parseDump;
  EXPECT_FALSE(test::has_diagnostic_message(*document, "Unexpected token `()`"))
      << parseDump;
  EXPECT_TRUE(test::has_diagnostic_message(
      *document, "Function sqrt expects 1 parameters, but 0 were given."))
      << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonAtEndOfEvaluationPublishesTerminatorInDiagnostic) {
  auto document = updateDocument(
      "pipeline-missing-semicolon-eof.calc",
      "module name\n"
      "\n"
      "3 + 5");

  ASSERT_NE(document, nullptr);

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message == "Expecting ;";
  })) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       EmptyFunctionCallWithoutSemicolonKeepsCallPrefixAndPublishesMissingParts) {
  auto document = updateDocument(
      "pipeline-empty-call-missing-semicolon.calc",
      "module basicMath\n"
      "\n"
      "def root(x, y):\n"
      "    x^(1/y);\n"
      "\n"
      "def sqrt(x):\n"
      "    root(x, 2);\n"
      "\n"
      "Sqrt() // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expression"))
      << parseDump;
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expecting ;"))
      << parseDump;
  EXPECT_FALSE(test::has_diagnostic_message(*document, "Unexpected token `Sqrt`"))
      << parseDump;
  EXPECT_FALSE(test::has_diagnostic_message(*document, "named 'qrt'"))
      << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonBeforeHiddenGapAnchorsAfterPreviousVisibleToken) {
  auto document = updateDocument(
      "pipeline-missing-semicolon-before-gap.calc",
      "module mod\n"
      "def a : 0\n"
      "\n"
      "def ID : 0 ;\n");

  ASSERT_NE(document, nullptr);

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  const auto nextDefinitionOffset = document->textDocument().getText().find("def ID");
  ASSERT_FALSE(diagnostics.empty());
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message.starts_with("Expecting ;") ||
           diagnostic->message.find("Unexpected token") != std::string::npos;
  })) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      diagnostics, [nextDefinitionOffset](const auto *diagnostic) {
        return diagnostic->message.starts_with("Expecting ;") &&
                   diagnostic->begin == diagnostic->end ||
               (diagnostic->message.find("Unexpected token") !=
                    std::string::npos &&
                nextDefinitionOffset != std::string::npos &&
                diagnostic->begin <
                    static_cast<pegium::TextOffset>(nextDefinitionOffset));
      })) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonBeforeTrailingCommentAnchorsAfterPreviousVisibleToken) {
  const std::string text =
      "module basicMath\n"
      "def a: 5 //aa\n";
  auto document =
      updateDocument("pipeline-missing-semicolon-before-comment.calc", text);

  ASSERT_NE(document, nullptr);

  const auto diagnostics = parseDiagnostics(*document);
  ASSERT_FALSE(diagnostics.empty());
  const auto expectedOffset =
      static_cast<TextOffset>(std::string_view{
          "module basicMath\n"
          "def a: 5"}
                                  .size());
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [expectedOffset](const auto *diagnostic) {
    return diagnostic->message == "Expecting ;" &&
           diagnostic->begin == expectedOffset &&
           diagnostic->end == expectedOffset;
  }));
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonBeforeBrokenCallKeepsFollowingCallRecoveryLocal) {
  auto document = updateDocument(
      "pipeline-missing-semicolon-before-broken-call.calc",
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
      "xx\n"
      "\n"
      "Root(64 3/0); // 4\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expecting ;") ||
              test::has_diagnostic_message(*document, "Unexpected token `xx`"))
      << parseDump;

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr) << parseDump;
  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastCall = dynamic_cast<arithmetics::ast::FunctionCall *>(
      lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr) << parseDump;
  EXPECT_EQ(lastCall->func.getRefText(), "root");
  EXPECT_FALSE(lastCall->args.empty());
}

TEST_F(DocumentPipelineIntegrationTest,
       LongDeleteRunCanRecoverToNextVisibleStatementBoundary) {
  const std::string text =
      "module basicMath\n"
      "\n"
      "def a: 5; // comment\n"
      "\n"
      "2*7+++++++++;\n"
      "\n"
      "\n"
      "def b: 5;\n";
  auto document = updateDocument(
      "pipeline-long-delete-run.calc", text,
      std::chrono::milliseconds(5000));

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());
  EXPECT_TRUE(std::ranges::any_of(document->diagnostics, [](const auto &diagnostic) {
    return diagnostic.message.find("+++++++++") != std::string::npos;
  }));
  EXPECT_FALSE(std::ranges::any_of(document->diagnostics, [](const auto &diagnostic) {
    return diagnostic.message.find("2*7+++++++++;") != std::string::npos;
  }));
}

TEST_F(DocumentPipelineIntegrationTest,
       LongDeleteRunBeyondDefaultBudgetCanRecoverToNextVisibleStatementBoundary) {
  const std::string text =
      "module basicMath\n"
      "\n"
      "def a: 5; // comment\n"
      "\n"
      "2*7+++++++++++++++++++++++++++++++++++;\n"
      "\n"
      "def b: 5;\n";
  auto document = updateDocument("pipeline-long-delete-budget.calc", text,
                                 std::chrono::milliseconds(10000));

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());
  EXPECT_TRUE(std::ranges::any_of(document->diagnostics, [](const auto &diagnostic) {
    return diagnostic.message.find("+++++++++++++++++++++++++++++++++++") !=
           std::string::npos;
  }));
  EXPECT_FALSE(std::ranges::any_of(document->diagnostics, [](const auto &diagnostic) {
    return diagnostic.message.find("2*7+++++++++++++++++++++++++++++++++++;") !=
           std::string::npos;
  }));
}

TEST_F(DocumentPipelineIntegrationTest,
       MalformedStandaloneOperatorRunKeepsFollowingStatementBoundary) {
  const std::string text =
      "module m\n"
      "\n"
      "def b: 3;\n"
      "b % 2;\n"
      "2*********\n"
      "\n"
      "b % 2;\n";
  auto document = updateDocument("pipeline-malformed-operator-run.calc", text);

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  EXPECT_EQ(module->statements.size(), 3u)
      << dumpDiagnostics(parseDiagnostics(*document));
  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr) << dumpDiagnostics(parseDiagnostics(*document));
  auto *lastBinary = dynamic_cast<arithmetics::ast::BinaryExpression *>(
      lastEvaluation->expression);
  ASSERT_NE(lastBinary, nullptr) << dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(lastBinary->op, "%") << dumpDiagnostics(parseDiagnostics(*document));
}

TEST_F(DocumentPipelineIntegrationTest,
       LongMalformedStandaloneOperatorRunKeepsMultipleFollowingStatementsBoundary) {
  const std::string operatorRun(95u, '*');
  const std::string text =
      "module m\n"
      "\n"
      "b % 2;\n"
      "2" + operatorRun + "\n"
      "\n"
      "b % 2;\n"
      "b % 2;\n";
  auto document =
      updateDocument("pipeline-malformed-operator-run-long.calc", text,
                     std::chrono::milliseconds(60000));

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  const auto parseDump = dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(module->statements.size(), 3u) << parseDump;

  const auto malformedOffset =
      text.find("2" + operatorRun);
  ASSERT_NE(malformedOffset, std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(
      parseDiagnostics(*document), [malformedOffset, &operatorRun](const auto *diagnostic) {
        return diagnostic->message.find("Unexpected token `2") != std::string::npos &&
               diagnostic->begin == static_cast<TextOffset>(malformedOffset) &&
               diagnostic->end ==
                   static_cast<TextOffset>(malformedOffset + operatorRun.size() + 1u);
      }))
      << parseDump;

  ASSERT_GE(module->statements.size(), 2u) << parseDump;
  for (std::size_t index = module->statements.size() - 2u;
       index < module->statements.size(); ++index) {
    auto *evaluation =
        dynamic_cast<arithmetics::ast::Evaluation *>(module->statements[index]);
    ASSERT_NE(evaluation, nullptr) << parseDump;
    auto *binary =
        dynamic_cast<arithmetics::ast::BinaryExpression *>(evaluation->expression);
    ASSERT_NE(binary, nullptr) << parseDump;
    EXPECT_EQ(binary->op, "%") << parseDump;
  }
}

TEST_F(DocumentPipelineIntegrationTest,
       VeryLongOperatorRunDoesNotBleedAcrossFollowingStatements) {
  const std::string operatorRun(95u, '*');
  const std::string text =
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
      "b % 2; //aaa\n"
      "2" + operatorRun + "\n"
      "\n"
      "b % 2;\n"
      "b % 2;\n";
  auto document =
      updateDocument("pipeline-very-long-operator-run-no-bleed.calc", text,
                     std::chrono::milliseconds(60000));

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  const auto parseDump = dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(module->statements.size(), 10u) << parseDump;

  const auto starsPos = text.find(operatorRun);
  ASSERT_NE(starsPos, std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(
      parseDiagnostics(*document), [starsPos, &operatorRun](const auto *diagnostic) {
        return diagnostic->message.find("Unexpected token `2") != std::string::npos &&
               diagnostic->begin == static_cast<TextOffset>(starsPos - 1u) &&
               diagnostic->end ==
                   static_cast<TextOffset>(starsPos + operatorRun.size());
      }));

  ASSERT_GE(module->statements.size(), 2u) << parseDump;
  for (std::size_t index = module->statements.size() - 2u;
       index < module->statements.size(); ++index) {
    auto *evaluation =
        dynamic_cast<arithmetics::ast::Evaluation *>(module->statements[index]);
    ASSERT_NE(evaluation, nullptr) << parseDump;
    auto *binary =
        dynamic_cast<arithmetics::ast::BinaryExpression *>(evaluation->expression);
    ASSERT_NE(binary, nullptr) << parseDump;
    EXPECT_EQ(binary->op, "%") << parseDump;
  }
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonBeforeNextStatementPublishesGenericParseDiagnostic) {
  auto document = updateDocument(
      "pipeline-missing-separator.calc",
      "module name\n"
      "\n"
      "2  \n"
      "\n"
      "222222 * 7;\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  const auto diagnostics = parseDiagnostics(*document);
  const auto parseDump = dumpDiagnostics(diagnostics);
  ASSERT_FALSE(diagnostics.empty()) << parseDump;
  const auto expectedStatementOffset = static_cast<TextOffset>(
      std::string_view{"module name\n\n"}.size());
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto *diagnostic) {
    return diagnostic->message.starts_with("Expecting ;") ||
           diagnostic->message.find("Unexpected token") != std::string::npos;
  })) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      diagnostics, [expectedStatementOffset](const auto *diagnostic) {
        return diagnostic->begin == expectedStatementOffset ||
               diagnostic->begin == expectedStatementOffset + 1u;
  })) << parseDump;
}

TEST_F(DocumentPipelineIntegrationTest,
       MissingSemicolonBeforeCallStatementKeepsFollowingStatementAndValidation) {
  auto document = updateDocument(
      "pipeline-missing-semicolon-before-call.calc",
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
      "xxx\n"
      "\n"
      "Sqrt(1/0); // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  EXPECT_GE(module->statements.size(), 11u)
      << dumpDiagnostics(parseDiagnostics(*document));

  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr)
      << dumpDiagnostics(parseDiagnostics(*document));
  auto *lastCall = dynamic_cast<arithmetics::ast::FunctionCall *>(
      lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr) << dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(lastCall->func.getRefText(), "sqrt");
  EXPECT_EQ(lastCall->args.size(), 1u);

  EXPECT_FALSE(test::has_diagnostic_message(*document, "Unexpected token `Sqrt(1/0)`"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Unexpected token `xxx`") ||
              test::has_diagnostic_message(*document, "named 'xxx'"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
}

TEST_F(DocumentPipelineIntegrationTest,
       SeveralRecoveredIdentifierLinesStillKeepFollowingCallStatement) {
  auto document = updateDocument(
      "pipeline-several-recovered-lines-before-call.calc",
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
      "\n"
      "xxxxxxx\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "Sqrt(81/0); // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  EXPECT_GE(module->statements.size(), 11u)
      << dumpDiagnostics(parseDiagnostics(*document));

  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr)
      << dumpDiagnostics(parseDiagnostics(*document));
  auto *lastCall = dynamic_cast<arithmetics::ast::FunctionCall *>(
      lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr) << dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(lastCall->func.getRefText(), "sqrt");
  EXPECT_EQ(lastCall->args.size(), 1u);

  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unexpected token `Sqrt(81/0)`"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "xxxxx"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "xxxxxxx"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
}

TEST_F(DocumentPipelineIntegrationTest,
       RecoveredIdentifierAndOperatorLinesStillPublishLateParseDiagnostics) {
  auto document = updateDocument(
      "pipeline-recovered-identifier-and-operator-lines.calc",
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
      "xxxxxxx\n"
      "<<<\n"
      "\n"
      "\n"
      "sada;\n"
      "\n"
      "\n"
      "Sqrt(81/0); // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded())
      << dumpDiagnostics(parseDiagnostics(*document));
  ASSERT_TRUE(document->parseRecovered())
      << dumpDiagnostics(parseDiagnostics(*document));

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  const auto parseDump = dumpDiagnostics(parseDiagnostics(*document));
  const auto lateGarbageOffset =
      document->textDocument().getText().find("<<<");
  const auto lateCallOffset =
      document->textDocument().getText().find("Sqrt(81/0);");

  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expecting ;"))
      << parseDump;
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Unexpected token `<<<`"))
      << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      parseDiagnostics(*document), [lateGarbageOffset](const auto *diagnostic) {
        return lateGarbageOffset != std::string::npos &&
               diagnostic->begin >=
                   static_cast<pegium::TextOffset>(lateGarbageOffset);
      }))
      << parseDump;
  (void)lateCallOffset;
}

TEST_F(DocumentPipelineIntegrationTest,
       RecoveredIdentifierOperatorAndAngleLinesStillKeepLateCallAndValidation) {
  auto document = updateDocument(
      "pipeline-recovered-identifier-operator-angle-lines.calc",
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
      "Sqrt(81/0); // 9\n");

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded())
      << dumpDiagnostics(parseDiagnostics(*document));
  ASSERT_TRUE(document->parseRecovered())
      << dumpDiagnostics(parseDiagnostics(*document));

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  const auto parseDump = dumpDiagnostics(parseDiagnostics(*document));
  const auto lateGarbageOffset =
      document->textDocument().getText().find("<<<<<<<<<<<<");
  EXPECT_GE(module->statements.size(), 14u) << parseDump;

  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastCall = dynamic_cast<arithmetics::ast::FunctionCall *>(
      lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr) << parseDump;
  EXPECT_EQ(lastCall->func.getRefText(), "sqrt") << parseDump;
  ASSERT_EQ(lastCall->args.size(), 1u) << parseDump;

  EXPECT_TRUE(test::has_diagnostic_message(*document, "Expecting ;"))
      << parseDump;
  EXPECT_TRUE(
      test::has_diagnostic_message(*document, "Unexpected token `<<<<<<<<<<<<`"))
      << parseDump;
  EXPECT_TRUE(std::ranges::any_of(
      parseDiagnostics(*document), [lateGarbageOffset](const auto *diagnostic) {
        return lateGarbageOffset != std::string::npos &&
               diagnostic->begin >=
                   static_cast<pegium::TextOffset>(lateGarbageOffset);
      }))
      << parseDump;
  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unexpected token `Sqrt(81/0)`"))
      << parseDump;
  EXPECT_TRUE(test::has_diagnostic_message(*document, "named 'sada'"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
}

TEST_F(DocumentPipelineIntegrationTest,
       RecoveredEmptyCallOpenGroupAndMalformedCallsKeepFollowingStatements) {
  auto document = updateDocument(
      "pipeline-empty-call-open-group-and-malformed-calls.calc",
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
      "Sqrt()\n"
      "(\n"
      "root(2,);\n"
      "root(,2);\n"
      "\n"
      "sada;\n"
      "Sqrt(81/0); // 9\n",
      std::chrono::milliseconds(5000));

  ASSERT_NE(document, nullptr);
  ASSERT_TRUE(document->parseSucceeded())
      << dumpDiagnostics(parseDiagnostics(*document));
  ASSERT_TRUE(document->parseRecovered())
      << dumpDiagnostics(parseDiagnostics(*document));

  auto *module =
      dynamic_cast<arithmetics::ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);

  auto *lastEvaluation =
      dynamic_cast<arithmetics::ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr)
      << dumpDiagnostics(parseDiagnostics(*document));
  auto *lastCall =
      dynamic_cast<arithmetics::ast::FunctionCall *>(
          lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr)
      << dumpDiagnostics(parseDiagnostics(*document));
  EXPECT_EQ(lastCall->func.getRefText(), "sqrt");
  ASSERT_EQ(lastCall->args.size(), 1u);

  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unexpected token `sada`"));
  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unexpected token `Sqrt(81/0)`"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "named 'sada'"));
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
}

TEST_F(DocumentPipelineIntegrationTest,
       RecoveredDocumentStillPublishesLateValidationDiagnostic) {
  auto document = updateDocument(
      "pipeline-recovered-late-validation.calc",
      "module calc\n"
      "2*4+7-7/1*9-711*69*22;v\n"
      "2*4+7-7/1*9-711*69*22;\n"
      "2*4+7-7/1*9-711*69*22;\n"
      "2*4+7-7/1*9-711*69*22;\n"
      "2*4+7-7/1*9-711*69*22;\n"
      "2*4+7-7/1*9-711*69*22/0;\n");

  ASSERT_NE(document, nullptr);
  ASSERT_EQ(document->state, workspace::DocumentState::Validated);
  ASSERT_TRUE(document->parseSucceeded());
  ASSERT_TRUE(document->parseRecovered());
  EXPECT_GT(parseDiagnosticCount(*document), 0u);
  EXPECT_TRUE(test::has_diagnostic_message(*document, "Division by zero"));
  EXPECT_FALSE(
      test::has_diagnostic_message(*document, "Unknown reference in function call."));
}

} // namespace
} // namespace pegium::integration
