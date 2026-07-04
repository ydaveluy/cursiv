#include <gtest/gtest.h>

#include <memory>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>

#include <arithmetics/lsp/LspModule.hpp>
#include <arithmetics/lsp/LspServices.hpp>

#include <lsp/json/json.h>

#include <pegium/core/references/DefaultNameProvider.hpp>
#include <arithmetics/lsp/ArithmeticsCodeActionProvider.hpp>
#include <arithmetics/lsp/ArithmeticsFormatter.hpp>
#include <arithmetics/core/validation/ArithmeticsValidator.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/lsp/support/JsonValue.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>

namespace arithmetics::tests {
namespace {
constexpr std::string_view kDefaultCodeActionsKey = "pegiumDefaultCodeActions";

[[nodiscard]] std::filesystem::path make_temp_directory() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto path = std::filesystem::temp_directory_path() /
              ("pegium-arithmetics-tests-" + suffix);
  std::filesystem::create_directories(path);
  return path;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

const pegium::Diagnostic *
find_diagnostic(const pegium::workspace::Document &document,
                std::string_view message) {
  for (const auto &diagnostic : document.diagnostics) {
    if (diagnostic.message.find(message) != std::string::npos) {
      return &diagnostic;
    }
  }
  return nullptr;
}

std::vector<const pegium::Diagnostic *>
find_diagnostics(const pegium::workspace::Document &document,
                 std::string_view message) {
  std::vector<const pegium::Diagnostic *> diagnostics;
  for (const auto &diagnostic : document.diagnostics) {
    if (diagnostic.message.find(message) != std::string::npos) {
      diagnostics.push_back(&diagnostic);
    }
  }
  return diagnostics;
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

::lsp::Diagnostic make_default_recovery_diagnostic() {
  pegium::JsonValue::Object action;
  action.try_emplace("kind", "quickfix");
  action.try_emplace("editKind", "delete");
  action.try_emplace("title", "Delete unexpected text");
  action.try_emplace("begin", static_cast<std::int64_t>(0));
  action.try_emplace("end", static_cast<std::int64_t>(1));
  action.try_emplace("newText", "");

  pegium::JsonValue::Array actions;
  actions.emplace_back(std::move(action));

  pegium::JsonValue::Object data;
  data.try_emplace(std::string(kDefaultCodeActionsKey), std::move(actions));

  ::lsp::Diagnostic diagnostic{};
  diagnostic.message = "recovery";
  diagnostic.data = pegium::to_lsp_any(
      pegium::JsonValue(std::move(data)));
  return diagnostic;
}

TEST(ArithmeticsModuleTest, InstallsLanguageSpecificOverrides) {
  static_assert(std::is_base_of_v<pegium::Services,
                                  arithmetics::ArithmeticsServices>);
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->validator, nullptr);
  EXPECT_NE(services->references.scopeProvider, nullptr);
  EXPECT_NE(services->references.linker, nullptr);
  EXPECT_NE(dynamic_cast<pegium::references::DefaultNameProvider *>(
                services->references.nameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<arithmetics::ArithmeticsCodeActionProvider *>(
                services->lsp.codeActionProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<arithmetics::ArithmeticsFormatter *>(
                services->lsp.formatter.get()),
            nullptr);

  static_assert(std::is_base_of_v<pegium::NamedAstNode, arithmetics::ast::Definition>);
  arithmetics::ast::Definition definition;
  definition.name = "value";
  EXPECT_EQ(services->references.nameProvider->getName(definition),
            (std::optional<std::string>{"value"}));
}

TEST(ArithmeticsModuleTest, RegistersLanguagesAndPublishesValidationDiagnostic) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));
  EXPECT_NO_THROW((void)shared->serviceRegistry->getServices(
      pegium::test::make_file_uri("arithmetics-module.calc")));
  EXPECT_EQ(shared->serviceRegistry->findServices(
                pegium::test::make_file_uri("arithmetics-module.txt")),
            nullptr);

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-module.calc"),
      "arithmetics",
      "module Demo\n"
      "def value: 1 / 0;\n");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(pegium::test::has_diagnostic_message(*document, "Division by zero"));
}

TEST(ArithmeticsModuleTest, CreateLanguageServicesReturnsTypedServices) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);

  auto services =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  ASSERT_NE(services, nullptr);
  EXPECT_NE(services->validator, nullptr);
}

