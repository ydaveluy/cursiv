#include <gtest/gtest.h>

#include <memory>

#include <arithmetics/lsp/LspModule.hpp>
#include <arithmetics/core/CoreModule.hpp>
#include <arithmetics/core/Language.hpp>

#include "LanguageTestSupport.hpp"

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/lsp/services/Services.hpp>

#include <functional>
#include <string>

namespace arithmetics::tests {
namespace {
TEST(ArithmeticsLanguageTest,
     RecoveryAfterMissingSemicolonKeepsFollowingBrokenCallLocal) {
  auto parser = createArithmeticsParser();
  const std::string prefix =
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
      "\n";
  const std::string suffix =
      "\n"
      "Root(64 3/0); // 4\n";
  auto missingSemicolonDocument = pegium::test::parse_document(
      *parser, prefix + "xx" + suffix,
      pegium::test::make_file_uri("missing-semicolon-before-broken-call.calc"),
      "arithmetics");
  auto explicitSemicolonDocument = pegium::test::parse_document(
      *parser, prefix + "xx;" + suffix,
      pegium::test::make_file_uri("explicit-semicolon-before-broken-call.calc"),
      "arithmetics");

  const auto &missingParsed = missingSemicolonDocument->parseResult;
  const auto &explicitParsed = explicitSemicolonDocument->parseResult;
  const auto missingDump = dump_parse_diagnostics(missingParsed.parseDiagnostics);
  const auto explicitDump =
      dump_parse_diagnostics(explicitParsed.parseDiagnostics);

  ASSERT_TRUE(missingParsed.value) << missingDump;
  ASSERT_TRUE(explicitParsed.value) << explicitDump;
  ASSERT_TRUE(missingSemicolonDocument->parseSucceeded()) << missingDump;
  ASSERT_TRUE(explicitSemicolonDocument->parseSucceeded()) << explicitDump;
  EXPECT_TRUE(missingSemicolonDocument->parseRecovered()) << missingDump;
  EXPECT_TRUE(explicitSemicolonDocument->parseRecovered()) << explicitDump;

  auto *missingModule =
      dynamic_cast<ast::Module *>(missingParsed.value);
  auto *explicitModule =
      dynamic_cast<ast::Module *>(explicitParsed.value);
  ASSERT_NE(missingModule, nullptr) << missingDump;
  ASSERT_NE(explicitModule, nullptr) << explicitDump;

  auto *missingLastEvaluation = dynamic_cast<ast::Evaluation *>(
      missingModule->statements.back());
  auto *explicitLastEvaluation = dynamic_cast<ast::Evaluation *>(
      explicitModule->statements.back());
  ASSERT_NE(missingLastEvaluation, nullptr) << missingDump;
  ASSERT_NE(explicitLastEvaluation, nullptr) << explicitDump;
  auto *missingLastCall = dynamic_cast<ast::FunctionCall *>(
      missingLastEvaluation->expression);
  auto *explicitLastCall = dynamic_cast<ast::FunctionCall *>(
      explicitLastEvaluation->expression);
  ASSERT_NE(missingLastCall, nullptr)
      << missingDump << " :: "
      << summarize_module_statement_shapes(*missingModule);
  ASSERT_NE(explicitLastCall, nullptr)
      << explicitDump << " :: "
      << summarize_module_statement_shapes(*explicitModule);
  EXPECT_EQ(missingLastCall->func.getRefText(), "root");
  EXPECT_EQ(explicitLastCall->func.getRefText(), "root");
  EXPECT_FALSE(missingLastCall->args.empty());
  EXPECT_FALSE(explicitLastCall->args.empty());
}

TEST(ArithmeticsLanguageTest, SingleBrokenCallKeepsFunctionNameLocal) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "Module basicMath\n"
      "\n"
      "Root(64 3/0); // 4\n",
      pegium::test::make_file_uri("single-broken-call.calc"), "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);

  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(document->parseSucceeded()) << parseDump;
  EXPECT_TRUE(document->parseRecovered()) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_EQ(module->statements.size(), 1u) << parseDump;
  auto *evaluation =
      dynamic_cast<ast::Evaluation *>(module->statements.front());
  ASSERT_NE(evaluation, nullptr) << parseDump;
  auto *call =
      dynamic_cast<ast::FunctionCall *>(evaluation->expression);
  ASSERT_NE(call, nullptr) << parseDump;
  EXPECT_EQ(call->func.getRefText(), "root") << parseDump;
  EXPECT_FALSE(call->args.empty()) << parseDump;
}

TEST(ArithmeticsLanguageTest,
     LateStandaloneTokenDoesNotDegradeEarlierRecoveredSemicolons) {
  auto parser = createArithmeticsParser();
  const std::string prefix =
      "Module basicMath\n"
      "\n"
      "def a: 5\n"
      "def b: 3\n"
      "\n"
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
      "\n";
  const std::string suffix =
      "\n"
      "Root(64 + 5, 3/0+5-3); // 4\n";
  const auto withStandalone = prefix + "xx" + suffix;
  const auto withCommentedStandalone = prefix + "//xx" + suffix;
  auto standaloneDocument = pegium::test::parse_document(
      *parser, withStandalone,
      pegium::test::make_file_uri("late-standalone-token.calc"), "arithmetics");
  auto commentedDocument = pegium::test::parse_document(
      *parser, withCommentedStandalone,
      pegium::test::make_file_uri("late-commented-token.calc"), "arithmetics");

  const auto &standaloneParsed = standaloneDocument->parseResult;
  const auto &commentedParsed = commentedDocument->parseResult;
  const auto standaloneDump =
      dump_parse_diagnostics(standaloneParsed.parseDiagnostics);
  const auto commentedDump =
      dump_parse_diagnostics(commentedParsed.parseDiagnostics);
  ASSERT_TRUE(standaloneParsed.value) << standaloneDump;
  ASSERT_TRUE(commentedParsed.value) << commentedDump;
  ASSERT_TRUE(standaloneDocument->parseSucceeded()) << standaloneDump;
  ASSERT_TRUE(commentedDocument->parseSucceeded()) << commentedDump;
  EXPECT_TRUE(standaloneDocument->parseRecovered()) << standaloneDump;
  EXPECT_TRUE(commentedDocument->parseRecovered()) << commentedDump;

  auto *standaloneModule =
      dynamic_cast<ast::Module *>(standaloneParsed.value);
  auto *commentedModule =
      dynamic_cast<ast::Module *>(commentedParsed.value);
  ASSERT_NE(standaloneModule, nullptr) << standaloneDump;
  ASSERT_NE(commentedModule, nullptr) << commentedDump;
  const auto standaloneMarker = withStandalone.find("\nxx\n");
  ASSERT_NE(standaloneMarker, std::string::npos);
  const auto standaloneOffset =
      static_cast<pegium::TextOffset>(standaloneMarker + 1u);
  const auto dump_before_offset =
      [](const std::vector<pegium::parser::ParseDiagnostic> &diagnostics,
         pegium::TextOffset offset) {
        std::vector<pegium::parser::ParseDiagnostic> prefixDiagnostics;
        for (const auto &diagnostic : diagnostics) {
          if (diagnostic.endOffset <= offset) {
            prefixDiagnostics.push_back(diagnostic);
          }
        }
        return dump_parse_diagnostics(prefixDiagnostics);
      };
  EXPECT_EQ(dump_before_offset(standaloneParsed.parseDiagnostics, standaloneOffset),
            dump_before_offset(commentedParsed.parseDiagnostics, standaloneOffset))
      << standaloneDump << " / " << commentedDump;
  EXPECT_TRUE(std::ranges::any_of(
      standaloneParsed.parseDiagnostics, [&](const auto &diag) {
        return pegium::parser::isSyntaxParseDiagnostic(diag.kind) &&
               diag.beginOffset >= standaloneOffset;
      }))
      << standaloneDump;

  auto *standaloneLastEvaluation = dynamic_cast<ast::Evaluation *>(
      standaloneModule->statements.back());
  auto *commentedLastEvaluation = dynamic_cast<ast::Evaluation *>(
      commentedModule->statements.back());
  ASSERT_NE(standaloneLastEvaluation, nullptr) << standaloneDump;
  ASSERT_NE(commentedLastEvaluation, nullptr) << commentedDump;
  EXPECT_EQ(summarize_expression(standaloneLastEvaluation->expression),
            summarize_expression(commentedLastEvaluation->expression))
      << standaloneDump << " / " << commentedDump;
}

TEST(ArithmeticsLanguageTest, ParsesAndEvaluatesModule) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "module Demo\n"
      "def value: 1 + 2;\n"
      "value;\n",
      pegium::test::make_file_uri("language.calc"), "arithmetics");

  ASSERT_TRUE(document->parseSucceeded());
  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);

  const auto results = evaluate_module(*module);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_DOUBLE_EQ(results.front(), 3.0);
}

