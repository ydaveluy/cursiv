#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <optional>

#include <pegium/core/ParseJsonTestSupport.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/parser/RecoveryDebug.hpp>
#include <pegium/core/parser/RecoverySearch.hpp>
#include <pegium/core/syntax-tree/CstBuilder.hpp>
#include <pegium/core/text/TextSnapshot.hpp>

using namespace pegium::parser;

namespace {

detail::StrictParseResult
run_strict_parse(const auto &entryRule, const Skipper &skipper,
                 const pegium::text::TextSnapshot &text,
                 const pegium::utils::CancellationToken &cancelToken = {}) {
  return detail::run_strict_parse(entryRule, skipper, text, cancelToken);
}

detail::StrictFailureEngineResult
inspect_failure(const auto &entryRule, const Skipper &skipper,
                const pegium::text::TextSnapshot &text,
                const pegium::utils::CancellationToken &cancelToken = {}) {
  return detail::run_strict_parse_with_failure_snapshot(entryRule, skipper,
                                                        text, cancelToken);
}

detail::RecoveryAttemptSpec
build_attempt_spec(std::span<const detail::RecoveryWindow> /*selectedWindows*/,
                   const detail::RecoveryWindow &window) {
  return detail::RecoveryAttemptSpec{.window = window};
}

void set_recovery_edits(
    detail::RecoveryAttempt &attempt,
    std::initializer_list<ParseDiagnostic> diagnostics) {
  attempt.recoveryEdits.clear();
  attempt.recoveryEdits.reserve(diagnostics.size());
  for (const auto &diagnostic : diagnostics) {
    attempt.recoveryEdits.push_back({.kind = diagnostic.kind,
                                     .offset = diagnostic.offset,
                                     .beginOffset = diagnostic.beginOffset,
                                     .endOffset = diagnostic.endOffset,
                                     .element = diagnostic.element,
                                     .message = diagnostic.message});
  }
  // Keep the cached facts in sync with the edits so tests that read
  // facts-derived predicates without calling `classify_recovery_attempt`
  // still see consistent values. Tests that classify will recompute
  // facts again — that's fine, derive is pure.
  attempt.facts = detail::derive_attempt_facts(attempt);
}

detail::RecoveryAttempt make_ranked_attempt(
    detail::RecoveryAttemptStatus status, bool fullMatch,
    pegium::TextOffset parsedLength, pegium::TextOffset maxCursorOffset,
    std::uint32_t editCost, pegium::TextOffset firstEditOffset,
    pegium::TextOffset editSpan, std::uint32_t entryCount) {
  detail::RecoveryAttempt attempt;
  attempt.entryRuleMatched = true;
  attempt.fullMatch = fullMatch;
  attempt.parsedLength = parsedLength;
  attempt.maxCursorOffset = maxCursorOffset;
  attempt.editCost = editCost;
  attempt.editCount = entryCount;
  attempt.status = status;
  attempt.recoveryEdits.reserve(entryCount);
  if (entryCount == 1u) {
    attempt.recoveryEdits.push_back(
        {
            .kind = editSpan == 0u ? ParseDiagnosticKind::Inserted
                                   : ParseDiagnosticKind::Deleted,
            .offset = firstEditOffset,
            .beginOffset = firstEditOffset,
            .endOffset = firstEditOffset + editSpan,
            .element = nullptr,
            .message = {},
        });
    attempt.facts = detail::derive_attempt_facts(attempt);
    return attempt;
  }
  for (std::uint32_t index = 0; index < entryCount; ++index) {
    const auto offset =
        index + 1u == entryCount ? firstEditOffset + editSpan : firstEditOffset;
    attempt.recoveryEdits.push_back(
        {
            .kind = ParseDiagnosticKind::Inserted,
            .offset = offset,
            .beginOffset = offset,
            .endOffset = offset,
            .element = nullptr,
            .message = {},
        });
  }
  attempt.facts = detail::derive_attempt_facts(attempt);
  return attempt;
}

std::unique_ptr<pegium::RootCstNode>
make_attempt_cst(std::string_view text,
                 std::initializer_list<
                     std::pair<pegium::TextOffset, pegium::TextOffset>>
                     visibleLeafSpans) {
  auto cst = std::make_unique<pegium::RootCstNode>(
      pegium::text::TextSnapshot::copy(std::string(text)));
  pegium::CstBuilder builder(*cst);
  static const auto leafElement = "leaf"_kw;
  for (const auto &[beginOffset, endOffset] : visibleLeafSpans) {
    builder.leaf(beginOffset, endOffset, std::addressof(leafElement));
  }
  return cst;
}

std::string dump_parse_diagnostics(
    const std::vector<pegium::parser::ParseDiagnostic> &diagnostics) {
  std::string dump;
  for (const auto &diagnostic : diagnostics) {
    if (!dump.empty()) {
      dump += " | ";
    }
    dump += std::to_string(diagnostic.beginOffset);
    dump += "-";
    dump += std::to_string(diagnostic.endOffset);
    dump += ":";
    switch (diagnostic.kind) {
    case ParseDiagnosticKind::Inserted:
      dump += "Inserted";
      break;
    case ParseDiagnosticKind::Deleted:
      dump += "Deleted";
      break;
    case ParseDiagnosticKind::Replaced:
      dump += "Replaced";
      break;
    case ParseDiagnosticKind::Incomplete:
      dump += "Incomplete";
      break;
    case ParseDiagnosticKind::Recovered:
      dump += "Recovered";
      break;
    case ParseDiagnosticKind::ConversionError:
      dump += "ConversionError";
      break;
    }
  }
  return dump;
}

constexpr pegium::CstJsonConversionOptions kRecoveryCstJsonOptions{
    .includeText = true,
    .includeGrammarSource = true,
    .includeHidden = false,
    .includeRecovered = true,
};

struct RecoverySearchNode : pegium::AstNode {
  string name;
};

struct RecoveryPairNode : pegium::AstNode {
  string first;
  string second;
};

struct RecoveryTripleNode : pegium::AstNode {
  string first;
  string second;
  string third;
};

struct RecoveryQuadNode : pegium::AstNode {
  string first;
  string second;
  string third;
  string fourth;
};

struct RecoveryQuintNode : pegium::AstNode {
  string first;
  string second;
  string third;
  string fourth;
  string fifth;
};

struct RecoveryStatementNode : pegium::AstNode {
  string name;
};

struct RecoveryStatementListNode : pegium::AstNode {
  vector<pointer<pegium::AstNode>> statements;
};

struct RecoverySearchTransitionNode : pegium::AstNode {
  string event;
  string target;
};

struct RecoverySearchStateNode : pegium::AstNode {
  string name;
  vector<pointer<RecoverySearchTransitionNode>> transitions;
};

struct RecoveryFeatureNode : pegium::AstNode {
  bool many = false;
  string name;
  string type;
};

struct RecoveryEntityNode : pegium::AstNode {
  string name;
  vector<pointer<RecoveryFeatureNode>> features;
};

struct RecoveryDataTypeNode : pegium::AstNode {
  string name;
};

struct RecoveryTypeListNode : pegium::AstNode {
  vector<pointer<pegium::AstNode>> elements;
};

std::string summarize_statement_names(const RecoveryStatementListNode &root) {
  std::string summary;
  for (const auto &statement : root.statements) {
    const auto *node =
        dynamic_cast<const RecoveryStatementNode *>(statement);
    if (node == nullptr) {
      continue;
    }
    if (!summary.empty()) {
      summary += "|";
    }
    summary += node->name;
  }
  return summary;
}

TEST(RecoverySearchTest, MissingDelimiterUsesInsertionAttempt) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchNode> entry{
      "Entry", assign<&RecoverySearchNode::name>(id) + ";"_kw};

  ParseOptions options;
  options.recoveryWindowTokenCount = 4;
  const auto result = pegium::test::Parse(entry, "hello", options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 5,
          "grammarSource": "name=ID",
          "text": "hello"
        },
        {
          "begin": 5,
          "end": 5,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        }
      ],
      "end": 5,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json");
  ASSERT_FALSE(result.parseDiagnostics.empty());
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Inserted);
}

TEST(RecoverySearchTest,
     StatementChoicePrefersInsertedDelimiterOverDeletingCurrentStatement) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryStatementNode> tagged{
      "Tagged", "def"_kw + assign<&RecoveryStatementNode::name>(id) + ";"_kw};
  ParserRule<RecoveryStatementNode> plain{
      "Plain", assign<&RecoveryStatementNode::name>(id) + ";"_kw};
  ParserRule<pegium::AstNode> statement{"Statement", tagged | plain};
  ParserRule<RecoveryStatementListNode> entry{
      "Entry", some(append<&RecoveryStatementListNode::statements>(statement))};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  const auto parsed = pegium::test::Parse(entry, "def a\ndef b;", skipper);

  ASSERT_TRUE(parsed.value);
  EXPECT_TRUE(parsed.fullMatch);
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered);
  EXPECT_TRUE(
      std::ranges::any_of(parsed.parseDiagnostics, [](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Inserted;
      }));
  EXPECT_FALSE(
      std::ranges::any_of(parsed.parseDiagnostics, [](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Deleted;
      }));

  auto *root = pegium::ast_ptr_cast<RecoveryStatementListNode>(parsed.value);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->statements.size(), 2u);
  auto *first =
      dynamic_cast<RecoveryStatementNode *>(root->statements[0]);
  auto *second =
      dynamic_cast<RecoveryStatementNode *>(root->statements[1]);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(first->name, "a");
  EXPECT_EQ(second->name, "b");
}

