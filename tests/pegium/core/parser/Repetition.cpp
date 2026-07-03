#include <gtest/gtest.h>
#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <limits>

using namespace pegium::parser;

namespace {

struct RepetitionProbeContact : pegium::AstNode {
  std::string userName;
};

struct RepetitionProbeModel : pegium::AstNode {
  std::unique_ptr<RepetitionProbeContact> contact;
};

struct RepetitionProbeTransition : pegium::AstNode {
  bool many = false;
  std::string event;
  std::string target;
};

struct RepetitionProbeState : pegium::AstNode {
  std::string name;
  std::vector<pointer<RepetitionProbeTransition>> transitions;
  std::vector<pointer<RepetitionProbeTransition>> nextTransitions;
};

} // namespace

TEST(RepetitionTest, OptionSomeAndManyBehaveAsExpected) {
  {
    std::string input = "";
    auto result = option("a"_kw).terminal(input);
    EXPECT_EQ(result, (input).c_str());
  }

  {
    std::string input = "";
    auto result = some("a"_kw).terminal(input);
    EXPECT_EQ(result, nullptr);
  }

  {
    std::string input = "aaab";
    auto result = many("a"_kw).terminal(input);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result - (input).c_str(), 3);
  }
}

TEST(RepetitionTest, ParseRuleHandlesOptionalStarAndPlusVariants) {
  auto skipper = SkipperBuilder().build();

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("b");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(option("a"_kw), ctx);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.cursor(), input.begin());
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("aaab");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(many(":"_kw), ctx);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.cursor() - input.begin(), 0);
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("aaab");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(some(":"_kw), ctx);
    EXPECT_FALSE(result);
    EXPECT_EQ(ctx.cursor() - input.begin(), 0);
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness(":::b");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(many(":"_kw), ctx);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.cursor() - input.begin(), 3);
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness(":::b");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(some(":"_kw), ctx);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.cursor() - input.begin(), 3);
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("bbb");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(some(":"_kw), ctx);
    EXPECT_FALSE(result);
    EXPECT_EQ(ctx.cursor(), input.begin());
  }
}

TEST(RepetitionTest, StarRewindsSkippedHiddenNodesWhenNextOccurrenceFails) {
  TerminalRule<> ws{"WS", some(s)};
  auto skipper = SkipperBuilder().hide(ws).build();
  auto expression = many("a"_kw);

  auto builderHarness = pegium::test::makeCstBuilderHarness("a   !");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  ParseContext ctx{builder, skipper};

  const bool result = parse(expression, ctx);
  EXPECT_TRUE(result);
  EXPECT_EQ(ctx.cursor() - input.begin(), 1);

  auto root = builder.getRootCstNode();
  auto it = root->begin();
  ASSERT_NE(it, root->end());
  EXPECT_FALSE((*it).isHidden());
  EXPECT_EQ((*it).getText(), "a");
  ++it;
  EXPECT_EQ(it, root->end());
}

TEST(RepetitionTest, WithLocalSkipperCanMatchSeparatedRepetitions) {
  TerminalRule<> ws{"WS", some(s)};
  auto defaultSkipper = skip();
  auto expression = some("a"_kw).skip(ignored(ws));

  auto builderHarness = pegium::test::makeCstBuilderHarness("a   a");
  auto &builder = builderHarness.builder;
  ParseContext ctx{builder, defaultSkipper};
  EXPECT_TRUE(parse(expression, ctx));
  EXPECT_EQ(ctx.cursorOffset(), 5U);
}

TEST(RepetitionTest, WithLocalSkipperRestoresOuterSkipperAfterMatch) {
  TerminalRule<> ws{"WS", some(s)};
  auto defaultSkipper = skip();
  auto expression = some("a"_kw).skip(ignored(ws));
  auto trailing = "b"_kw;

  auto builderHarness = pegium::test::makeCstBuilderHarness("a   a   b");
  auto &builder = builderHarness.builder;
  ParseContext ctx{builder, defaultSkipper};
  ASSERT_TRUE(parse(expression, ctx));
  EXPECT_EQ(ctx.cursorOffset(), 5U);

  const auto beforeSkip = ctx.cursorOffset();
  ctx.skip();
  EXPECT_EQ(ctx.cursorOffset(), beforeSkip);
  EXPECT_FALSE(parse(trailing, ctx));
}