TEST(ArithmeticsLanguageTest, EvaluatesForwardReferences) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "module Demo\n"
      "def b: a;\n"
      "def a: 5;\n"
      "b;\n",
      pegium::test::make_file_uri("forward.calc"), "arithmetics");

  ASSERT_TRUE(document->parseSucceeded());
  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);

  // `b` references `a` before it is defined: a valid program must evaluate, not
  // throw "Unknown symbol".
  const auto results = evaluate_module(*module);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_DOUBLE_EQ(results.front(), 5.0);
}

TEST(ArithmeticsLanguageTest,
     RecoveryKeepsFollowingStatementAfterMalformedStandaloneOperatorRunInLargerModule) {
  auto parser = createArithmeticsParser();
  const std::string text =
      "module m\n"
      "\n"
      "def a: 5;\n"
      "def b: 3;\n"
      "def c: a + b;\n"
      "def d: (a + b);\n"
      "\n"
      "def root(x, y):\n"
      "    x+(1/y);\n"
      "\n"
      "def sqrt(x):\n"
      "    root(x, 2);\n"
      "\n"
      "2 * c;\n"
      "b % 2;\n"
      "2*********\n"
      "\n"
      "b % 2;\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri("malformed-operator-run-large.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  EXPECT_EQ(module->statements.size(), 9u)
      << parseDump << " :: " << summarize_module_statements(*module) << " :: "
      << summarize_module_statement_shapes(*module);
  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastBinary =
      dynamic_cast<ast::BinaryExpression *>(lastEvaluation->expression);
  ASSERT_NE(lastBinary, nullptr) << parseDump;
  EXPECT_EQ(lastBinary->op, "%") << parseDump;
}