TEST(
    RecoverySearchTest,
    StatementChoiceKeepsFollowingStatementRecoverableAfterStrayStandaloneToken) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryStatementNode> tagged{
      "Tagged", "def"_kw + assign<&RecoveryStatementNode::name>(id) + ";"_kw};
  ParserRule<RecoveryStatementNode> plain{
      "Plain", assign<&RecoveryStatementNode::name>(id) + ";"_kw};
  ParserRule<pegium::AstNode> statement{"Statement", tagged | plain};
  ParserRule<RecoveryStatementListNode> entry{
      "Entry", some(append<&RecoveryStatementListNode::statements>(statement))};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  const std::string input = "def a;\n"
                            "v\n"
                            "def b;\n";
  const auto parsed = pegium::test::Parse(entry, input, skipper);

  ASSERT_TRUE(parsed.value);
  EXPECT_TRUE(parsed.fullMatch);
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered);

  const auto strayPos = input.find("\nv\n");
  ASSERT_NE(strayPos, std::string::npos);
  const auto strayOffset = static_cast<pegium::TextOffset>(strayPos + 1);
  const auto nextStatementPos = input.find("def b;");
  ASSERT_NE(nextStatementPos, std::string::npos);
  const auto nextStatementOffset =
      static_cast<pegium::TextOffset>(nextStatementPos);
  EXPECT_TRUE(
      std::ranges::any_of(parsed.parseDiagnostics, [&](const auto &diag) {
        if (!diag.isSyntax()) {
          return false;
        }
        return diag.beginOffset >= strayOffset &&
               diag.beginOffset <= nextStatementOffset;
      }));

  auto *root = pegium::ast_ptr_cast<RecoveryStatementListNode>(parsed.value);
  ASSERT_NE(root, nullptr);
  ASSERT_GE(root->statements.size(), 2u);
  auto *first =
      dynamic_cast<RecoveryStatementNode *>(root->statements[0]);
  auto *last =
      dynamic_cast<RecoveryStatementNode *>(root->statements.back());
  ASSERT_NE(first, nullptr);
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(first->name, "a");
  EXPECT_EQ(last->name, "b");
}

TEST(
    RecoverySearchTest,
    InitialRecoveryWindowDoesNotRewriteEarlierTopLevelChoiceBeforeStrictFrontier) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryFeatureNode> feature{
      "Feature", option(enable_if<&RecoveryFeatureNode::many>("many"_kw)) +
                     assign<&RecoveryFeatureNode::name>(id) + ":"_kw +
                     assign<&RecoveryFeatureNode::type>(id)};
  ParserRule<RecoveryDataTypeNode> dataType{
      "DataType", "datatype"_kw + assign<&RecoveryDataTypeNode::name>(id)};
  ParserRule<RecoveryEntityNode> entity{
      "Entity", "entity"_kw + assign<&RecoveryEntityNode::name>(id) + "{"_kw +
                    many(append<&RecoveryEntityNode::features>(feature)) +
                    "}"_kw};
  ParserRule<pegium::AstNode> type{"Type", dataType | entity};
  ParserRule<RecoveryTypeListNode> entry{
      "Entry", some(append<&RecoveryTypeListNode::elements>(type))};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  const auto parsed = pegium::test::Parse(entry,
                                          "entity Post {\n"
                                          "  many comments Comment\n"
                                          "  title: String\n"
                                          "}\n"
                                          "\n"
                                          "entity Comment {}\n",
                                          skipper);

  ASSERT_TRUE(parsed.value);
  EXPECT_TRUE(parsed.fullMatch);
  EXPECT_TRUE(parsed.recoveryReport.hasRecovered);
  EXPECT_FALSE(
      std::ranges::any_of(parsed.parseDiagnostics, [](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Replaced &&
               diag.beginOffset == 0u;
      }));

  auto *root = pegium::ast_ptr_cast<RecoveryTypeListNode>(parsed.value);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->elements.size(), 2u);
  auto *post = dynamic_cast<RecoveryEntityNode *>(root->elements[0]);
  auto *comment = dynamic_cast<RecoveryEntityNode *>(root->elements[1]);
  ASSERT_NE(post, nullptr);
  ASSERT_NE(comment, nullptr);
  EXPECT_EQ(post->name, "Post");
  EXPECT_EQ(comment->name, "Comment");
  ASSERT_EQ(post->features.size(), 2u);
  ASSERT_NE(post->features[0], nullptr);
  EXPECT_TRUE(post->features[0]->many);
  EXPECT_EQ(post->features[0]->name, "comments");
  EXPECT_EQ(post->features[0]->type, "Comment");
}

TEST(RecoverySearchTest, RecoveryAttemptSpecsStayGenericWithoutAnchorVariants) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryPairNode> entry{
      "Entry", assign<&RecoveryPairNode::first>(id) + ";"_kw +
                   assign<&RecoveryPairNode::second>(id)};
  const auto text = pegium::text::TextSnapshot::copy("one two");

  const auto failureAnalysis = inspect_failure(entry, NoOpSkipper(), text);
  const auto window =
      detail::compute_recovery_window(failureAnalysis.snapshot, 2u);
  const auto spec = build_attempt_spec({}, window);

  EXPECT_EQ(spec.window.beginOffset, window.beginOffset);
  EXPECT_EQ(spec.window.maxCursorOffset, window.maxCursorOffset);
}

TEST(
    RecoverySearchTest,
    ExplicitRecoveryAttemptWindowCanDeleteGarbageBeforeRecoverableIterationAndSuffix) {
  const auto skipper = SkipperBuilder().ignore(some(s)).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchTransitionNode> transition{
      "Transition", assign<&RecoverySearchTransitionNode::event>(id) + "=>"_kw +
                        assign<&RecoverySearchTransitionNode::target>(id)};
  ParserRule<RecoverySearchStateNode> entry{
      "State",
      "state"_kw + assign<&RecoverySearchStateNode::name>(id) +
          many(append<&RecoverySearchStateNode::transitions>(transition)) +
          "end"_kw};

  const auto text =
      pegium::text::TextSnapshot::copy("state Idle\n"
                                       "<<<<<<<<<<<<<<<<<<<<<<<<\n"
                                       "Start => Idle\n"
                                       "end\n");
  ParseOptions options;
  options.recoveryWindowTokenCount = 8;
  const detail::RecoveryWindow window{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 11,
      .tokenCount = 8,
      .forwardTokenCount = 8,
  };
  const auto spec = build_attempt_spec({}, window);
  auto attempt =
      detail::execute_recovery_parse(entry, skipper, options, text, spec);
  detail::classify_recovery_attempt(attempt);
  const auto diagnostics =
      detail::materialize_syntax_diagnostics(attempt.recoveryEdits);

  EXPECT_TRUE(attempt.entryRuleMatched);
  EXPECT_TRUE(attempt.fullMatch);
  EXPECT_EQ(attempt.parsedLength, static_cast<pegium::TextOffset>(text.size()));
  EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Deleted;
      }));
  EXPECT_NE(attempt.status, detail::RecoveryAttemptStatus::StrictFailure);
}

TEST(RecoverySearchTest,
     ExplicitStablePrefixAttemptCanDeleteGarbageFeatureLineInsideEntity) {
  const auto skipper = SkipperBuilder().ignore(some(s)).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryFeatureNode> feature{
      "Feature", option(enable_if<&RecoveryFeatureNode::many>("many"_kw)) +
                     assign<&RecoveryFeatureNode::name>(id) + ":"_kw +
                     assign<&RecoveryFeatureNode::type>(id)};
  ParserRule<RecoveryEntityNode> entry{
      "Entity", "entity"_kw + assign<&RecoveryEntityNode::name>(id) + "{"_kw +
                    many(append<&RecoveryEntityNode::features>(feature)) +
                    "}"_kw};

  const auto text =
      pegium::text::TextSnapshot::copy("entity Blog {\n"
                                       "  <<<<<<<<<<<<<<<<<<<<<<<<\n"
                                       "  title: String\n"
                                       "}\n");
  ParseOptions options;
  options.recoveryWindowTokenCount = 8;
  const detail::RecoveryWindow window{
      .beginOffset = 13,
      .editFloorOffset = 13,
      .maxCursorOffset = 13,
      .tokenCount = 8,
      .forwardTokenCount = 8,
      .stablePrefixOffset = 13,
      .hasStablePrefix = true,
  };
  const auto spec = build_attempt_spec({}, window);
  auto attempt =
      detail::execute_recovery_parse(entry, skipper, options, text, spec);
  detail::classify_recovery_attempt(attempt);
  const auto diagnostics =
      detail::materialize_syntax_diagnostics(attempt.recoveryEdits);

  EXPECT_TRUE(attempt.entryRuleMatched);
  EXPECT_TRUE(attempt.fullMatch);
  EXPECT_EQ(attempt.parsedLength, static_cast<pegium::TextOffset>(text.size()));
  EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Deleted;
      }));
  EXPECT_TRUE(detail::is_selectable_recovery_attempt(attempt));
}