TEST(RepetitionTest, FixedAndBoundedRepetitionsHandleLimits) {
  {
    std::string input = "aaab";
    auto result = repeat<2>("a"_kw).terminal(input);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result - (input).c_str(), 2);
  }

  {
    std::string input = "ab";
    auto result = repeat<2>("a"_kw).terminal(input);
    EXPECT_EQ(result, nullptr);
  }

  {
    std::string input = "aaab";
    auto result = repeat<1, 3>("a"_kw).terminal(input);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result - (input).c_str(), 3);
  }

  {
    std::string input = "a,a,a!";
    auto result = some("a"_kw, ","_kw).terminal(input);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result - (input).c_str(), 5);
  }
}

TEST(RepetitionTest, BoundedParseRuleRespectsMinAndMax) {
  auto skipper = SkipperBuilder().build();

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness(":::x");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(repeat<1, 3>(":"_kw), ctx);
    EXPECT_TRUE(result);
    EXPECT_EQ(ctx.cursor() - input.begin(), 3);
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("x");
    auto &builder = builderHarness.builder;
    const auto input = builder.getText();
    ParseContext ctx{builder, skipper};
    auto result = parse(repeat<1, 3>(":"_kw), ctx);
    EXPECT_FALSE(result);
    EXPECT_EQ(ctx.cursor(), input.begin());
  }
}

TEST(RepetitionTest, FixedParseRuleCanFailAfterPartialConsumption) {
  auto skipper = SkipperBuilder().build();
  auto builderHarness = pegium::test::makeCstBuilderHarness(":x");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();

  ParseContext ctx{builder, skipper};
  auto result = parse(repeat<2>(":"_kw), ctx);
  EXPECT_FALSE(result);
  EXPECT_EQ(ctx.cursor() - input.begin(), 1);
}

TEST(RepetitionTest, NullableWrappersDoNotFastProbeWhenElementDoesNotStart) {
  auto skipper = SkipperBuilder().build();

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("b");
    auto &builder = builderHarness.builder;
    ParseContext ctx{builder, skipper};
    EXPECT_FALSE(attempt_fast_probe(ctx, option("a"_kw)));
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("b");
    auto &builder = builderHarness.builder;
    ParseContext ctx{builder, skipper};
    EXPECT_FALSE(attempt_fast_probe(ctx, many("a"_kw)));
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("a");
    auto &builder = builderHarness.builder;
    ParseContext ctx{builder, skipper};
    EXPECT_TRUE(attempt_fast_probe(ctx, option("a"_kw)));
  }
}

TEST(RepetitionTest, OptionalAssignmentCanLookLocallyRecoverableWhenKeywordIsMisspelled) {
  TerminalRule<std::string> stringRule{
      "STRING", "\""_kw + many(!"\""_kw + dot) + "\""_kw};
  ParserRule<RepetitionProbeContact> contactRule{
      "Contact",
      "contact"_kw.i() + ":"_kw +
          assign<&RepetitionProbeContact::userName>(stringRule)};
  auto contactAssignment =
      assign<&RepetitionProbeModel::contact>(contactRule);
  auto optionalContact = option(contactAssignment);

  auto builderHarness = pegium::test::makeCstBuilderHarness("conttact:: \"qa\"");
  auto &builder = builderHarness.builder;
  auto skipper = SkipperBuilder().build();
  detail::FailureHistoryRecorder recorder(builder.input_begin());
  RecoveryContext ctx{builder, skipper, recorder};

  ctx.skip();

  EXPECT_TRUE(probe_locally_recoverable(contactAssignment, ctx));
  EXPECT_TRUE(optionalContact.probeRecoverable(ctx));
}