TEST(ArithmeticsLanguageTest,
     MissingDefinitionColonInLargeModulePrefersLocalInsertRecovery) {
  auto parser = createArithmeticsParser();
  const std::string text =
      "//\n"
      "Module basicMath\n"
      "\n"
      "\n"
      "def a 5;\n"
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
      "\n"
      "\n"
      "// This language is case-insensitive regarding symbol names\n"
      "Root(D, 3/0); // 32\n"
      "Root(64, 3/0); // 4\n";
  auto document = pegium::test::parse_document(
      *parser, text, pegium::test::make_file_uri("missing-definition-colon-large.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_GE(module->statements.size(), 8u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  auto *firstDefinition =
      dynamic_cast<ast::Definition *>(module->statements.front());
  ASSERT_NE(firstDefinition, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(firstDefinition->name, "a");
  ASSERT_NE(firstDefinition->expr, nullptr) << parseDump;
  EXPECT_EQ(summarize_expression(firstDefinition->expr),
            "number:5.000000")
      << parseDump;
  EXPECT_NE(parseDump.find("Inserted:':'"), std::string::npos) << parseDump;
}

TEST(ArithmeticsLanguageTest,
     MissingSemicolonBeforeNextDefinitionWithTrailingSemicolonsPrefersLocalInsert) {
  auto parser = createArithmeticsParser();
  const std::string text =
      "//\n"
      "Module basicMath\n"
      "\n"
      "\n"
      "def a: 5\n"
      "def b: 3;\n"
      ";\n"
      ";\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri(
          "missing-semicolon-before-next-definition-with-trailing-semicolons.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  const auto syntaxDiagnosticCount = std::ranges::count_if(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        return pegium::parser::isSyntaxParseDiagnostic(diagnostic.kind);
      });
  const auto insertedSemicolonCount = std::ranges::count_if(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        return diagnostic.kind == pegium::parser::ParseDiagnosticKind::Inserted &&
               diagnostic.beginOffset == 31u && diagnostic.endOffset == 31u &&
               diagnostic.element != nullptr &&
               diagnostic.element->getKind() ==
                   pegium::grammar::ElementKind::Literal;
      });
  EXPECT_EQ(insertedSemicolonCount, 1) << parseDump;
  EXPECT_FALSE(std::ranges::any_of(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        return diagnostic.kind == pegium::parser::ParseDiagnosticKind::Replaced;
      }))
      << parseDump;
  EXPECT_TRUE(std::ranges::all_of(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        if (!pegium::parser::isSyntaxParseDiagnostic(diagnostic.kind)) {
          return true;
        }
        if (diagnostic.kind == pegium::parser::ParseDiagnosticKind::Inserted) {
          return diagnostic.beginOffset >= 31u && diagnostic.endOffset >= 31u;
        }
        return diagnostic.kind == pegium::parser::ParseDiagnosticKind::Deleted &&
               diagnostic.beginOffset >= 40u;
      }))
      << parseDump;
  EXPECT_LE(syntaxDiagnosticCount, 3) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_GE(module->statements.size(), 2u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);

  auto *firstDefinition =
      dynamic_cast<ast::Definition *>(module->statements[0]);
  auto *secondDefinition =
      dynamic_cast<ast::Definition *>(module->statements[1]);
  ASSERT_NE(firstDefinition, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  ASSERT_NE(secondDefinition, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(firstDefinition->name, "a") << parseDump;
  EXPECT_EQ(secondDefinition->name, "b") << parseDump;
  EXPECT_EQ(summarize_expression(firstDefinition->expr),
            "number:5.000000")
      << parseDump;
  EXPECT_EQ(summarize_expression(secondDefinition->expr),
            "number:3.000000")
      << parseDump;
}

TEST(ArithmeticsLanguageTest,
     TwoMissingDefinitionColonsPreferTwoLocalInsertions) {
  auto parser = createArithmeticsParser();
  const std::string text =
      "Module basicMath\n"
      "\n"
      "\n"
      "def a 5;\n"
      "def b 3;\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri("two-missing-definition-colons.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  const auto countInsertedColons = std::ranges::count_if(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        return diagnostic.kind == pegium::parser::ParseDiagnosticKind::Inserted &&
               diagnostic.element != nullptr &&
               diagnostic.element->getKind() ==
                   pegium::grammar::ElementKind::Literal &&
               diagnostic.beginOffset == diagnostic.endOffset &&
               (diagnostic.beginOffset == 25u || diagnostic.beginOffset == 34u);
      });
  EXPECT_EQ(countInsertedColons, 2) << parseDump;
  EXPECT_TRUE(std::ranges::all_of(
      parsed.parseDiagnostics, [](const pegium::parser::ParseDiagnostic &diagnostic) {
        if (!pegium::parser::isSyntaxParseDiagnostic(diagnostic.kind)) {
          return true;
        }
        return diagnostic.kind == pegium::parser::ParseDiagnosticKind::Inserted &&
               diagnostic.element != nullptr &&
               diagnostic.element->getKind() ==
                   pegium::grammar::ElementKind::Literal &&
               diagnostic.beginOffset == diagnostic.endOffset &&
               (diagnostic.beginOffset == 25u || diagnostic.beginOffset == 34u);
      }))
      << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_EQ(module->statements.size(), 2u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);

  auto *firstDefinition =
      dynamic_cast<ast::Definition *>(module->statements[0]);
  auto *secondDefinition =
      dynamic_cast<ast::Definition *>(module->statements[1]);
  ASSERT_NE(firstDefinition, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  ASSERT_NE(secondDefinition, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(firstDefinition->name, "a") << parseDump;
  EXPECT_EQ(secondDefinition->name, "b") << parseDump;
  EXPECT_EQ(summarize_expression(firstDefinition->expr),
            "number:5.000000")
      << parseDump;
  EXPECT_EQ(summarize_expression(secondDefinition->expr),
            "number:3.000000")
      << parseDump;
}

TEST(ArithmeticsLanguageTest,
     RecoveryKeepsFollowingDivisionStatementAfterStandaloneStarRunStatement) {
  auto parser = createArithmeticsParser();
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
      "\n"
      "***;\n"
      "\n"
      "b % 2/0;\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri("standalone-star-run-mid-module.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  const auto starsPos = text.find("***");
  ASSERT_NE(starsPos, std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(parsed.parseDiagnostics, [&](const auto &diag) {
    return diag.kind == pegium::parser::ParseDiagnosticKind::Deleted &&
           diag.beginOffset == static_cast<pegium::TextOffset>(starsPos) &&
           diag.endOffset ==
               static_cast<pegium::TextOffset>(starsPos + 4u);
  })) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  EXPECT_EQ(module->statements.size(), 9u)
      << parseDump << " :: " << summarize_module_statements(*module) << " :: "
      << summarize_module_statement_shapes(*module);

  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastBinary =
      dynamic_cast<ast::BinaryExpression *>(lastEvaluation->expression);
  ASSERT_NE(lastBinary, nullptr) << parseDump;
  EXPECT_EQ(lastBinary->op, "/");
}

TEST(ArithmeticsLanguageTest,
     RecoveryKeepsFollowingStatementAfterLongStandaloneStarRunStatement) {
  auto parser = createArithmeticsParser();
  const std::string operatorRun(37u, '*');
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
      "\n" +
      operatorRun +
      ";\n"
      "\n"
      "b % 2;\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri("standalone-long-star-run-mid-module.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  const auto starsPos = text.find(operatorRun);
  ASSERT_NE(starsPos, std::string::npos);
  EXPECT_TRUE(std::ranges::any_of(parsed.parseDiagnostics, [&](const auto &diag) {
    return diag.kind == pegium::parser::ParseDiagnosticKind::Deleted &&
           diag.beginOffset ==
               static_cast<pegium::TextOffset>(starsPos);
  })) << parseDump;
  EXPECT_TRUE(std::ranges::any_of(parsed.parseDiagnostics, [&](const auto &diag) {
    return diag.kind == pegium::parser::ParseDiagnosticKind::Deleted &&
           diag.endOffset ==
               static_cast<pegium::TextOffset>(starsPos + operatorRun.size() + 1u);
  })) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  EXPECT_EQ(module->statements.size(), 9u)
      << parseDump << " :: " << summarize_module_statements(*module) << " :: "
      << summarize_module_statement_shapes(*module);
  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
}

TEST(ArithmeticsLanguageTest,
     RecoveryKeepsRecoveredCallBeforeTrailingGarbageAndEmptyLineCommentAtEof) {
  auto parser = createArithmeticsParser();
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
      "b % 2; // 1\n"
      "\n"
      "// This language is case-insensitive regarding symbol names\n"
      "Root(D, 3); // 32\n"
      "Root(64 3); // 4\n"
      "\n"
      "\n"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
      "xxxxxxxxxxxxxxxxxxxxxxx\n"
      "//\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri(
          "recovered-call-before-trailing-garbage-empty-comment.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_GE(module->statements.size(), 10u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);

  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements[9]);
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastCall =
      dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(lastCall->func.getRefText(), "root");
  EXPECT_FALSE(lastCall->args.empty());
}

TEST(ArithmeticsLanguageTest,
     RecoveryKeepsRecoveredCallBeforeTrailingGarbageWithoutEmptyLineCommentAtEof) {
  auto parser = createArithmeticsParser();
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
      "b % 2; // 1\n"
      "\n"
      "// This language is case-insensitive regarding symbol names\n"
      "Root(D, 3); // 32\n"
      "Root(64 3); // 4\n"
      "\n"
      "\n"
      "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
      "xxxxxxxxxxxxxxxxxxxxxxx\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri(
          "recovered-call-before-trailing-garbage-no-empty-comment.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_GE(module->statements.size(), 10u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);

  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements[9]);
  ASSERT_NE(lastEvaluation, nullptr) << parseDump;
  auto *lastCall =
      dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(lastCall->func.getRefText(), "root");
  EXPECT_FALSE(lastCall->args.empty());
}