TEST(RecoverySearchTest,
     InitialWindowFloorStopsBeforeFailureLeafThatEndsAtFurthestCursor) {
  ParseOptions options;
  options.recoveryWindowTokenCount = 8;

  detail::FailureSnapshot snapshot{
      .maxCursorOffset = 4,
      .failureLeafHistory =
          {
              {.beginOffset = 0, .endOffset = 1, .element = nullptr},
              {.beginOffset = 1, .endOffset = 2, .element = nullptr},
              {.beginOffset = 2, .endOffset = 3, .element = nullptr},
              {.beginOffset = 3, .endOffset = 4, .element = nullptr},
          },
      .failureTokenIndex = 3,
      .hasFailureToken = true,
  };
  detail::RecoveryAttempt selectedAttempt;

  const auto window = detail::plan_recovery_window(
      snapshot, selectedAttempt, options,
      std::max<std::uint32_t>(1u, options.recoveryWindowTokenCount));

  EXPECT_EQ(window.beginOffset, 0u);
  EXPECT_EQ(window.editFloorOffset, 3u);
  EXPECT_EQ(window.stablePrefixOffset, 3u);
  EXPECT_TRUE(window.hasStablePrefix);
}

TEST(RecoverySearchTest, ClassifiesRecoveryAttemptSelectability) {
  struct Case {
    const char *name;
    std::function<detail::RecoveryAttempt()> build;
    std::optional<detail::RecoveryAttemptStatus> expectedStatus;
    std::optional<bool> expectedSelectable;
    std::optional<bool> expectedFallbackContract;
  };
  static const Case kCases[] = {
      {"BudgetOverflowFullMatchAtStablePrefixBoundaryDeleteOnlyIsFallback",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.stablePrefixOffset = 10;
         attempt.hasStablePrefix = true;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 112;
         attempt.fullMatch = true;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 10,
                                       .beginOffset = 10,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, false,
       std::nullopt},
      {"BudgetOverflowAcrossLaterReplayWindowStaysSelectableOnFullMatch",
       [] {
         // Over-budget attempts that nevertheless reach a full match with
         // genuine continuation past the first edit are trusted under the
         // 4-axis ranking: the budget is a cap on exploration, not a veto on
         // completed parses.
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.stablePrefixOffset = 22;
         attempt.hasStablePrefix = true;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 107;
         attempt.parsedLength = 113;
         attempt.maxCursorOffset = 113;
         attempt.fullMatch = true;
         attempt.stableAfterRecovery = true;
         attempt.reachedRecoveryTarget = true;
         attempt.replayWindow =
             detail::RecoveryWindow{.beginOffset = 3,
                                    .editFloorOffset = 22,
                                    .maxCursorOffset = 32,
                                    .tokenCount = 8,
                                    .forwardTokenCount = 8,
                                    .visibleLeafBeginIndex = 0,
                                    .stablePrefixOffset = 22,
                                    .hasStablePrefix = true};
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 32,
                                       .beginOffset = 32,
                                       .endOffset = 32,
                                       .element = nullptr,
                                       .message = {}},
                                      {.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 73,
                                       .beginOffset = 73,
                                       .endOffset = 94,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, true, std::nullopt},
      {"BudgetOverflowFullMatchWithoutStablePrefixAtEditFloorIsNotSelectable",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.stablePrefixOffset = 0;
         attempt.hasStablePrefix = false;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 112;
         attempt.fullMatch = true;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 10,
                                       .beginOffset = 10,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, false,
       std::nullopt},
      {"BudgetOverflowFullMatchDeletingInputPrefixWithoutStablePrefixRemainsSelectable",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = true;
         attempt.parsedLength = 53;
         attempt.lastVisibleCursorOffset = 53;
         attempt.maxCursorOffset = 53;
         attempt.stablePrefixOffset = 0;
         attempt.hasStablePrefix = false;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 112;
         attempt.replayWindow =
             detail::RecoveryWindow{.beginOffset = 0,
                                    .editFloorOffset = 0,
                                    .maxCursorOffset = 0,
                                    .tokenCount = 1,
                                    .forwardTokenCount = 1,
                                    .visibleLeafBeginIndex = 0,
                                    .stablePrefixOffset = 0,
                                    .hasStablePrefix = false};
         attempt.cst = make_attempt_cst("req login", {{24u, 27u}, {28u, 33u}});
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 0,
                                       .beginOffset = 0,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, true, std::nullopt},
      {"RecoveredButNotCredibleAttemptWithinBudgetIsNotSelectable",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 12;
         attempt.completedRecoveryWindows = 1;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, false,
       std::nullopt},
      {"NarrowLocalGapInsertionWithoutStablePrefixCanBypassBudgetDowngrade",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.reachedRecoveryTarget = true;
         attempt.parsedLength = 28;
         attempt.lastVisibleCursorOffset = 28;
         attempt.maxCursorOffset = 31;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, std::nullopt, std::nullopt},
      {"FullMatchLocalGapInsertionWithContinuationBypassesBudget",
       [] {
         // A local gap insert that still produces a full match with visible
         // continuation past the edit is accepted even when the edit cost
         // exceeds the configured budget. The budget caps exploratory work,
         // not completed parses.
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = true;
         attempt.reachedRecoveryTarget = true;
         attempt.parsedLength = 28;
         attempt.lastVisibleCursorOffset = 28;
         attempt.maxCursorOffset = 31;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, true, std::nullopt},
      {"StableLocalGapInsertionWithoutStablePrefixDoesNotBypassBudgetDowngrade",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.stableAfterRecovery = true;
         attempt.reachedRecoveryTarget = true;
         attempt.parsedLength = 28;
         attempt.lastVisibleCursorOffset = 28;
         attempt.maxCursorOffset = 31;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, false,
       std::nullopt},
      {"MultiInsertWithoutStablePrefixDoesNotUseLocalGapBudgetExemption",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.reachedRecoveryTarget = true;
         attempt.parsedLength = 28;
         attempt.lastVisibleCursorOffset = 28;
         attempt.maxCursorOffset = 31;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}},
                                      {.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 18,
                                       .beginOffset = 18,
                                       .endOffset = 18,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       std::nullopt},
      {"DeleteOnlyFullMatchDeletingInputPrefixIsSelectableWithoutFallback",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = true;
         attempt.parsedLength = 53;
         attempt.lastVisibleCursorOffset = 53;
         attempt.maxCursorOffset = 53;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         attempt.replayWindow =
             detail::RecoveryWindow{.beginOffset = 0,
                                    .editFloorOffset = 0,
                                    .maxCursorOffset = 0,
                                    .tokenCount = 1,
                                    .forwardTokenCount = 1,
                                    .visibleLeafBeginIndex = 0,
                                    .stablePrefixOffset = 0,
                                    .hasStablePrefix = false};
         attempt.cst = make_attempt_cst("req login", {{24u, 27u}, {28u, 33u}});
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 0,
                                       .beginOffset = 0,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, true, false},
      {"DeleteOnlyPrefixProgressWithoutFullMatchCannotUseFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = false;
         attempt.parsedLength = 41;
         attempt.lastVisibleCursorOffset = 41;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 48;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 0,
                                       .beginOffset = 0,
                                       .endOffset = 12,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       false},
      {"DeleteOnlyLocalGapProgressWithoutStablePrefixCanUseFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = false;
         attempt.parsedLength = 17;
         attempt.lastVisibleCursorOffset = 17;
         attempt.maxCursorOffset = 34;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 1;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 8,
                                       .beginOffset = 8,
                                       .endOffset = 9,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       true},
      {"InsertedPrefixProgressWithoutTailParseCanUseFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.hasStablePrefix = true;
         attempt.stablePrefixOffset = 2;
         attempt.parsedLength = 21;
         attempt.lastVisibleCursorOffset = 21;
         attempt.maxCursorOffset = 29;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 2;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 3,
                                       .beginOffset = 3,
                                       .endOffset = 3,
                                       .element = nullptr,
                                       .message = {}},
                                      {.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 21,
                                       .beginOffset = 21,
                                       .endOffset = 21,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       true},
      {"StablePrefixBoundaryClosingInsertionsNeedTailProgressForFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.hasStablePrefix = true;
         attempt.stablePrefixOffset = 10;
         attempt.parsedLength = 34;
         attempt.lastVisibleCursorOffset = 33;
         attempt.maxCursorOffset = 34;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 2;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 16,
                                       .beginOffset = 16,
                                       .endOffset = 16,
                                       .element = nullptr,
                                       .message = {}},
                                      {.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 34,
                                       .beginOffset = 34,
                                       .endOffset = 34,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       false},
      {"SingleBoundaryClosingInsertionNeedsTailProgressForFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.hasStablePrefix = true;
         attempt.stablePrefixOffset = 22;
         attempt.parsedLength = 25;
         attempt.lastVisibleCursorOffset = 25;
         attempt.maxCursorOffset = 25;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 1;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 25,
                                       .beginOffset = 25,
                                       .endOffset = 25,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       false},
      {"BoundaryClosingInsertionCanUseFallbackSelectionAfterSpeculativeForwardExploration",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.hasStablePrefix = true;
         attempt.stablePrefixOffset = 22;
         attempt.parsedLength = 25;
         attempt.lastVisibleCursorOffset = 25;
         attempt.maxCursorOffset = 34;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 1;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Inserted,
                                       .offset = 25,
                                       .beginOffset = 25,
                                       .endOffset = 25,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       true},
      {"NonPrefixDeleteOnlyFullMatchIsSelectableOnGenuineContinuation",
       [] {
         // A non-prefix delete that still reaches a full match with visible
         // continuation past the deleted span is accepted under the 4-axis
         // ranking: the fallback-contract path is bypassed because the primary
         // path already classifies the attempt as Stable.
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = true;
         attempt.parsedLength = 53;
         attempt.lastVisibleCursorOffset = 53;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 12,
                                       .beginOffset = 12,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::Selectable, true, std::nullopt},
      {"DeleteOnlyFullMatchWithoutVisibleContinuationCannotUseFallbackSelection",
       [] {
         detail::RecoveryAttempt attempt;
         attempt.entryRuleMatched = true;
         attempt.fullMatch = true;
         attempt.parsedLength = 24;
         attempt.lastVisibleCursorOffset = 0;
         attempt.configuredMaxEditCost = 64;
         attempt.editCost = 96;
         set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                       .offset = 0,
                                       .beginOffset = 0,
                                       .endOffset = 24,
                                       .element = nullptr,
                                       .message = {}}});
         return attempt;
       },
       detail::RecoveryAttemptStatus::RecoveredButNotCredible, std::nullopt,
       false},
  };

  for (const auto &c : kCases) {
    SCOPED_TRACE(c.name);
    detail::RecoveryAttempt attempt = c.build();
    detail::classify_recovery_attempt(attempt);
    if (c.expectedStatus) {
      EXPECT_EQ(attempt.status, *c.expectedStatus);
    }
    if (c.expectedSelectable) {
      EXPECT_EQ(detail::is_selectable_recovery_attempt(attempt),
                *c.expectedSelectable);
    }
    if (c.expectedFallbackContract) {
      EXPECT_EQ(detail::satisfies_non_credible_fallback_contract(attempt, {}),
                *c.expectedFallbackContract);
    }
  }
}