TEST(ArithmeticsModuleTest, CliEvalReturnsCodeZeroAndPrintsEvaluations) {
  const auto tempDirectory = make_temp_directory();
  const auto outputFile = tempDirectory / "arithmetics-cli.out";
  const auto inputPath = tempDirectory / "arithmetics-cli-input.calc";
  {
    std::ofstream out(inputPath, std::ios::binary);
    out << "Module basicMath\n"
           "\n"
           "def a: 5;\n"
           "def b: 3;\n"
           "def c: a + b; // 8\n"
           "\n"
           "2 * c; // 16\n";
  }
  const std::string command =
      std::string("\"") + PEGIUM_EXAMPLE_ARITHMETICS_CLI_PATH +
      "\" eval \"" + inputPath.string() + "\" > \"" + outputFile.string() +
      "\" 2>&1";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 0);

  const auto output = read_file(outputFile);
  EXPECT_NE(output.find("line "), std::string::npos);
  EXPECT_NE(output.find("===>"), std::string::npos);
  EXPECT_NE(output.find("2 * c"), std::string::npos);
  EXPECT_NE(output.find("16"), std::string::npos);

  std::filesystem::remove_all(tempDirectory);
}

TEST(ArithmeticsModuleTest, CliEvalRejectsUnexpectedFileExtensionCleanly) {
  const auto tempDirectory = make_temp_directory();
  const auto outputFile = tempDirectory / "arithmetics-cli-invalid.out";
  const auto inputPath = tempDirectory / "invalid.txt";
  {
    std::ofstream out(inputPath, std::ios::binary);
    out << "module Demo\n"
           "def a: 1;\n";
  }

  const std::string command =
      std::string("\"") + PEGIUM_EXAMPLE_ARITHMETICS_CLI_PATH +
      "\" eval \"" + inputPath.string() + "\" > \"" + outputFile.string() +
      "\" 2>&1";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 3 << 8);

  const auto output = read_file(outputFile);
  EXPECT_NE(
      output.find("Please choose a file with one of these extensions: .calc."),
      std::string::npos);

  std::filesystem::remove_all(tempDirectory);
}

TEST(ArithmeticsModuleTest, DoesNotWarnWhenDefinitionIsAlreadyAConstantLiteral) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-constant-literal.calc"),
      "arithmetics",
      "module Demo\n"
      "def a: 5;\n");

  ASSERT_NE(document, nullptr);

  const auto hasNormalizationDiagnostic = std::ranges::any_of(
      document->diagnostics, [](const auto &diagnostic) {
        return diagnostic.code.has_value() &&
               std::holds_alternative<std::string>(*diagnostic.code) &&
               std::get<std::string>(*diagnostic.code) ==
                   arithmetics::validation::IssueCodes::ExpressionNormalizable;
      });
  EXPECT_FALSE(hasNormalizationDiagnostic);
}

TEST(ArithmeticsModuleTest, DivisionByZeroTargetsRightOperand) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-div-zero.calc"),
      "arithmetics",
      "module test\n"
      "5 / 0;\n");

  ASSERT_NE(document, nullptr);
  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *evaluation = dynamic_cast<ast::Evaluation *>(module->statements[0]);
  ASSERT_NE(evaluation, nullptr);
  auto *binary = dynamic_cast<ast::BinaryExpression *>(evaluation->expression);
  ASSERT_NE(binary, nullptr);

  const auto *diagnostic =
      find_diagnostic(*document, "Division by zero is detected.");
  ASSERT_NE(diagnostic, nullptr);
  const auto [begin, end] =
      pegium::validation::range_for_feature<&ast::BinaryExpression::right>(*binary);
  EXPECT_EQ(diagnostic->begin, begin);
  EXPECT_EQ(diagnostic->end, end);
}

TEST(ArithmeticsModuleTest, ModuloByZeroIsDetected) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-mod-zero.calc"),
      "arithmetics",
      "module test\n"
      "5 % 0;\n");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(
      pegium::test::has_diagnostic_message(*document, "Division by zero is detected."));
}

TEST(ArithmeticsModuleTest, ExpressionNormalizationUsesExpectedIssueCode) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-normalizable.calc"),
      "arithmetics",
      "module test\n"
      "def test: 2 + 3;\n");

  ASSERT_NE(document, nullptr);
  const auto *diagnostic =
      find_diagnostic(*document, "Expression could be normalized to constant 5");
  ASSERT_NE(diagnostic, nullptr);
  ASSERT_TRUE(diagnostic->code.has_value());
  ASSERT_TRUE(std::holds_alternative<std::string>(*diagnostic->code));
  EXPECT_EQ(std::get<std::string>(*diagnostic->code),
            arithmetics::validation::IssueCodes::ExpressionNormalizable);
  ASSERT_TRUE(diagnostic->data.has_value());
  ASSERT_TRUE(diagnostic->data->isObject());
  const auto constant = diagnostic->data->object().find("constant");
  ASSERT_NE(constant, diagnostic->data->object().end());
  ASSERT_TRUE(constant->second.isNumber());
  EXPECT_EQ(constant->second.number(), 5.0);
}