// Folds the three large-module recovery tests that share the identical ordered
// assertion sequence (value / fullMatch / hasRecovered / module cast /
// statements.size() >= 11 / last statement is an Evaluation whose expression is
// a FunctionCall to 'root' with non-empty args), varying only the input module
// text. Each row reproduces its parse setup + module text verbatim.
//
// NOTE: BrokenCallAfterComplexFirstArgumentKeepsFunctionNameWithoutGarbagePrefix
// (asserts args.size() == 2) and the
// RecoveryKeepsRecoveredCallBeforeTrailingGarbage* tests (assert
// statements.size() >= 10 and index statements[9]) are intentionally NOT folded
// here: their assertion shapes differ, so they remain their own tests.
TEST(ArithmeticsLanguageTest, RecoveredTrailingCallKeepsRootBackStatement) {
  struct Outcome {
    bool value;
    bool fullMatch;
    bool hasRecovered;
    ast::Module *module;
    std::size_t statementCount;
    ast::Evaluation *lastEvaluation;
    ast::FunctionCall *lastCall;
    std::string func;
    bool argsEmpty;
    std::string parseDump;
    std::string shapes;
  };
  struct Case {
    const char *name;
    std::function<Outcome()> run;
  };

  static const Case kCases[] = {
      {"RecoveryKeepsFollowingStatementAfterLongGarbageTail",
       [] {
         auto parser = createArithmeticsParser();
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
             "b % 2; // 1\n"
             "\n"
             "// This language is case-insensitive regarding symbol names\n"
             "Root(D, 3); // 32\n"
             "Root(64, 3); // 4\n"
             "\n"
             "\n"
             "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
             "xxxxxxxxxxxxxxxxxxxxxxx\n"
             "\n"
             "Root(64, 3/0); // 4\n";
         auto document = pegium::test::parse_document(
             *parser, text,
             pegium::test::make_file_uri(
                 "recovered-long-garbage-tail-followed-by-call.calc"),
             "arithmetics");

         const auto &parsed = document->parseResult;
         auto *module = dynamic_cast<ast::Module *>(parsed.value);
         std::size_t statementCount =
             module != nullptr ? module->statements.size() : 0u;
         auto *lastEvaluation =
             (module != nullptr && !module->statements.empty())
                 ? dynamic_cast<ast::Evaluation *>(module->statements.back())
                 : nullptr;
         auto *lastCall =
             lastEvaluation != nullptr
                 ? dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression)
                 : nullptr;
         return Outcome{
             static_cast<bool>(parsed.value),
             parsed.fullMatch,
             parsed.recoveryReport.hasRecovered,
             module,
             statementCount,
             lastEvaluation,
             lastCall,
             lastCall != nullptr ? std::string(lastCall->func.getRefText())
                                 : std::string(),
             lastCall != nullptr ? lastCall->args.empty() : true,
             dump_parse_diagnostics(parsed.parseDiagnostics),
             module != nullptr ? summarize_module_statement_shapes(*module)
                               : std::string()};
       }},
      {"ShortGarbageLineBeforeComplexBrokenCallKeepsFollowingCallRecoveryLocal",
       [] {
         auto parser = createArithmeticsParser();
         const std::string text =
             "Module basicMath\n"
             "\n"
             "def a: 5;\n"
             "def b: 3;\n"
             "\n"
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
             "xx\n"
             "\n"
             "Root(64 + 5 3/0+5-3); // 4\n";
         auto document = pegium::test::parse_document(
             *parser, text,
             pegium::test::make_file_uri(
                 "short-garbage-line-before-complex-broken-call-keeps-following-call-local.calc"),
             "arithmetics");

         const auto &parsed = document->parseResult;
         auto *module = dynamic_cast<ast::Module *>(parsed.value);
         std::size_t statementCount =
             module != nullptr ? module->statements.size() : 0u;
         auto *lastEvaluation =
             (module != nullptr && !module->statements.empty())
                 ? dynamic_cast<ast::Evaluation *>(module->statements.back())
                 : nullptr;
         auto *lastCall =
             lastEvaluation != nullptr
                 ? dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression)
                 : nullptr;
         return Outcome{
             static_cast<bool>(parsed.value),
             parsed.fullMatch,
             parsed.recoveryReport.hasRecovered,
             module,
             statementCount,
             lastEvaluation,
             lastCall,
             lastCall != nullptr ? std::string(lastCall->func.getRefText())
                                 : std::string(),
             lastCall != nullptr ? lastCall->args.empty() : true,
             dump_parse_diagnostics(parsed.parseDiagnostics),
             module != nullptr ? summarize_module_statement_shapes(*module)
                               : std::string()};
       }},
      {"SpacedShortGarbageLineBeforeComplexBrokenCallKeepsFollowingCallRecoveryLocal",
       [] {
         auto parser = createArithmeticsParser();
         const std::string text =
             "Module basicMath\n"
             "\n"
             "def a: 5;\n"
             "def b: 3;\n"
             "\n"
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
             "x    \n"
             "\n"
             "Root(64 + 5 3/0+5-3); // 4\n";
         auto document = pegium::test::parse_document(
             *parser, text,
             pegium::test::make_file_uri(
                 "spaced-short-garbage-line-before-complex-broken-call-keeps-following-call-local.calc"),
             "arithmetics");

         const auto &parsed = document->parseResult;
         auto *module = dynamic_cast<ast::Module *>(parsed.value);
         std::size_t statementCount =
             module != nullptr ? module->statements.size() : 0u;
         auto *lastEvaluation =
             (module != nullptr && !module->statements.empty())
                 ? dynamic_cast<ast::Evaluation *>(module->statements.back())
                 : nullptr;
         auto *lastCall =
             lastEvaluation != nullptr
                 ? dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression)
                 : nullptr;
         return Outcome{
             static_cast<bool>(parsed.value),
             parsed.fullMatch,
             parsed.recoveryReport.hasRecovered,
             module,
             statementCount,
             lastEvaluation,
             lastCall,
             lastCall != nullptr ? std::string(lastCall->func.getRefText())
                                 : std::string(),
             lastCall != nullptr ? lastCall->args.empty() : true,
             dump_parse_diagnostics(parsed.parseDiagnostics),
             module != nullptr ? summarize_module_statement_shapes(*module)
                               : std::string()};
       }},
  };

  for (const auto &c : kCases) {
    SCOPED_TRACE(c.name);
    const Outcome o = c.run();
    const std::string &parseDump = o.parseDump;
    ASSERT_TRUE(o.value) << parseDump;
    EXPECT_TRUE(o.fullMatch) << parseDump;
    EXPECT_TRUE(o.hasRecovered) << parseDump;

    ASSERT_NE(o.module, nullptr) << parseDump;
    ASSERT_GE(o.statementCount, 11u) << parseDump << " :: " << o.shapes;

    ASSERT_NE(o.lastEvaluation, nullptr) << parseDump << " :: " << o.shapes;
    ASSERT_NE(o.lastCall, nullptr) << parseDump << " :: " << o.shapes;
    EXPECT_EQ(o.func, "root");
    EXPECT_FALSE(o.argsEmpty);
  }
}