TEST(RecoverySearchTest,
     LaterRecoveryWindowDoesNotDegradeEarlierRecoveredStatementPrefix) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryStatementNode> statement{
      "Statement",
      "def"_kw + assign<&RecoveryStatementNode::name>(id) + ";"_kw};
  ParserRule<RecoveryStatementListNode> entry{
      "Entry", some(append<&RecoveryStatementListNode::statements>(statement))};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  ParseOptions options;
  options.recoveryWindowTokenCount = 4;

  const std::string baselineInput = "def a\n"
                                    "def b\n"
                                    "\n"
                                    "def c;\n"
                                    "def d;\n"
                                    "\n"
                                    "def e;\n";
  const std::string laterErrorInput = "def a\n"
                                      "def b\n"
                                      "\n"
                                      "def c;\n"
                                      "def d;\n"
                                      "\n"
                                      "xx\n"
                                      "\n"
                                      "def e;\n";

  const auto baseline =
      pegium::test::Parse(entry, baselineInput, skipper, options);
  const auto laterError =
      pegium::test::Parse(entry, laterErrorInput, skipper, options);
  const auto baselineDump = dump_parse_diagnostics(baseline.parseDiagnostics);
  const auto laterErrorDump =
      dump_parse_diagnostics(laterError.parseDiagnostics);

  ASSERT_TRUE(baseline.value) << baselineDump;
  ASSERT_TRUE(laterError.value) << laterErrorDump;
  ASSERT_TRUE(baseline.fullMatch) << baselineDump;
  ASSERT_TRUE(laterError.fullMatch) << laterErrorDump;
  EXPECT_TRUE(baseline.recoveryReport.hasRecovered);
  EXPECT_TRUE(laterError.recoveryReport.hasRecovered);
  EXPECT_GE(laterError.recoveryReport.recoveryEdits, 2u);

  auto *baselineRoot =
      pegium::ast_ptr_cast<RecoveryStatementListNode>(baseline.value);
  auto *laterErrorRoot =
      pegium::ast_ptr_cast<RecoveryStatementListNode>(laterError.value);
  ASSERT_NE(baselineRoot, nullptr);
  ASSERT_NE(laterErrorRoot, nullptr);

  EXPECT_EQ(summarize_statement_names(*baselineRoot), "a|b|c|d|e");
  const auto laterErrorSummary = summarize_statement_names(*laterErrorRoot);
  EXPECT_TRUE(std::string_view(laterErrorSummary).starts_with("a|b|c|d"))
      << laterErrorSummary;

  const auto laterErrorMarker = laterErrorInput.find("\nxx\n");
  ASSERT_NE(laterErrorMarker, std::string::npos);
  const auto laterErrorOffset =
      static_cast<pegium::TextOffset>(laterErrorMarker + 1u);
  EXPECT_EQ(std::ranges::count_if(laterError.parseDiagnostics,
                                  [&](const auto &diag) {
                                    return diag.kind ==
                                               ParseDiagnosticKind::Inserted &&
                                           diag.offset < laterErrorOffset;
                                  }),
            2u);
  EXPECT_FALSE(
      std::ranges::any_of(laterError.parseDiagnostics, [&](const auto &diag) {
        return diag.kind == ParseDiagnosticKind::Deleted &&
               diag.beginOffset < laterErrorOffset;
      }));
}

TEST(RecoverySearchTest, RecoveryReportStaysEmptyWhenStrictParseMatches) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchNode> entry{"Entry",
                                       assign<&RecoverySearchNode::name>(id)};

  const auto result = pegium::test::Parse(entry, "hello");

  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(result.parseDiagnostics.empty());
  EXPECT_FALSE(result.recoveryReport.hasRecovered);
  EXPECT_FALSE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 0u);
  EXPECT_EQ(result.recoveryReport.recoveryAttemptRuns, 0u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 0u);
  EXPECT_FALSE(result.recoveryReport.lastRecoveryWindow.has_value());
}

TEST(RecoverySearchTest, ParserResultMatchesGlobalRecoverySearchRunResult) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchNode> entry{
      "Entry", assign<&RecoverySearchNode::name>(id) + ";"_kw};

  ParseOptions options;
  options.recoveryWindowTokenCount = 4;
  const auto text = pegium::text::TextSnapshot::copy("hello");

  const auto search =
      detail::orchestrate_recovery_search(entry, NoOpSkipper(), options, text);
  const auto parsed =
      pegium::test::Parse(entry, "hello", NoOpSkipper(), options);

  EXPECT_EQ(parsed.fullMatch, search.selectedAttempt.fullMatch);
  EXPECT_EQ(parsed.parsedLength, search.selectedAttempt.parsedLength);
  EXPECT_EQ(parsed.lastVisibleCursorOffset,
            search.selectedAttempt.lastVisibleCursorOffset);
  EXPECT_EQ(parsed.failureVisibleCursorOffset,
            search.failureVisibleCursorOffset);
  EXPECT_EQ(parsed.maxCursorOffset, search.selectedAttempt.maxCursorOffset);

  EXPECT_EQ(parsed.recoveryReport.hasRecovered,
            !search.selectedWindows.empty());
  EXPECT_EQ(parsed.recoveryReport.fullRecovered,
            !search.selectedWindows.empty() &&
                search.selectedAttempt.fullMatch);
  EXPECT_EQ(parsed.recoveryReport.recoveryCount,
            search.selectedWindows.size());
  EXPECT_EQ(parsed.recoveryReport.recoveryWindowsTried,
            search.recoveryWindowsTried);
  EXPECT_EQ(parsed.recoveryReport.strictParseRuns, search.strictParseRuns);
  EXPECT_EQ(parsed.recoveryReport.recoveryAttemptRuns,
            search.recoveryAttemptRuns);
  EXPECT_EQ(parsed.recoveryReport.recoveryEdits,
            search.selectedAttempt.editCount);

  ASSERT_EQ(parsed.recoveryReport.lastRecoveryWindow.has_value(),
            !search.selectedWindows.empty());
  if (!search.selectedWindows.empty()) {
    const auto &lastWindow = search.selectedWindows.back();
    const auto &reportedWindow = *parsed.recoveryReport.lastRecoveryWindow;
    EXPECT_EQ(reportedWindow.beginOffset, lastWindow.beginOffset);
    EXPECT_EQ(reportedWindow.maxCursorOffset, lastWindow.maxCursorOffset);
    EXPECT_EQ(reportedWindow.backwardTokenCount, lastWindow.tokenCount);
    EXPECT_EQ(reportedWindow.forwardTokenCount, lastWindow.forwardTokenCount);
  }
}

// Global recovery-attempt ranking is the RecoveryKey
// (fullMatch > matched > rank-anchor score higher > faithfulness lower >
// progressAfterEdits higher), where the rank anchor is the first edit's
// offset for destructive scripts, while insert-only attempts fold to their
// own progress (recovery_key_first_edit_score).

TEST(RecoverySearchTest,
     GlobalRecoveryRankingPrefersLaterDestructiveEditOverFartherParse) {
  // Non-zero edit spans make the single edit a Deleted, so the rank anchor
  // is the edit site itself (no insert transparency in play).
  const auto farther = make_ranked_attempt(detail::RecoveryAttemptStatus::Selectable,
                                           false, 24u, 24u, 1u, 10u, 1u, 1u);
  const auto laterEdit = make_ranked_attempt(
      detail::RecoveryAttemptStatus::Selectable, false, 20u, 20u, 1u, 18u, 1u, 1u);

  EXPECT_TRUE(detail::is_better_recovery_attempt(laterEdit, farther));
  EXPECT_FALSE(detail::is_better_recovery_attempt(farther, laterEdit));
}

TEST(RecoverySearchTest,
     GlobalRecoveryRankingPrefersLaterDestructiveEditWhenAttemptsOtherwiseTie) {
  const auto earlier = make_ranked_attempt(detail::RecoveryAttemptStatus::Selectable,
                                           true, 24u, 24u, 1u, 8u, 1u, 1u);
  const auto later = make_ranked_attempt(detail::RecoveryAttemptStatus::Selectable,
                                         true, 24u, 24u, 1u, 12u, 1u, 1u);

  EXPECT_TRUE(detail::is_better_recovery_attempt(later, earlier));
  EXPECT_FALSE(detail::is_better_recovery_attempt(earlier, later));
}