TEST(ArithmeticsModuleTest, DuplicateDefinitionNamesAreDetectedOnNameRanges) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-duplicate-def.calc"),
      "arithmetics",
      "module test\n"
      "def x: 1;\n"
      "def x: 2;\n");

  ASSERT_NE(document, nullptr);
  auto diagnostics = find_diagnostics(*document, "Duplicate definition name: x");
  ASSERT_EQ(diagnostics.size(), 2u);

  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *first = dynamic_cast<ast::Definition *>(module->statements[0]);
  auto *second = dynamic_cast<ast::Definition *>(module->statements[1]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  const auto [firstBegin, firstEnd] =
      pegium::validation::range_for_feature<&ast::Definition::name>(*first);
  const auto [secondBegin, secondEnd] =
      pegium::validation::range_for_feature<&ast::Definition::name>(*second);
  EXPECT_EQ(diagnostics[0]->begin, firstBegin);
  EXPECT_EQ(diagnostics[0]->end, firstEnd);
  EXPECT_EQ(diagnostics[1]->begin, secondBegin);
  EXPECT_EQ(diagnostics[1]->end, secondEnd);
}

TEST(ArithmeticsModuleTest, DirectFunctionRecursionIsReportedOnFunctionReference) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-direct-recursion.calc"),
      "arithmetics",
      "module test\n"
      "def factorial(n): factorial(n - 1);\n");

  ASSERT_NE(document, nullptr);
  const auto *diagnostic =
      find_diagnostic(*document, "Recursion is not allowed [factorial()]");
  ASSERT_NE(diagnostic, nullptr);

  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *definition = dynamic_cast<ast::Definition *>(module->statements[0]);
  ASSERT_NE(definition, nullptr);
  auto *call = dynamic_cast<ast::FunctionCall *>(definition->expr);
  ASSERT_NE(call, nullptr);
  const auto [begin, end] =
      pegium::validation::range_for_feature<&ast::FunctionCall::func>(*call);
  EXPECT_EQ(diagnostic->begin, begin);
  EXPECT_EQ(diagnostic->end, end);
}

TEST(ArithmeticsModuleTest, MutualFunctionRecursionReportsBothCalls) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-mutual-recursion.calc"),
      "arithmetics",
      "module test\n"
      "def even(n): odd(n - 1);\n"
      "def odd(n): even(n - 1);\n");

  ASSERT_NE(document, nullptr);
  auto diagnostics = find_diagnostics(*document, "Recursion is not allowed [");
  ASSERT_EQ(diagnostics.size(), 2u);
}

TEST(ArithmeticsModuleTest, DuplicateParameterNamesMatchExpectedMessage) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-duplicate-param.calc"),
      "arithmetics",
      "module test\n"
      "def test(x, x): x + 1;\n");

  ASSERT_NE(document, nullptr);
  auto diagnostics = find_diagnostics(*document, "Duplicate definition name: x");
  ASSERT_EQ(diagnostics.size(), 2u);
}

TEST(ArithmeticsModuleTest, FunctionCallArityMismatchTargetsArgumentsRange) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-arity-mismatch.calc"),
      "arithmetics",
      "module test\n"
      "def add(a, b): a + b;\n"
      "add(1, 2, 3);\n");

  ASSERT_NE(document, nullptr);
  const auto *diagnostic =
      find_diagnostic(*document, "Function add expects 2 parameters, but 3 were given.");
  ASSERT_NE(diagnostic, nullptr);

  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *evaluation = dynamic_cast<ast::Evaluation *>(module->statements[1]);
  ASSERT_NE(evaluation, nullptr);
  auto *call = dynamic_cast<ast::FunctionCall *>(evaluation->expression);
  ASSERT_NE(call, nullptr);
  const auto [begin, firstEndIgnored] =
      pegium::validation::range_for_feature(*call, "args", 0u);
  (void)firstEndIgnored;
  const auto [lastBeginIgnored, end] = pegium::validation::range_for_feature(
      *call, "args", call->args.size() - 1u);
  (void)lastBeginIgnored;
  EXPECT_EQ(diagnostic->begin, begin);
  EXPECT_EQ(diagnostic->end, end);
}

TEST(ArithmeticsModuleTest, ValidFunctionCallsDoNotEmitDiagnostics) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-valid-call.calc"),
      "arithmetics",
      "module test\n"
      "def add(a, b): a + b;\n"
      "add(1, 2);\n");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(document->diagnostics.empty());
}

TEST(ArithmeticsModuleTest, InitializeResultSerializesForLspTransport) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));
  ASSERT_NE(shared->lsp.languageServer, nullptr);

  ::lsp::InitializeParams params{};
  params.rootUri = ::lsp::DocumentUri(
      ::lsp::Uri::parse(pegium::test::make_file_uri("arithmetics-root")));
  params.workspaceFolders = ::lsp::Array<::lsp::WorkspaceFolder>{};

  auto result = shared->lsp.languageServer->initialize(params);

  EXPECT_NO_THROW({
    const auto json = ::lsp::toJson(std::move(result));
    EXPECT_FALSE(json.isNull());
  });
}