TEST(ArithmeticsLanguageTest,
     BrokenCallAfterComplexFirstArgumentKeepsFunctionNameWithoutGarbagePrefix) {
  auto parser = createArithmeticsParser();
  const std::string text =
      "Module basicMath\n"
      "\n"
      "def a: 5;\n"
      "def b: 3;\n"
      "\n"
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
      "Root(64 + 5 3/0+5-3); // 4\n";
  auto document = pegium::test::parse_document(
      *parser, text,
      pegium::test::make_file_uri(
          "broken-call-after-complex-first-argument-without-garbage-prefix.calc"),
      "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  ASSERT_TRUE(parsed.value) << parseDump;
  EXPECT_TRUE(parsed.fullMatch) << parseDump;
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered) << parseDump;

  auto *module = dynamic_cast<ast::Module *>(parsed.value);
  ASSERT_NE(module, nullptr) << parseDump;
  ASSERT_GE(module->statements.size(), 11u)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);

  auto *lastEvaluation =
      dynamic_cast<ast::Evaluation *>(module->statements.back());
  ASSERT_NE(lastEvaluation, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  auto *lastCall =
      dynamic_cast<ast::FunctionCall *>(lastEvaluation->expression);
  ASSERT_NE(lastCall, nullptr)
      << parseDump << " :: " << summarize_module_statement_shapes(*module);
  EXPECT_EQ(lastCall->func.getRefText(), "root");
  EXPECT_EQ(lastCall->args.size(), 2u);
}