TEST(RecoverySearchTest,
     GlobalRecoveryRankingInsertOnlyAttemptFoldsPastDestructiveCascade) {
  // An insert-only attempt contradicts no source text, so its rank anchor
  // folds to its own progress and outranks a destructive cascade that pushed
  // its first edit one reinterpreted token later (the missing-close-delimiter
  // shape: insert the delimiter vs. reinterpret the next declaration).
  const auto insertOnly = make_ranked_attempt(
      detail::RecoveryAttemptStatus::Selectable, true, 37u, 37u, 1u, 18u, 0u, 1u);
  const auto cascade = make_ranked_attempt(
      detail::RecoveryAttemptStatus::Selectable, true, 37u, 37u, 5u, 27u, 1u, 1u);

  EXPECT_TRUE(detail::is_better_recovery_attempt(insertOnly, cascade));
  EXPECT_FALSE(detail::is_better_recovery_attempt(cascade, insertOnly));
}

TEST(RecoverySearchTest,
     GlobalRecoveryRankingLetsLaterLocalRepairBeatSameCostSingleDelete) {
  const auto wholeLineDelete = make_ranked_attempt(
      detail::RecoveryAttemptStatus::Selectable, true, 80u, 80u, 8u, 40u, 12u, 1u);
  const auto localRepair = make_ranked_attempt(
      detail::RecoveryAttemptStatus::Selectable, true, 80u, 80u, 8u, 41u, 12u, 2u);

  EXPECT_TRUE(detail::is_better_recovery_attempt(localRepair, wholeLineDelete));
  EXPECT_FALSE(
      detail::is_better_recovery_attempt(wholeLineDelete, localRepair));
}

TEST(RecoverySearchTest,
     GlobalRecoveryRankingPrefersFullerRecoveryBeforeCheaperPartialAttempt) {
  const auto full = make_ranked_attempt(detail::RecoveryAttemptStatus::Selectable,
                                        true, 24u, 24u, 12u, 8u, 0u, 2u);
  const auto partial = make_ranked_attempt(detail::RecoveryAttemptStatus::Selectable,
                                           false, 20u, 20u, 1u, 12u, 0u, 1u);

  EXPECT_TRUE(detail::is_better_recovery_attempt(full, partial));
  EXPECT_FALSE(detail::is_better_recovery_attempt(partial, full));
}

TEST(RecoverySearchTest, RecoveryKeyRanksByAxisPriority) {
  // Each row isolates one comparator axis: `better` must beat `worse` and the
  // reverse must not hold. The matched-axis row keeps adversarial losing fields
  // (worse offset/cost/progress) to prove the matched axis dominates them all.
  struct Row {
    const char *axis;
    detail::RecoveryKey better;
    detail::RecoveryKey worse;
  };
  const Row rows[] = {
      {"matched beats unmatched despite worse offset/cost/progress",
       {.matched = true,
        .firstEditOffset = 0,
        .rankAnchorOffset = 0,
        .editCost = 100,
        .progressAfterEdits = 0},
       {.matched = false,
        .firstEditOffset = 100,
        .rankAnchorOffset = 100,
        .editCost = 0,
        .progressAfterEdits = 100}},
      {"later rank anchor wins",
       {.matched = true,
        .firstEditOffset = 20,
        .rankAnchorOffset = 20,
        .editCost = 5,
        .progressAfterEdits = 40},
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 5,
        .progressAfterEdits = 40}},
      {"lower edit cost wins at same anchor",
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 1,
        .progressAfterEdits = 20},
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 5,
        .progressAfterEdits = 40}},
      // Insert transparency (attempt-level keys): an insert-only attempt's
      // rank anchor is its own progress, so a destructive token-theft
      // cascade cannot outrank it by pushing its first edit one
      // reinterpreted token later. The family anchor (firstEditOffset)
      // still records the insert site.
      {"insert-only repair folds past a destructive cascade one token later",
       {.matched = true,
        .firstEditOffset = 18,
        .rankAnchorOffset = 37,
        .editCost = 1,
        .editCount = 1,
        .progressAfterEdits = 37},
       {.matched = true,
        .firstEditOffset = 25,
        .rankAnchorOffset = 25,
        .editCost = 5,
        .editCount = 2,
        .progressAfterEdits = 37}},
      // Axis 4 fuses cost+count into faithfulness = editCost + W*editCount
      // (W = kFirstEditPenaltyStep = 4). The next three rows pin its behavior.
      // (a) Exact-cost tie resolves by parsimony: fewer operations win.
      //     faithfulness 128+4*1 = 132 BEATS 128+4*2 = 136.
      {"exact-cost tie: fewer edit operations win (parsimony)",
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 128,
        .editCount = 1,
        .progressAfterEdits = 40},
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 128,
        .editCount = 2,
        .progressAfterEdits = 40}},
      // (b) Near-tie: a strictly cheaper-but-larger fold still loses to the
      //     parsimonious single operation once the per-operation tax applies.
      //     faithfulness 128+4*1 = 132 BEATS 126+4*2 = 134, even though the
      //     fold's raw editCost (126) is the cheaper of the two.
      {"near-tie: single faithful edit beats a slightly cheaper two-edit fold",
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 128,
        .editCount = 1,
        .progressAfterEdits = 40},
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 126,
        .editCount = 2,
        .progressAfterEdits = 40}},
      // (c) Count-dominant trap avoided: a genuinely cheaper many-op recovery
      //     still wins, because the tax is additive on top of cost, never a
      //     standalone count tiebreak. faithfulness 2+4*2 = 10 BEATS 8+4*1 = 12.
      //     Anchors are picked so axis 3 (rank-anchor score) ties first — the
      //     cost-2 cand pays no offset penalty (score 10) and the cost-8 cand
      //     pays one step at anchor 11 (monus(11, 1) = 10) — so axis 4 decides.
      {"count-dominant trap: genuinely cheaper many-op recovery still wins",
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 2,
        .editCount = 2,
        .progressAfterEdits = 40},
       {.matched = true,
        .firstEditOffset = 11,
        .rankAnchorOffset = 11,
        .editCost = 8,
        .editCount = 1,
        .progressAfterEdits = 40}},
      {"more progress after edits breaks the tie",
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 2,
        .progressAfterEdits = 40},
       {.matched = true,
        .firstEditOffset = 10,
        .rankAnchorOffset = 10,
        .editCost = 2,
        .progressAfterEdits = 30}},
  };

  for (const auto &row : rows) {
    SCOPED_TRACE(row.axis);
    EXPECT_TRUE(detail::is_better_recovery_key(row.better, row.worse));
    EXPECT_FALSE(detail::is_better_recovery_key(row.worse, row.better));
  }
}

TEST(RecoverySearchTest, EffectiveFirstEditOffsetFoldsNoEditToProgress) {
  // Single-sourced rule shared by every RecoveryKey builder: a candidate that
  // made edits ranks on its first edit offset; one that made none ranks on its
  // own forward progress so a UINT_MAX "no edit" sentinel never wins axis 2.
  EXPECT_EQ(detail::effective_first_edit_offset(true, 9u, 20u), 9u);
  EXPECT_EQ(detail::effective_first_edit_offset(false, 9u, 20u), 20u);
  static_assert(detail::effective_first_edit_offset(true, 5u, 7u) == 5u);
  static_assert(detail::effective_first_edit_offset(false, 5u, 7u) == 7u);
}

TEST(RecoverySearchTest, FirstEditScorePenaltyCurveStaysDecoupledFromCostTable) {
  using detail::recovery_key_first_edit_score;
  // Edit cost up to one penalty step pays no offset penalty.
  EXPECT_EQ(recovery_key_first_edit_score({.rankAnchorOffset = 100, .editCost = 0}),
            100u);
  EXPECT_EQ(recovery_key_first_edit_score({.rankAnchorOffset = 100, .editCost = 4}),
            100u);
  // Each additional penalty step subtracts one offset unit.
  EXPECT_EQ(recovery_key_first_edit_score({.rankAnchorOffset = 100, .editCost = 8}),
            99u);
  EXPECT_EQ(
      recovery_key_first_edit_score({.rankAnchorOffset = 100, .editCost = 12}),
      98u);
  // The penalty saturates at zero and never underflows the offset.
  EXPECT_EQ(recovery_key_first_edit_score({.rankAnchorOffset = 1, .editCost = 100}),
            0u);
}

TEST(RecoverySearchTest, EditableRecoveryKeyProjectsAxesFromCandidate) {
  // The ranking key reads the producer-chosen keyProgressOffset for
  // progressAfterEdits (here distinct from postSkipCursorOffset to pin which
  // field drives the axis).
  const detail::EditableRecoveryCandidate candidate{
      .matched = true,
      .postSkipCursorOffset = 18,
      .keyProgressOffset = 20,
      .editCost = 3u,
      .editCount = 2u,
      .firstEditOffset = 9u,
  };

  const auto key = detail::editable_recovery_key(candidate);
  EXPECT_TRUE(key.matched);
  EXPECT_EQ(key.firstEditOffset, 9u);
  EXPECT_EQ(key.editCost, 3u);
  EXPECT_EQ(key.progressAfterEdits, 20u);
}

TEST(RecoverySearchTest, TerminalRecoveryKeyProjectsFromCandidate) {
  // Terminal candidates project cost axis from primaryRankCost (logical edit
  // distance + strategy penalty), not budgetCost (raw deleted/inserted span).
  // This keeps a cheap-looking synthetic-gap split from beating a fuzzy
  // keyword repair that describes a single logical substitution.
  const detail::TerminalRecoveryCandidate candidate{
      .kind = detail::TerminalRecoveryChoiceKind::DeleteScan,
      .cost = {.budgetCost = 4u, .primaryRankCost = 2u},
      .consumed = 7u,
  };

  const auto key = detail::terminal_recovery_key(candidate);
  EXPECT_TRUE(key.matched);
  EXPECT_EQ(key.editCost, 2u);
  EXPECT_EQ(key.progressAfterEdits, 7u);
}