TEST(RepetitionTest, ProbeStartedWithoutEditsSeesVisibleDelimitedPrefix) {
  auto skipper = SkipperBuilder().build();
  auto delimited = "("_kw + "name"_kw + ")"_kw;

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("()");
    auto &builder = builderHarness.builder;
    detail::FailureHistoryRecorder recorder(builder.input_begin());
    RecoveryContext ctx{builder, skipper, recorder};
    ctx.skip();

    EXPECT_TRUE(probe_started_without_edits(ctx, delimited));
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness(":");
    auto &builder = builderHarness.builder;
    detail::FailureHistoryRecorder recorder(builder.input_begin());
    RecoveryContext ctx{builder, skipper, recorder};
    ctx.skip();

    EXPECT_FALSE(probe_started_without_edits(ctx, delimited));
  }
}

TEST(RepetitionTest, NoInsertProbeDoesNotUseWordBoundarySplit) {
  auto skipper = SkipperBuilder().build();
  auto keyword = "module"_kw;

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("moduleName");
    auto &builder = builderHarness.builder;
    detail::FailureHistoryRecorder recorder(builder.input_begin());
    RecoveryContext ctx{builder, skipper, recorder};
    ctx.skip();

    EXPECT_FALSE(attempt_parse_no_edits(ctx, keyword));
  }

  {
    auto builderHarness = pegium::test::makeCstBuilderHarness("moduleName");
    auto &builder = builderHarness.builder;
    detail::FailureHistoryRecorder recorder(builder.input_begin());
    RecoveryContext ctx{builder, skipper, recorder};
    ctx.skip();

    ASSERT_TRUE(parse(keyword, ctx));
    const auto edits = ctx.snapshotRecoveryEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits.front().kind, ParseDiagnosticKind::Inserted);
  }
}