TEST(ArithmeticsLanguageTest, EmptyDocumentPublishesSingleGrammarDiagnosticDirectParse) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser, "", pegium::test::make_file_uri("empty-direct.calc"), "arithmetics");

  const auto &parsed = document->parseResult;
  const auto parseDump = dump_parse_diagnostics(parsed.parseDiagnostics);
  EXPECT_EQ(parsed.parseDiagnostics.size(), 1u) << parseDump;
  if (!parsed.parseDiagnostics.empty()) {
    EXPECT_EQ(parsed.parseDiagnostics.front().offset, 0u) << parseDump;
  }
}

TEST(ArithmeticsLanguageTest, CommentProviderReturnsLeadingBlockComment) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "module Demo\n"
      "/** Adds numbers. */\n"
      "def value: 1;\n",
      pegium::test::make_file_uri("comments.calc"), "arithmetics");

  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *definition = dynamic_cast<ast::Definition *>(module->statements.front());
  ASSERT_NE(definition, nullptr);

  const auto comment =
      services->documentation.commentProvider->getComment(*definition);
  EXPECT_NE(comment.find("Adds numbers."), std::string_view::npos);
}

TEST(ArithmeticsLanguageTest, DocumentationProviderRendersDocCommentMarkdown) {
  auto parser = createArithmeticsParser();
  auto document = pegium::test::parse_document(
      *parser,
      "module Demo\n"
      "/**\n"
      " * Adds numbers.\n"
      " * @param x first value\n"
      " */\n"
      "def value: 1;\n",
      pegium::test::make_file_uri("documentation.calc"), "arithmetics");

  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      arithmetics::createArithmeticsLspServices(*shared, "arithmetics");

  auto *module = dynamic_cast<ast::Module *>(document->parseResult.value);
  ASSERT_NE(module, nullptr);
  auto *definition = dynamic_cast<ast::Definition *>(module->statements.front());
  ASSERT_NE(definition, nullptr);

  const auto documentation = services->documentation.documentationProvider
                                 ->getDocumentation(*definition);
  ASSERT_TRUE(documentation.has_value());
  EXPECT_NE(documentation->find("Adds numbers."), std::string::npos);
  EXPECT_NE(documentation->find("*@param* — x first value"), std::string::npos);
}