TEST(RecoverySearchTest,
     EditableCandidateContinuationUsesVisiblePostSkipAfterZeroWidthEdit) {
  const detail::EditableRecoveryCandidate candidate{
      .matched = true,
      .postSkipCursorOffset = 18,
      .editCost = 1,
      .editCount = 1,
      .lastEditEndOffset = 17,
      .firstEditOffset = 17,
  };

  EXPECT_TRUE(detail::continues_after_first_edit(candidate));
}

// Obsolete axis-projection and per-profile comparator tests were removed in
// the recovery rewrite (a single 4-axis RecoveryKey replaced the
// former 24-axis / 3-profile normalized key). The surviving behavior is
// exercised by the RecoveryKey* tests above and by the end-to-end recovery
// sample tests elsewhere.

TEST(RecoverySearchTest, RecoveryAttemptDebugJsonTracksEditedSourceSpan) {
  detail::RecoveryAttempt attempt;
  attempt.entryRuleMatched = true;
  attempt.status = detail::RecoveryAttemptStatus::Selectable;
  attempt.parsedLength = 40u;
  attempt.maxCursorOffset = 40u;
  attempt.editCost = 9u;
  attempt.editCount = 2u;
  set_recovery_edits(attempt, {{.kind = ParseDiagnosticKind::Deleted,
                                .offset = 9u,
                                .beginOffset = 9u,
                                .endOffset = 17u,
                                .element = nullptr,
                                .message = {}},
                               {.kind = ParseDiagnosticKind::Deleted,
                                .offset = 28u,
                                .beginOffset = 28u,
                                .endOffset = 31u,
                                .element = nullptr,
                                .message = {}}});

  const auto json = detail::recovery_attempt_to_json(attempt);
  const auto &object = json.object();
  const auto &editTrace = object.at("editTrace").object();

  EXPECT_EQ(editTrace.at("firstEditOffset").integer(), 9);
  EXPECT_EQ(editTrace.at("lastEditOffset").integer(), 31);
  EXPECT_EQ(editTrace.at("editSpan").integer(), 22);
}

TEST(RecoverySearchTest, WordLiteralSingleSubstitutionCanRecoverGenerically) {
  ParserRule<RecoverySearchNode> entry{
      "Entry", assign<&RecoverySearchNode::name>("service"_kw)};

  const auto result = pegium::test::Parse(entry, "servixe");

  EXPECT_TRUE(result.fullMatch);
  ASSERT_TRUE(result.value);
}

TEST(RecoverySearchTest,
     LongWordLiteralSingleSubstitutionCanRecoverGenerically) {
  ParserRule<RecoverySearchNode> entry{
      "Entry", assign<&RecoverySearchNode::name>("catalogue"_kw)};

  const auto result = pegium::test::Parse(entry, "catalogoe");
  EXPECT_TRUE(result.fullMatch);
  ASSERT_TRUE(result.value);
}

TEST(RecoverySearchTest,
     FirstSelectableReplacementAtFileStartIsNotRejectedAsBoundaryRewrite) {
  const auto whitespace = some(s);
  const auto skipper = SkipperBuilder().ignore(whitespace).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchNode> entry{
      "Entry", "module"_kw + assign<&RecoverySearchNode::name>(id)};

  const auto text = pegium::text::TextSnapshot::copy("modle basicMath");
  const auto run =
      detail::orchestrate_recovery_search(entry, skipper, ParseOptions{}, text);
  const auto json = detail::recovery_search_run_to_json(run);

  EXPECT_TRUE(run.selectedAttempt.entryRuleMatched) << json;
  EXPECT_TRUE(run.selectedAttempt.fullMatch) << json;
  EXPECT_EQ(run.selectedAttempt.status, detail::RecoveryAttemptStatus::Selectable)
      << json;
  ASSERT_EQ(run.selectedAttempt.recoveryEdits.size(), 1u) << json;
  EXPECT_EQ(run.selectedAttempt.recoveryEdits.front().kind,
            ParseDiagnosticKind::Replaced)
      << json;
  EXPECT_EQ(run.selectedAttempt.recoveryEdits.front().offset, 0u) << json;
}

TEST(RecoverySearchTest,
     ParserUsesRecoveredButNotCredibleFallbackWhenNoSelectableAttemptExists) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryTripleNode> entry{
      "Entry", assign<&RecoveryTripleNode::first>(id) + ";"_kw +
                   assign<&RecoveryTripleNode::second>(id) + ";"_kw +
                   assign<&RecoveryTripleNode::third>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 2;
  options.maxRecoveryEditsPerAttempt = 1;

  const auto result =
      pegium::test::Parse(entry, "one two three", skipper, options);
  const auto text = pegium::text::TextSnapshot::copy("one two three");
  const auto failureAnalysis = inspect_failure(entry, skipper, text);
  const auto &strictSummary = failureAnalysis.strictResult.summary;
  const auto editFloorOffset =
      strictSummary.parsedLength != 0 ||
              !failureAnalysis.snapshot.hasFailureToken
          ? strictSummary.parsedLength
          : failureAnalysis.snapshot
                .failureLeafHistory[failureAnalysis.snapshot.failureTokenIndex]
                .beginOffset;
  const auto window = detail::compute_recovery_window(
      failureAnalysis.snapshot, options.recoveryWindowTokenCount,
      options.recoveryWindowTokenCount, editFloorOffset);
  const auto spec = build_attempt_spec({}, window);
  bool foundSelectableAttempt = false;
  auto attempt =
      detail::execute_recovery_parse(entry, skipper, options, text, spec);
  detail::classify_recovery_attempt(attempt);
  foundSelectableAttempt |= detail::is_selectable_recovery_attempt(attempt);

  EXPECT_TRUE(result.fullMatch);
  EXPECT_FALSE(foundSelectableAttempt);
  ASSERT_TRUE(result.value);
  auto *typed = pegium::ast_ptr_cast<RecoveryTripleNode>(result.value);
  ASSERT_TRUE(typed != nullptr);
  EXPECT_EQ(typed->first, "one");
  EXPECT_EQ(typed->second, "two");
  EXPECT_EQ(typed->third, "three");
  ASSERT_EQ(result.parseDiagnostics.size(), 2u);
  EXPECT_EQ(result.parseDiagnostics[0].kind, ParseDiagnosticKind::Inserted);
  EXPECT_EQ(result.parseDiagnostics[1].kind, ParseDiagnosticKind::Inserted);
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 2u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
}

TEST(RecoverySearchTest,
     ExtendedFallbackPreservingEarlierEditsCanUseFallbackSelection) {
  detail::RecoveryAttempt selectedAttempt;
  selectedAttempt.entryRuleMatched = true;
  selectedAttempt.hasStablePrefix = true;
  selectedAttempt.stablePrefixOffset = 10;
  selectedAttempt.parsedLength = 27;
  selectedAttempt.lastVisibleCursorOffset = 27;
  selectedAttempt.maxCursorOffset = 34;
  selectedAttempt.configuredMaxEditCost = 64;
  selectedAttempt.editCost = 1;
  set_recovery_edits(selectedAttempt, {{.kind = ParseDiagnosticKind::Inserted,
                                        .offset = 13,
                                        .beginOffset = 13,
                                        .endOffset = 13,
                                        .element = nullptr,
                                        .message = {}}});
  detail::classify_recovery_attempt(selectedAttempt);
  ASSERT_EQ(selectedAttempt.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);

  detail::RecoveryAttempt candidate;
  candidate.entryRuleMatched = true;
  candidate.hasStablePrefix = true;
  candidate.stablePrefixOffset = 10;
  candidate.parsedLength = 36;
  candidate.lastVisibleCursorOffset = 27;
  candidate.maxCursorOffset = 42;
  candidate.configuredMaxEditCost = 64;
  candidate.editCost = 4;
  set_recovery_edits(candidate, {{.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 13,
                                  .beginOffset = 13,
                                  .endOffset = 13,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 32,
                                  .beginOffset = 32,
                                  .endOffset = 32,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 34,
                                  .beginOffset = 34,
                                  .endOffset = 34,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 36,
                                  .beginOffset = 36,
                                  .endOffset = 36,
                                  .element = nullptr,
                                  .message = {}}});
  detail::classify_recovery_attempt(candidate);

  EXPECT_EQ(candidate.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);
  EXPECT_TRUE(detail::satisfies_non_credible_fallback_contract(candidate, {}));
  EXPECT_TRUE(detail::satisfies_non_credible_fallback_contract(
      candidate, selectedAttempt));
}