TEST(ArithmeticsModuleTest, FormatterFormatsToExpectedText) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  ASSERT_NE(registeredServices, nullptr);
  shared->serviceRegistry->registerServices(std::move(registeredServices));

  struct Case {
    const char *name;
    std::string_view uri;
    std::string_view input;
    std::string_view expected;
  };
  static const Case cases[] = {
      {"CompactModule", "arithmetics-format.calc",
       "module demo def value:1+2;def root(x,y):x^(1/y);root(value,3);",
       "module demo\n"
       "def value: 1 + 2;\n"
       "def root(x, y): x ^ (1 / y);\n"
       "root(value, 3);"},
      {"BinaryOperatorSpacing", "arithmetics-operators.calc",
       "module demo def value:2 +          8 * 7 / 0 -             9 * 7;",
       "module demo\n"
       "def value: 2 + 8 * 7 / 0 - 9 * 7;"},
      {"RootIndentationWithLeadingComments", "arithmetics-root-indent.calc",
       "            /** module comment */\n"
       "        Module basicMath\n"
       "\n"
       " \n"
       "/** test */\n"
       "def c: 2 * 5;\n",
       "/** module comment */\n"
       "Module basicMath\n"
       "\n"
       " \n"
       "/** test */\n"
       "def c: 2 * 5;\n"},
      {"DocCommentsAndTags", "arithmetics-doc-comment.calc",
       "module demo\n"
       "/**\n"
       "*   Adds   numbers.\n"
       "* @param   x   first   value\n"
       "*   more   details\n"
       "*/\n"
       "def add(x):x+1;",
       "module demo\n"
       "/**\n"
       " * Adds numbers.\n"
       " * @param x first value\n"
       " *  more details\n"
       " */\n"
       "def add(x): x + 1;"},
  };

  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);

    auto document = pegium::test::open_and_build_document(
        *shared, pegium::test::make_file_uri(std::string(c.uri)), "arithmetics",
        std::string(c.input));
    ASSERT_NE(document, nullptr);

    const auto *coreServices =
        &shared->serviceRegistry->getServices(document->uri);
    const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
    ASSERT_NE(services, nullptr);
    ASSERT_NE(services->lsp.formatter, nullptr);

    ::lsp::DocumentFormattingParams params{};
    params.options.insertSpaces = true;
    params.options.tabSize = 2;
    const auto edits = services->lsp.formatter->formatDocument(
        *document, params, pegium::utils::default_cancel_token);

    ASSERT_FALSE(edits.empty());
    EXPECT_EQ(apply_text_edits(*document, edits), c.expected);
  }
}

TEST(ArithmeticsModuleTest, CodeActionsKeepRecoveryAndLanguageSpecificFixes) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  ASSERT_NE(registeredServices, nullptr);
  shared->serviceRegistry->registerServices(std::move(registeredServices));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("arithmetics-code-actions.calc"),
      "arithmetics", "1+2;");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.codeActionProvider, nullptr);

  ::lsp::CodeActionParams params{};
  params.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(document->uri));
  // Cursor-only request: each quick-fix must size its edit from its own
  // diagnostic range, not the (empty) request range.
  params.range.start = document->textDocument().positionAt(0);
  params.range.end = document->textDocument().positionAt(0);
  params.context.diagnostics.push_back(make_default_recovery_diagnostic());

  ::lsp::Diagnostic normalization{};
  normalization.code = ::lsp::OneOf<int, ::lsp::String>(
      ::lsp::String(
          std::string(arithmetics::validation::IssueCodes::ExpressionNormalizable)));
  normalization.range.start = document->textDocument().positionAt(0);
  normalization.range.end = document->textDocument().positionAt(3);
  ::lsp::LSPObject normalizationData{};
  normalizationData["constant"] = ::lsp::LSPAny(3.0);
  normalization.data = std::move(normalizationData);
  params.context.diagnostics.push_back(std::move(normalization));

  const auto actions = services->lsp.codeActionProvider->getCodeActions(
      *document, params, pegium::utils::default_cancel_token);

  ASSERT_TRUE(actions.has_value());
  ASSERT_EQ(actions->size(), 2u);
  EXPECT_EQ(std::get<::lsp::CodeAction>((*actions)[0]).title,
            "Delete unexpected text");
  EXPECT_EQ(std::get<::lsp::CodeAction>((*actions)[1]).title,
            "Replace with constant 3");
}

} // namespace
} // namespace arithmetics::tests