TEST(ArithmeticsLanguageTest, HoverReturnsNoContentWithoutDocumentation) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto uri = pegium::test::make_file_uri("hover.calc");
  auto document = pegium::test::open_and_build_document(
      *shared, uri, "arithmetics",
      "Module basicMath\n"
      "\n"
      "\n"
      "def c: 2*5;\n"
      "\n"
      "2 * c + 1 - 3;\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.hoverProvider, nullptr);

  auto expect_no_hover = [&](std::uint32_t line, std::uint32_t character) {
    ::lsp::HoverParams params{};
    params.textDocument.uri = ::lsp::FileUri::parse(uri);
    params.position.line = line;
    params.position.character = character;

    auto hover =
        services->lsp.hoverProvider->getHoverContent(*document, params);
    EXPECT_FALSE(hover.has_value());
  };

  expect_no_hover(3, 1);
  expect_no_hover(5, 4);
}

TEST(ArithmeticsLanguageTest, HoverReturnsDocumentationForModuleName) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto uri = pegium::test::make_file_uri("root-hover.calc");
  auto document = pegium::test::open_and_build_document(
      *shared, uri, "arithmetics",
      "/**the best module*/\n"
      "Module basicMath\n"
      "\n"
      "/** test */\n"
      "def c: 2*5;\n"
      "\n"
      "2 * c + 1 - 3;\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.hoverProvider, nullptr);

  ::lsp::HoverParams params{};
  params.textDocument.uri = ::lsp::FileUri::parse(uri);
  params.position.line = 1;
  params.position.character = 8;

  const auto hover = services->lsp.hoverProvider->getHoverContent(
      *document, params, pegium::utils::default_cancel_token);
  ASSERT_TRUE(hover.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::MarkupContent>(hover->contents));
  const auto &content = std::get<::lsp::MarkupContent>(hover->contents);
  EXPECT_NE(content.value.find("the best module"), std::string::npos);
}