TEST(RecoverySearchTest,
     StableReplayContractFallbackExtensionMustPreserveEarlierEdits) {
  detail::RecoveryAttempt selectedAttempt;
  selectedAttempt.entryRuleMatched = true;
  selectedAttempt.hasStablePrefix = true;
  selectedAttempt.stablePrefixOffset = 10;
  selectedAttempt.parsedLength = 27;
  selectedAttempt.lastVisibleCursorOffset = 27;
  selectedAttempt.maxCursorOffset = 34;
  selectedAttempt.stableAfterRecovery = true;
  selectedAttempt.configuredMaxEditCost = 64;
  selectedAttempt.editCost = 1;
  set_recovery_edits(selectedAttempt, {{.kind = ParseDiagnosticKind::Inserted,
                                        .offset = 13,
                                        .beginOffset = 13,
                                        .endOffset = 13,
                                        .element = nullptr,
                                        .message = {}}});
  detail::classify_recovery_attempt(selectedAttempt);
  ASSERT_EQ(selectedAttempt.status, detail::RecoveryAttemptStatus::Selectable);

  detail::RecoveryAttempt candidate;
  candidate.entryRuleMatched = true;
  candidate.hasStablePrefix = true;
  candidate.stablePrefixOffset = 10;
  candidate.parsedLength = 36;
  candidate.lastVisibleCursorOffset = 27;
  candidate.maxCursorOffset = 42;
  candidate.configuredMaxEditCost = 64;
  candidate.editCost = 3;
  set_recovery_edits(candidate, {{.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 32,
                                  .beginOffset = 32,
                                  .endOffset = 32,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 34,
                                  .beginOffset = 34,
                                  .endOffset = 34,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 36,
                                  .beginOffset = 36,
                                  .endOffset = 36,
                                  .element = nullptr,
                                  .message = {}}});
  detail::classify_recovery_attempt(candidate);

  EXPECT_EQ(candidate.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);
  EXPECT_FALSE(detail::satisfies_non_credible_fallback_contract(
      candidate, selectedAttempt));
}

TEST(RecoverySearchTest,
     StableReplayContractCanCarryLaterFallbackExtensionWhenEditsArePreserved) {
  detail::RecoveryAttempt selectedAttempt;
  selectedAttempt.entryRuleMatched = true;
  selectedAttempt.hasStablePrefix = true;
  selectedAttempt.stablePrefixOffset = 10;
  selectedAttempt.parsedLength = 27;
  selectedAttempt.lastVisibleCursorOffset = 27;
  selectedAttempt.maxCursorOffset = 34;
  selectedAttempt.stableAfterRecovery = true;
  selectedAttempt.configuredMaxEditCost = 64;
  selectedAttempt.editCost = 1;
  set_recovery_edits(selectedAttempt, {{.kind = ParseDiagnosticKind::Inserted,
                                        .offset = 13,
                                        .beginOffset = 13,
                                        .endOffset = 13,
                                        .element = nullptr,
                                        .message = {}}});
  detail::classify_recovery_attempt(selectedAttempt);
  ASSERT_EQ(selectedAttempt.status, detail::RecoveryAttemptStatus::Selectable);

  detail::RecoveryAttempt candidate;
  candidate.entryRuleMatched = true;
  candidate.hasStablePrefix = true;
  candidate.stablePrefixOffset = 10;
  candidate.parsedLength = 36;
  candidate.lastVisibleCursorOffset = 27;
  candidate.maxCursorOffset = 42;
  candidate.configuredMaxEditCost = 64;
  candidate.editCost = 4;
  set_recovery_edits(candidate, {{.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 13,
                                  .beginOffset = 13,
                                  .endOffset = 13,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 32,
                                  .beginOffset = 32,
                                  .endOffset = 32,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 34,
                                  .beginOffset = 34,
                                  .endOffset = 34,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 36,
                                  .beginOffset = 36,
                                  .endOffset = 36,
                                  .element = nullptr,
                                  .message = {}}});
  detail::classify_recovery_attempt(candidate);

  EXPECT_EQ(candidate.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);
  EXPECT_TRUE(detail::satisfies_non_credible_fallback_contract(
      candidate, selectedAttempt));
}

TEST(RecoverySearchTest,
     ExtendedFallbackCannotRewriteFirstCommittedSourceAtResumeFloor) {
  detail::RecoveryAttempt selectedAttempt;
  selectedAttempt.entryRuleMatched = true;
  selectedAttempt.hasStablePrefix = true;
  selectedAttempt.stablePrefixOffset = 10;
  selectedAttempt.parsedLength = 27;
  selectedAttempt.lastVisibleCursorOffset = 27;
  selectedAttempt.maxCursorOffset = 34;
  selectedAttempt.configuredMaxEditCost = 64;
  selectedAttempt.editCost = 1;
  set_recovery_edits(selectedAttempt, {{.kind = ParseDiagnosticKind::Inserted,
                                        .offset = 13,
                                        .beginOffset = 13,
                                        .endOffset = 13,
                                        .element = nullptr,
                                        .message = {}}});
  detail::classify_recovery_attempt(selectedAttempt);
  ASSERT_EQ(selectedAttempt.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);

  detail::RecoveryAttempt candidate;
  candidate.entryRuleMatched = true;
  candidate.hasStablePrefix = true;
  candidate.stablePrefixOffset = 10;
  candidate.parsedLength = 36;
  candidate.lastVisibleCursorOffset = 27;
  candidate.maxCursorOffset = 42;
  candidate.configuredMaxEditCost = 64;
  candidate.editCost = 4;
  set_recovery_edits(candidate, {{.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 13,
                                  .beginOffset = 13,
                                  .endOffset = 13,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 27,
                                  .beginOffset = 27,
                                  .endOffset = 27,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Deleted,
                                  .offset = 27,
                                  .beginOffset = 27,
                                  .endOffset = 28,
                                  .element = nullptr,
                                  .message = {}},
                                 {.kind = ParseDiagnosticKind::Inserted,
                                  .offset = 32,
                                  .beginOffset = 32,
                                  .endOffset = 32,
                                  .element = nullptr,
                                  .message = {}}});
  detail::classify_recovery_attempt(candidate);

  EXPECT_EQ(candidate.status,
            detail::RecoveryAttemptStatus::RecoveredButNotCredible);
  EXPECT_TRUE(detail::satisfies_non_credible_fallback_contract(candidate, {}));
  EXPECT_FALSE(detail::satisfies_non_credible_fallback_contract(
      candidate, selectedAttempt));
}

TEST(RecoverySearchTest, EditBudgetCanDisableRecoveryAttempts) {
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoverySearchNode> entry{
      "Entry", assign<&RecoverySearchNode::name>(id) + "{"_kw + "}"_kw};

  ParseOptions options;
  options.maxRecoveryEditsPerAttempt = 0;
  const auto result =
      pegium::test::Parse(entry, "hello{", NoOpSkipper(), options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": []
})json");
  EXPECT_FALSE(result.fullMatch);
}

TEST(RecoverySearchTest, SingleWindowResumesStrictParsingAfterRecovery) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryPairNode> entry{
      "Entry", assign<&RecoveryPairNode::first>(id) + ";"_kw +
                   assign<&RecoveryPairNode::second>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;
  const auto result = pegium::test::Parse(entry, "one two", skipper, options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 3,
          "grammarSource": "first=ID",
          "text": "one"
        },
        {
          "begin": 4,
          "end": 4,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 4,
          "end": 7,
          "grammarSource": "second=ID",
          "text": "two"
        }
      ],
      "end": 7,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json");
  EXPECT_TRUE(result.fullMatch);
  ASSERT_EQ(result.parseDiagnostics.size(), 1u);
  EXPECT_EQ(result.parseDiagnostics.front().kind,
            ParseDiagnosticKind::Inserted);
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 1u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 1u);
  ASSERT_TRUE(result.recoveryReport.lastRecoveryWindow.has_value());
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->beginOffset, 0u);
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->maxCursorOffset, 4u);
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->backwardTokenCount, 1u);
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->forwardTokenCount, 2u);
}

TEST(RecoverySearchTest,
     ParserCanRecoverSeparatedErrorsWithinSingleStablePrefixWindow) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryQuadNode> entry{
      "Entry", assign<&RecoveryQuadNode::first>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::second>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::third>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::fourth>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;
  const auto result =
      pegium::test::Parse(entry, "one;two three four", skipper, options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 3,
          "grammarSource": "first=ID",
          "text": "one"
        },
        {
          "begin": 3,
          "end": 4,
          "grammarSource": "Literal",
          "text": ";"
        },
        {
          "begin": 4,
          "end": 7,
          "grammarSource": "second=ID",
          "text": "two"
        },
        {
          "begin": 8,
          "end": 8,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 8,
          "end": 13,
          "grammarSource": "third=ID",
          "text": "three"
        },
        {
          "begin": 14,
          "end": 14,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 14,
          "end": 18,
          "grammarSource": "fourth=ID",
          "text": "four"
        }
      ],
      "end": 18,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json",
                          kRecoveryCstJsonOptions);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_EQ(result.parseDiagnostics.size(), 2u);
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 1u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
  EXPECT_LT(result.recoveryReport.recoveryAttemptRuns, 256u);
  ASSERT_TRUE(result.recoveryReport.lastRecoveryWindow.has_value());
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->backwardTokenCount, 1u);
  EXPECT_GE(result.recoveryReport.lastRecoveryWindow->forwardTokenCount, 2u);
  EXPECT_GE(result.recoveryReport.lastRecoveryWindow->beginOffset, 3u);
  EXPECT_GE(result.recoveryReport.lastRecoveryWindow->maxCursorOffset,
            result.recoveryReport.lastRecoveryWindow->beginOffset);
}

TEST(RecoverySearchTest,
     GlobalAttemptBudgetStillCapsLocalRecoveryToOneAttemptRun) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryQuadNode> entry{
      "Entry", assign<&RecoveryQuadNode::first>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::second>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::third>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::fourth>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;
  options.maxRecoveryAttempts = 1;

  const auto result =
      pegium::test::Parse(entry, "one;two three four", skipper, options);

  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 1u);
  EXPECT_EQ(result.recoveryReport.recoveryAttemptRuns, 1u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
  ASSERT_TRUE(result.recoveryReport.lastRecoveryWindow.has_value());
  EXPECT_EQ(result.recoveryReport.lastRecoveryWindow->backwardTokenCount, 1u);
  EXPECT_GE(result.recoveryReport.lastRecoveryWindow->forwardTokenCount, 2u);
}