TEST(RepetitionTest, LeadingGarbageCanStillBeLocallyRecoveredByRepeatedRule) {
  auto skipper = SkipperBuilder().ignore(some(s)).build();
  const std::string stateText =
      "state Idle\n<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle\nend";
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RepetitionProbeTransition> transitionRule{
      "Transition",
      assign<&RepetitionProbeTransition::event>(id) + "=>"_kw +
          assign<&RepetitionProbeTransition::target>(id)};
  auto assignedTransitions =
      many(append<&RepetitionProbeState::transitions>(transitionRule));

  auto builderHarness = pegium::test::makeCstBuilderHarness(
      "<<<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle");
  auto &builder = builderHarness.builder;
  detail::FailureHistoryRecorder recorder(builder.input_begin());
  RecoveryContext ctx{builder, skipper, recorder};
  ctx.skip();

  EXPECT_TRUE(probe_locally_recoverable(transitionRule, ctx));
  EXPECT_TRUE(parse(transitionRule, ctx));

  auto repeatedTransitions = many(transitionRule);
  auto repeatedHarness = pegium::test::makeCstBuilderHarness(
      "<<<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle");
  auto &repeatedBuilder = repeatedHarness.builder;
  detail::FailureHistoryRecorder repeatedRecorder(repeatedBuilder.input_begin());
  RecoveryContext repeatedCtx{repeatedBuilder, skipper, repeatedRecorder};
  repeatedCtx.editPolicy.trackEditState = true;
  repeatedCtx.skip();

  EXPECT_TRUE(parse(repeatedTransitions, repeatedCtx));
  EXPECT_GT(repeatedCtx.cursorOffset(), 0u);

  auto windowedHarness = pegium::test::makeCstBuilderHarness(
      "<<<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle");
  auto &windowedBuilder = windowedHarness.builder;
  detail::FailureHistoryRecorder windowedRecorder(windowedBuilder.input_begin());
  RecoveryContext windowedCtx{windowedBuilder, skipper, windowedRecorder};
  windowedCtx.editPolicy.trackEditState = true;
  windowedCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 0,
      .forwardTokenCount = 1,
  });
  windowedCtx.skip();

  EXPECT_TRUE(parse(repeatedTransitions, windowedCtx));
  EXPECT_GT(windowedCtx.cursorOffset(), 0u);

  auto assignedHarness = pegium::test::makeCstBuilderHarness(
      "<<<<<<<<<<<<<<<<<<<<<<<<\nStart => Idle\nend");
  auto &assignedBuilder = assignedHarness.builder;
  detail::FailureHistoryRecorder assignedRecorder(assignedBuilder.input_begin());
  RecoveryContext assignedCtx{assignedBuilder, skipper, assignedRecorder};
  assignedCtx.editPolicy.trackEditState = true;
  assignedCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 0,
      .forwardTokenCount = 1,
  });
  assignedCtx.skip();

  EXPECT_TRUE(probe_locally_recoverable(assignedTransitions, assignedCtx));
  EXPECT_TRUE(parse(assignedTransitions, assignedCtx));
  EXPECT_GT(assignedCtx.cursorOffset(), 0u);

  ParserRule<RepetitionProbeState> stateRule{
      "State",
      "state"_kw + assign<&RepetitionProbeState::name>(id) +
          many(append<&RepetitionProbeState::transitions>(transitionRule)) +
          "end"_kw};
  auto stateHarness = pegium::test::makeCstBuilderHarness(stateText);
  auto &stateBuilder = stateHarness.builder;
  detail::FailureHistoryRecorder stateRecorder(stateBuilder.input_begin());
  RecoveryContext stateCtx{stateBuilder, skipper, stateRecorder};
  stateCtx.editPolicy.trackEditState = true;
  stateCtx.skip();

  EXPECT_TRUE(parse(stateRule, stateCtx));

  auto stateWindowedHarness = pegium::test::makeCstBuilderHarness(stateText);
  auto &stateWindowedBuilder = stateWindowedHarness.builder;
  detail::FailureHistoryRecorder stateWindowedRecorder(
      stateWindowedBuilder.input_begin());
  RecoveryContext stateWindowedCtx{stateWindowedBuilder, skipper,
                                   stateWindowedRecorder};
  stateWindowedCtx.editPolicy.trackEditState = true;
  stateWindowedCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 11,
      .editFloorOffset = 11,
      .maxCursorOffset = 11,
      .forwardTokenCount = 1,
  });
  stateWindowedCtx.skip();

  EXPECT_TRUE(parse(stateRule, stateWindowedCtx));
  EXPECT_EQ(stateWindowedCtx.cursorOffset(),
            static_cast<pegium::TextOffset>(stateText.size()));

  auto stateEarlyWindowHarness = pegium::test::makeCstBuilderHarness(stateText);
  auto &stateEarlyWindowBuilder = stateEarlyWindowHarness.builder;
  detail::FailureHistoryRecorder stateEarlyWindowRecorder(
      stateEarlyWindowBuilder.input_begin());
  RecoveryContext stateEarlyWindowCtx{stateEarlyWindowBuilder, skipper,
                                      stateEarlyWindowRecorder};
  stateEarlyWindowCtx.editPolicy.trackEditState = true;
  stateEarlyWindowCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 11,
      .forwardTokenCount = 1,
  });
  stateEarlyWindowCtx.skip();

  EXPECT_TRUE(parse(stateRule, stateEarlyWindowCtx));
  EXPECT_EQ(stateEarlyWindowCtx.cursorOffset(),
            static_cast<pegium::TextOffset>(stateText.size()));

  auto statePartialHarness = pegium::test::makeCstBuilderHarness(stateText);
  auto &statePartialBuilder = statePartialHarness.builder;
  detail::FailureHistoryRecorder statePartialRecorder(
      statePartialBuilder.input_begin());
  RecoveryContext statePartialCtx{statePartialBuilder, skipper,
                                  statePartialRecorder};
  statePartialCtx.editPolicy.trackEditState = true;
  statePartialCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 11,
      .forwardTokenCount = 1,
  });
  statePartialCtx.skip();

  EXPECT_TRUE(parse(stateRule, statePartialCtx));
  EXPECT_EQ(statePartialCtx.cursorOffset(),
            static_cast<pegium::TextOffset>(stateText.size()));

  auto stateLatePartialHarness = pegium::test::makeCstBuilderHarness(stateText);
  auto &stateLatePartialBuilder = stateLatePartialHarness.builder;
  detail::FailureHistoryRecorder stateLatePartialRecorder(
      stateLatePartialBuilder.input_begin());
  RecoveryContext stateLatePartialCtx{stateLatePartialBuilder, skipper,
                                      stateLatePartialRecorder};
  stateLatePartialCtx.editPolicy.trackEditState = true;
  stateLatePartialCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 11,
      .editFloorOffset = 11,
      .maxCursorOffset = 11,
      .forwardTokenCount = 1,
  });
  stateLatePartialCtx.skip();

  EXPECT_TRUE(parse(stateRule, stateLatePartialCtx));
  EXPECT_EQ(stateLatePartialCtx.cursorOffset(),
            static_cast<pegium::TextOffset>(stateText.size()));

  auto stateDefaultBudgetHarness =
      pegium::test::makeCstBuilderHarness(stateText);
  auto &stateDefaultBudgetBuilder = stateDefaultBudgetHarness.builder;
  detail::FailureHistoryRecorder stateDefaultBudgetRecorder(
      stateDefaultBudgetBuilder.input_begin());
  RecoveryContext stateDefaultBudgetCtx{stateDefaultBudgetBuilder, skipper,
                                        stateDefaultBudgetRecorder};
  stateDefaultBudgetCtx.editPolicy.trackEditState = true;
  stateDefaultBudgetCtx.editPolicy.maxConsecutiveCodepointDeletes = 8;
  stateDefaultBudgetCtx.editPolicy.maxEditsPerAttempt = 10;
  stateDefaultBudgetCtx.editPolicy.maxEditCost = 64;
  stateDefaultBudgetCtx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 11,
      .forwardTokenCount = 8,
  });
  stateDefaultBudgetCtx.skip();

  EXPECT_TRUE(parse(stateRule, stateDefaultBudgetCtx));
  EXPECT_EQ(stateDefaultBudgetCtx.cursorOffset(),
            static_cast<pegium::TextOffset>(stateText.size()));
}