TEST(ArithmeticsLanguageTest, HoverReturnsDocumentationForDefinitionName) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto uri = pegium::test::make_file_uri("definition-hover.calc");
  auto document = pegium::test::open_and_build_document(
      *shared, uri, "arithmetics",
      "Module basicMath\n"
      "\n"
      "/** test */\n"
      "def c: 2*5;\n"
      "\n"
      "2 * c + 1 - 3;\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.hoverProvider, nullptr);

  ::lsp::HoverParams params{};
  params.textDocument.uri = ::lsp::FileUri::parse(uri);
  params.position.line = 3;
  params.position.character = 4;

  const auto hover = services->lsp.hoverProvider->getHoverContent(
      *document, params, pegium::utils::default_cancel_token);
  ASSERT_TRUE(hover.has_value());
  ASSERT_TRUE(std::holds_alternative<::lsp::MarkupContent>(hover->contents));
  const auto &content = std::get<::lsp::MarkupContent>(hover->contents);
  EXPECT_NE(content.value.find("test"), std::string::npos);
}

TEST(ArithmeticsLanguageTest, HoverResultSerializesForDocumentedRootAndReference) {
  auto shared = std::make_unique<arithmetics::ArithmeticsSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(arithmetics::registerArithmeticsLspServices(*shared));

  const auto uri = pegium::test::make_file_uri("documented-hover.calc");
  auto document = pegium::test::open_and_build_document(
      *shared, uri, "arithmetics",
      "/**the best module*/\n"
      "Module basicMath\n"
      "\n"
      "/** test */\n"
      "def c: 2*5;\n"
      "\n"
      "2 * c + 1 - 3;\n");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.hoverProvider, nullptr);

  auto expect_serializable_hover = [&](std::uint32_t line,
                                       std::uint32_t character) {
    ::lsp::HoverParams params{};
    params.textDocument.uri = ::lsp::FileUri::parse(uri);
    params.position.line = line;
    params.position.character = character;

    auto hover =
        services->lsp.hoverProvider->getHoverContent(*document, params);
    ASSERT_TRUE(hover.has_value());

    ::lsp::TextDocument_HoverResult result{};
    result = std::move(*hover);

    const auto json = ::lsp::json::stringify(::lsp::toJson(std::move(result)));
    EXPECT_NO_THROW((void)::lsp::json::parse(json));
  };

  expect_serializable_hover(1, 8);
  expect_serializable_hover(6, 4);
}

} // namespace
} // namespace arithmetics::tests