TEST(RecoverySearchTest,
     ParserCanRecoverSeparatedErrorsWithTriviaWithinSingleStablePrefixWindow) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryQuadNode> entry{
      "Entry", assign<&RecoveryQuadNode::first>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::second>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::third>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::fourth>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;

  const auto result =
      pegium::test::Parse(entry, "one;\n\ntwo three\n\nfour", skipper, options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 3,
          "grammarSource": "first=ID",
          "text": "one"
        },
        {
          "begin": 3,
          "end": 4,
          "grammarSource": "Literal",
          "text": ";"
        },
        {
          "begin": 6,
          "end": 9,
          "grammarSource": "second=ID",
          "text": "two"
        },
        {
          "begin": 10,
          "end": 10,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 10,
          "end": 15,
          "grammarSource": "third=ID",
          "text": "three"
        },
        {
          "begin": 17,
          "end": 17,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 17,
          "end": 21,
          "grammarSource": "fourth=ID",
          "text": "four"
        }
      ],
      "end": 21,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json",
                          kRecoveryCstJsonOptions);
  EXPECT_TRUE(result.fullMatch);
  EXPECT_EQ(result.parseDiagnostics.size(), 2u);
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 1u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
}

TEST(RecoverySearchTest,
     LaterRecoveryWindowGetsItsOwnLocalEditBudgetDuringReplay) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryQuadNode> entry{
      "Entry", assign<&RecoveryQuadNode::first>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::second>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::third>(id) + ";"_kw +
                   assign<&RecoveryQuadNode::fourth>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;
  options.maxRecoveryEditsPerAttempt = 1;

  const auto result =
      pegium::test::Parse(entry, "one;two three four", skipper, options);

  EXPECT_TRUE(result.fullMatch);
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 2u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
}

TEST(RecoverySearchTest,
     ParserRepairsSupportedMultiEditWordLiteralAndContinues) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryPairNode> entry{
      "Entry", "catalogue"_kw + assign<&RecoveryPairNode::first>(id) + ";"_kw +
                   assign<&RecoveryPairNode::second>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;

  const auto result =
      pegium::test::Parse(entry, "catalogxoe demo next", skipper, options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 10,
          "grammarSource": "Literal",
          "recovered": true,
          "text": "catalogxoe"
        },
        {
          "begin": 11,
          "end": 15,
          "grammarSource": "first=ID",
          "text": "demo"
        },
        {
          "begin": 16,
          "end": 16,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 16,
          "end": 20,
          "grammarSource": "second=ID",
          "text": "next"
        }
      ],
      "end": 20,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json",
                          kRecoveryCstJsonOptions);
  EXPECT_TRUE(result.fullMatch);
  ASSERT_EQ(result.parseDiagnostics.size(), 2u);
  ASSERT_TRUE(result.value);
}

TEST(RecoverySearchTest,
     ParserRejectsWordLiteralWhenFuzzyScoreStaysTooPoorInFirstWindow) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryPairNode> entry{
      "Entry", "catalogue"_kw + assign<&RecoveryPairNode::first>(id) + ";"_kw +
                   assign<&RecoveryPairNode::second>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;

  const auto result =
      pegium::test::Parse(entry, "cxtxlgxxe demo next", skipper, options);

  pegium::test::ExpectCst(result, R"json({
  "content": []
})json",
                          kRecoveryCstJsonOptions);
  EXPECT_FALSE(result.fullMatch);
  EXPECT_FALSE(result.value);
  ASSERT_FALSE(result.parseDiagnostics.empty());
}

TEST(RecoverySearchTest,
     ParserSingleWindowRepairsNearestLocalErrorBeforeStoppingAtNextOne) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryQuintNode> entry{
      "Entry", assign<&RecoveryQuintNode::first>(id) + ";"_kw +
                   assign<&RecoveryQuintNode::second>(id) + ";"_kw +
                   assign<&RecoveryQuintNode::third>(id) + ";"_kw +
                   assign<&RecoveryQuintNode::fourth>(id) + ";"_kw +
                   assign<&RecoveryQuintNode::fifth>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  ParseOptions options;
  options.recoveryWindowTokenCount = 1;
  const auto result =
      pegium::test::Parse(entry, "one;two three;four five", skipper, options);

  pegium::test::ExpectCst(result,
                          R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 3,
          "grammarSource": "first=ID",
          "text": "one"
        },
        {
          "begin": 3,
          "end": 4,
          "grammarSource": "Literal",
          "text": ";"
        },
        {
          "begin": 4,
          "end": 7,
          "grammarSource": "second=ID",
          "text": "two"
        },
        {
          "begin": 8,
          "end": 8,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 8,
          "end": 13,
          "grammarSource": "third=ID",
          "text": "three"
        },
        {
          "begin": 13,
          "end": 14,
          "grammarSource": "Literal",
          "text": ";"
        },
        {
          "begin": 14,
          "end": 18,
          "grammarSource": "fourth=ID",
          "text": "four"
        },
        {
          "begin": 19,
          "end": 19,
          "grammarSource": "Literal",
          "recovered": true,
          "text": ""
        },
        {
          "begin": 19,
          "end": 23,
          "grammarSource": "fifth=ID",
          "text": "five"
        }
      ],
      "end": 23,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json",
                          kRecoveryCstJsonOptions);
  EXPECT_TRUE(result.fullMatch);
  ASSERT_EQ(result.parseDiagnostics.size(), 2u);
  EXPECT_EQ(result.parseDiagnostics.front().kind, ParseDiagnosticKind::Inserted);
  EXPECT_EQ(result.parseDiagnostics.back().kind, ParseDiagnosticKind::Inserted);
  EXPECT_EQ(result.parseDiagnostics.back().offset, 19u);
  EXPECT_EQ(result.parseDiagnostics.back().beginOffset, 19u);
  EXPECT_EQ(result.parseDiagnostics.back().endOffset, 19u);
  EXPECT_EQ(result.parseDiagnostics.back().message, "");
  EXPECT_TRUE(result.recoveryReport.hasRecovered);
  EXPECT_TRUE(result.recoveryReport.fullRecovered);
  EXPECT_EQ(result.recoveryReport.recoveryCount, 2u);
  EXPECT_EQ(result.recoveryReport.recoveryEdits, 2u);
  EXPECT_LT(result.recoveryReport.recoveryAttemptRuns, 256u);
  ASSERT_TRUE(result.recoveryReport.lastRecoveryWindow.has_value());
}

// Regression test for the cached-facts contract footgun: a caller that
// builds a `RecoveryAttempt` with edits but forgets to call
// `classify_recovery_attempt` must NOT see a silently zero-edit ranking
// key. `recovery_attempt_key` reads the edits directly so the key always
// reflects reality, regardless of whether the cached `attempt.facts` is
// in sync.
TEST(RecoverySearchTest, RecoveryAttemptKeyDoesNotDependOnCachedFacts) {
  detail::RecoveryAttempt attempt;
  attempt.entryRuleMatched = true;
  attempt.fullMatch = true;
  attempt.parsedLength = 24;
  attempt.editCost = 8;
  attempt.status = detail::RecoveryAttemptStatus::Selectable;
  attempt.recoveryEdits.push_back({.kind = ParseDiagnosticKind::Deleted,
                                   .offset = 12,
                                   .beginOffset = 12,
                                   .endOffset = 14,
                                   .element = nullptr,
                                   .message = {}});
  // `attempt.facts` deliberately left default-constructed (no classify).

  const auto key = detail::recovery_attempt_key(attempt);

  EXPECT_TRUE(key.fullMatch);
  EXPECT_TRUE(key.matched);
  EXPECT_EQ(key.firstEditOffset, 12u);
  EXPECT_EQ(key.editCost, 8u);
  EXPECT_EQ(key.progressAfterEdits, 24u);
}

TEST(RecoverySearchTest,
     RecoveryAttemptKeyOnEmptyEditsFallsBackToParsedLength) {
  detail::RecoveryAttempt attempt;
  attempt.entryRuleMatched = true;
  attempt.fullMatch = true;
  attempt.parsedLength = 32;
  attempt.editCost = 0;
  attempt.status = detail::RecoveryAttemptStatus::Selectable;
  // No recovery edits. `firstEditOffset` should fall back to parsedLength.

  const auto key = detail::recovery_attempt_key(attempt);

  EXPECT_TRUE(key.fullMatch);
  EXPECT_TRUE(key.matched);
  EXPECT_EQ(key.firstEditOffset, 32u);
  EXPECT_EQ(key.editCost, 0u);
  EXPECT_EQ(key.progressAfterEdits, 32u);
}

TEST(RecoverySearchTest,
     RecoveryAttemptKeyMatchedFalseWhenStatusIsNotSelectable) {
  detail::RecoveryAttempt attempt;
  attempt.entryRuleMatched = true;
  attempt.parsedLength = 16;
  attempt.editCost = 4;
  attempt.status = detail::RecoveryAttemptStatus::RecoveredButNotCredible;
  attempt.recoveryEdits.push_back({.kind = ParseDiagnosticKind::Inserted,
                                   .offset = 8,
                                   .beginOffset = 8,
                                   .endOffset = 8,
                                   .element = nullptr,
                                   .message = {}});

  const auto key = detail::recovery_attempt_key(attempt);

  EXPECT_FALSE(key.matched);
  EXPECT_EQ(key.firstEditOffset, 8u);
}

} // namespace