TEST(RepetitionTest,
     WeakStartedIterationWithVisibleEntryRepairBeatsRecoverableParentFollow) {
  auto skipper = SkipperBuilder().ignore(some(s)).build();
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RepetitionProbeTransition> transitionRule{
      "Transition",
      option(enable_if<&RepetitionProbeTransition::many>("many"_kw.i())) +
          assign<&RepetitionProbeTransition::event>(id) + ":"_kw +
          assign<&RepetitionProbeTransition::target>(id)};
  ParserRule<RepetitionProbeState> stateRule{
      "State",
      "state"_kw + assign<&RepetitionProbeState::name>(id) +
          many(append<&RepetitionProbeState::transitions>(transitionRule)) +
          "next"_kw +
          many(append<&RepetitionProbeState::nextTransitions>(transitionRule)) +
          "end"_kw};

  const auto result = pegium::test::parse_rule_result(
      stateRule, "state S\nman tags: String\nnext\nend", skipper);
  ASSERT_TRUE(result.value);
  EXPECT_TRUE(result.fullMatch);

  auto *state = dynamic_cast<RepetitionProbeState *>(result.value);
  ASSERT_NE(state, nullptr);
  ASSERT_EQ(state->transitions.size(), 1u);
  ASSERT_NE(state->transitions[0], nullptr);
  EXPECT_TRUE(state->transitions[0]->many);
  EXPECT_EQ(state->transitions[0]->event, "tags");
  EXPECT_TRUE(state->nextTransitions.empty());
}
