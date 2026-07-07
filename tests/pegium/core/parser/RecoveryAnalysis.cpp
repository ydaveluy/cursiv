#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <pegium/core/RecoveryHarnessTestSupport.hpp>

using namespace pegium::parser;

namespace {

struct RecoveryAnalysisNode : pegium::AstNode {
  string name;
};

const auto kSimpleExpectedCst = R"json({
  "content": [
    {
      "begin": 0,
      "content": [
        {
          "begin": 0,
          "end": 5,
          "grammarSource": "Literal",
          "text": "hello"
        },
        {
          "begin": 6,
          "end": 11,
          "grammarSource": "name=ID",
          "text": "world"
        }
      ],
      "end": 11,
      "grammarSource": "Rule(Entry)"
    }
  ]
})json";

TEST(RecoveryFailureAnalysisTest, UsesContainingLeafWhenMaxCursorFallsInsideLeaf) {
  TerminalRule<> ws{"WS", some(s)};
  const auto hello = "hello"_kw;
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", hello + assign<&RecoveryAnalysisNode::name>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  const auto source =
      pegium::test::StrictRecoveryDocument(entry, "hello world", skipper);

  pegium::test::ExpectCst(*source.result.cst, kSimpleExpectedCst,
                          pegium::test::recovery_cst_json_options());

  pegium::parser::detail::FailureSnapshot snapshot;
  snapshot.maxCursorOffset = 7;
  pegium::parser::detail::FailureLeaf helloLeaf;
  helloLeaf.beginOffset = 0;
  helloLeaf.endOffset = 5;
  helloLeaf.element = std::addressof(hello);
  snapshot.failureLeafHistory.push_back(helloLeaf);
  pegium::parser::detail::FailureLeaf idLeaf;
  idLeaf.beginOffset = 6;
  idLeaf.endOffset = 11;
  idLeaf.element = std::addressof(id);
  snapshot.failureLeafHistory.push_back(idLeaf);
  pegium::parser::detail::finalize_failure_token_index(snapshot);

  EXPECT_TRUE(snapshot.hasFailureToken);
  EXPECT_EQ(snapshot.failureTokenIndex, 1u);
}

TEST(RecoveryFailureAnalysisTest,
     FallsBackToPreviousLeafWhenMaxCursorFallsBetweenVisibleLeaves) {
  TerminalRule<> ws{"WS", some(s)};
  const auto hello = "hello"_kw;
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", hello + assign<&RecoveryAnalysisNode::name>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  const auto harness =
      pegium::test::FailureAnalysisDocument(entry, "hello world !", skipper);

  pegium::test::ExpectCst(*harness.result.strictResult.cst, kSimpleExpectedCst,
                          pegium::test::recovery_cst_json_options());
  EXPECT_TRUE(harness.result.snapshot.hasFailureToken);
  EXPECT_EQ(harness.result.snapshot.failureTokenIndex, 1u);
}

TEST(RecoveryFailureAnalysisTest, HandlesInputWithoutVisibleLeaf) {
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", assign<&RecoveryAnalysisNode::name>("hello"_kw)};
  const auto harness =
      pegium::test::FailureAnalysisDocument(entry, "", NoOpSkipper());

  pegium::test::ExpectCst(
      *harness.result.strictResult.cst, R"json({
  "content": []
})json",
      pegium::test::recovery_cst_json_options());
  EXPECT_FALSE(harness.result.snapshot.hasFailureToken);
  EXPECT_TRUE(harness.result.snapshot.failureLeafHistory.empty());
}

TEST(RecoveryFailureAnalysisTest,
     CommittedCstSnapshotMatchesFailureAnalysisLeafHistory) {
  TerminalRule<> ws{"WS", some(s)};
  const auto hello = "hello"_kw;
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", hello + assign<&RecoveryAnalysisNode::name>(id)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  const auto harness =
      pegium::test::FailureAnalysisDocument(entry, "hello world !", skipper);

  ASSERT_NE(harness.result.strictResult.cst, nullptr);
  const auto committedSnapshot = pegium::parser::detail::snapshot_from_committed_cst(
      *harness.result.strictResult.cst, harness.result.snapshot.maxCursorOffset);

  EXPECT_EQ(committedSnapshot.failureLeafHistory.size(),
            harness.result.snapshot.failureLeafHistory.size());
  EXPECT_EQ(committedSnapshot.hasFailureToken,
            harness.result.snapshot.hasFailureToken);
  EXPECT_EQ(committedSnapshot.failureTokenIndex,
            harness.result.snapshot.failureTokenIndex);
  EXPECT_EQ(committedSnapshot.maxCursorOffset,
            harness.result.snapshot.maxCursorOffset);
}

TEST(RecoveryWindowTest, ComputesWindowForScenario) {
  using pegium::parser::detail::compute_recovery_window;
  using pegium::parser::detail::FailureSnapshot;

  // Expected window fields are optional: each row asserts only the fields that
  // were checked by its original dedicated test. A nullopt field is not
  // EXPECT_EQ'd, so no row's assertions are weakened or invented.
  struct ExpectedWindow {
    std::optional<std::uint32_t> beginOffset;
    std::optional<std::uint32_t> editFloorOffset;
    std::optional<std::uint32_t> maxCursorOffset;
    std::optional<std::uint32_t> tokenCount;
    std::optional<std::uint32_t> forwardTokenCount;
    std::optional<std::uint32_t> visibleLeafBeginIndex;
  };

  struct Scenario {
    const char *label;
    // Per-row builder: snapshot construction differs per scenario (harness vs.
    // manual leaf history), so each row supplies its own factory. The builder
    // may also assert scenario-specific preconditions (e.g. the clamp row's
    // strict-summary invariants) before returning the snapshot.
    std::function<FailureSnapshot()> makeSnapshot;
    // When true, call the single-argument overload compute_recovery_window(
    // snapshot, backwardTokenCount); otherwise call the 4-argument overload
    // with (backwardTokenCount, forwardTokenCount, stablePrefixFloorOffset).
    bool useSingleArgOverload;
    std::uint32_t backwardTokenCount;
    std::uint32_t forwardTokenCount;
    std::uint32_t stablePrefixFloorOffset;
    ExpectedWindow expected;
  };

  // Shared grammar pieces for the harness-backed clamp row and the
  // manual-history backward-start row. Kept alive for the whole TEST body so
  // the builder lambdas can reference them.
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  const auto skipper = SkipperBuilder().ignore(ws).build();

  // Clamp scenario grammar: entity rule with a field list.
  const auto field = id + ":"_kw + id;
  ParserRule<RecoveryAnalysisNode> clampEntry{
      "Entry",
      "entity"_kw + assign<&RecoveryAnalysisNode::name>(id) + "{"_kw +
          many(field) + "}"_kw};

  // Backward-start scenario grammar: hello + name=ID.
  const auto hello = "hello"_kw;
  ParserRule<RecoveryAnalysisNode> backwardEntry{
      "Entry", hello + assign<&RecoveryAnalysisNode::name>(id)};

  // Keep the harness alive across the whole test: its snapshot owns leaf
  // pointers into harness-owned grammar/CST state.
  const auto clampHarness = pegium::test::FailureAnalysisDocument(
      clampEntry,
      "entity Blog {\n"
      "  title: String\n"
      "  content:: String\n"
      "}\n",
      skipper);
  const auto backwardSource =
      pegium::test::StrictRecoveryDocument(backwardEntry, "hello world", skipper);

  const auto makeManualHistory =
      [](pegium::TextOffset maxCursor,
         std::vector<pegium::parser::detail::FailureLeaf> leaves,
         std::size_t failureTokenIndex) {
        FailureSnapshot snapshot;
        snapshot.maxCursorOffset = maxCursor;
        snapshot.failureLeafHistory = std::move(leaves);
        snapshot.failureTokenIndex = failureTokenIndex;
        snapshot.hasFailureToken = true;
        return snapshot;
      };

  const std::vector<Scenario> scenarios = {
      // --- clamp: ClampsInitialWindowToStrictParsedLengthInsideFirstConstruct
      // Harness-built snapshot; editFloorOffset clamps to strict parsedLength.
      {
          "clamp: window clamps initial begin/editFloor to strict parsedLength "
          "inside first construct",
          [&clampHarness]() {
            const auto &summary = clampHarness.result.strictResult.summary;
            // Scenario preconditions (preserved from the original test).
            EXPECT_FALSE(summary.entryRuleMatched);
            EXPECT_EQ(summary.parsedLength, 32u);
            EXPECT_GT(summary.maxCursorOffset, summary.parsedLength);
            EXPECT_TRUE(clampHarness.result.snapshot.hasFailureToken);
            return clampHarness.result.snapshot;
          },
          /*useSingleArgOverload=*/false,
          /*backwardTokenCount=*/8u,
          /*forwardTokenCount=*/8u,
          // stablePrefixFloorOffset == strict parsedLength (== 32).
          clampHarness.result.strictResult.summary.parsedLength,
          ExpectedWindow{
              .beginOffset = 0u,
              .editFloorOffset =
                  clampHarness.result.strictResult.summary.parsedLength,
          },
      },
      // --- backward-start: ComputesBackwardWindowStartFromFailureSnapshot
      // Manual hello/id leaf history; backward start walks to the first leaf.
      {
          "backward-start: window start walks backward to first leaf from "
          "failure snapshot",
          [&]() {
            pegium::test::ExpectCst(*backwardSource.result.cst,
                                    kSimpleExpectedCst,
                                    pegium::test::recovery_cst_json_options());
            return makeManualHistory(
                11,
                {{.beginOffset = 0, .endOffset = 5, .element = std::addressof(hello)},
                 {.beginOffset = 6, .endOffset = 11, .element = std::addressof(id)}},
                /*failureTokenIndex=*/1);
          },
          /*useSingleArgOverload=*/true,
          /*backwardTokenCount=*/8u,
          /*forwardTokenCount=*/8u,
          /*stablePrefixFloorOffset=*/0u,
          ExpectedWindow{
              .beginOffset = 0u,
              .editFloorOffset = 0u,
              .maxCursorOffset = 11u,
              .visibleLeafBeginIndex = 0u,
          },
      },
      // --- max-cursor fallback: FallsBackToMaxCursorWhenNoVisibleLeafIsAvailable
      // Empty leaf history; window collapses to maxCursorOffset.
      {
          "max-cursor fallback: window falls back to maxCursorOffset when no "
          "visible leaf is available",
          []() {
            FailureSnapshot snapshot;
            snapshot.maxCursorOffset = 17;
            return snapshot;
          },
          /*useSingleArgOverload=*/true,
          /*backwardTokenCount=*/8u,
          /*forwardTokenCount=*/8u,
          /*stablePrefixFloorOffset=*/0u,
          ExpectedWindow{
              .beginOffset = 17u,
              .editFloorOffset = 17u,
              .maxCursorOffset = 17u,
          },
      },
      // --- history widening: CanWidenBackwardHistoryWithoutGrowingForwardStability
      // Larger backward count than forward; tokenCount/forwardTokenCount kept.
      {
          "history widening: backward history widens without growing forward "
          "stability",
          [&]() {
            return makeManualHistory(
                17,
                {{.beginOffset = 0, .endOffset = 5, .element = nullptr},
                 {.beginOffset = 6, .endOffset = 11, .element = nullptr},
                 {.beginOffset = 12, .endOffset = 17, .element = nullptr}},
                /*failureTokenIndex=*/2);
          },
          /*useSingleArgOverload=*/false,
          /*backwardTokenCount=*/16u,
          /*forwardTokenCount=*/8u,
          /*stablePrefixFloorOffset=*/0u,
          ExpectedWindow{
              .beginOffset = 0u,
              .editFloorOffset = 0u,
              .maxCursorOffset = 17u,
              .tokenCount = 16u,
              .forwardTokenCount = 8u,
          },
      },
      // --- forward limit: UsesVisibleLeafHistoryBeyondMaxCursorAsForwardLimit
      // A leaf beyond maxCursorOffset extends the forward window to its end.
      {
          "forward limit: visible leaf history beyond maxCursor extends forward "
          "limit",
          [&]() {
            return makeManualHistory(
                17,
                {{.beginOffset = 0, .endOffset = 5, .element = nullptr},
                 {.beginOffset = 6, .endOffset = 11, .element = nullptr},
                 {.beginOffset = 12, .endOffset = 17, .element = nullptr},
                 {.beginOffset = 18, .endOffset = 23, .element = nullptr}},
                /*failureTokenIndex=*/2);
          },
          /*useSingleArgOverload=*/false,
          /*backwardTokenCount=*/8u,
          /*forwardTokenCount=*/8u,
          /*stablePrefixFloorOffset=*/0u,
          ExpectedWindow{
              .beginOffset = 0u,
              .editFloorOffset = 0u,
              .maxCursorOffset = 23u,
          },
      },
      // --- stable-prefix floor: StartsAtStablePrefixFloorWhenItPrecedesNatural...
      // A stablePrefixFloor between leaves pins the window start to the floor.
      {
          "stable-prefix floor: window starts at stable-prefix floor when it "
          "precedes the natural backward window",
          [&]() {
            return makeManualHistory(
                23,
                {{.beginOffset = 0, .endOffset = 5, .element = nullptr},
                 {.beginOffset = 6, .endOffset = 11, .element = nullptr},
                 {.beginOffset = 12, .endOffset = 17, .element = nullptr},
                 {.beginOffset = 18, .endOffset = 23, .element = nullptr}},
                /*failureTokenIndex=*/3);
          },
          /*useSingleArgOverload=*/false,
          /*backwardTokenCount=*/1u,
          /*forwardTokenCount=*/1u,
          /*stablePrefixFloorOffset=*/6u,
          ExpectedWindow{
              .beginOffset = 6u,
              .editFloorOffset = 12u,
              .maxCursorOffset = 23u,
              .visibleLeafBeginIndex = 1u,
          },
      },
  };

  for (const auto &scenario : scenarios) {
    SCOPED_TRACE(scenario.label);
    const FailureSnapshot snapshot = scenario.makeSnapshot();
    const auto window =
        scenario.useSingleArgOverload
            ? compute_recovery_window(snapshot, scenario.backwardTokenCount)
            : compute_recovery_window(snapshot, scenario.backwardTokenCount,
                                      scenario.forwardTokenCount,
                                      scenario.stablePrefixFloorOffset);

    const auto &exp = scenario.expected;
    if (exp.beginOffset) {
      EXPECT_EQ(window.beginOffset, *exp.beginOffset);
    }
    if (exp.editFloorOffset) {
      EXPECT_EQ(window.editFloorOffset, *exp.editFloorOffset);
    }
    if (exp.maxCursorOffset) {
      EXPECT_EQ(window.maxCursorOffset, *exp.maxCursorOffset);
    }
    if (exp.tokenCount) {
      EXPECT_EQ(window.tokenCount, *exp.tokenCount);
    }
    if (exp.forwardTokenCount) {
      EXPECT_EQ(window.forwardTokenCount, *exp.forwardTokenCount);
    }
    if (exp.visibleLeafBeginIndex) {
      EXPECT_EQ(window.visibleLeafBeginIndex, *exp.visibleLeafBeginIndex);
    }
  }
}

TEST(WordCharMaskDerivationTest, CanonicalIdentifierGrammarReproducesDefault) {
  // The canonical ID shape shared by every in-tree grammar: the continuation
  // class equals the default C-family word class byte-for-byte.
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many(w)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(id)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_EQ(mask.table, pegium::utils::is_word_lookup);
}

TEST(WordCharMaskDerivationTest, OptionalClassIsNotContinuation) {
  // An option contributes its class at most once: an optional sign before
  // the digits does not make '+'/'-' word characters.
  TerminalRule<std::string> number{"NUMBER", option("+-"_cr) + some("0-9"_cr)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(number)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_TRUE(mask.table['0']);
  EXPECT_FALSE(mask.table['+']);
  EXPECT_FALSE(mask.table['-']);
}

TEST(WordCharMaskDerivationTest, TerminalFragmentKeepsRepetitionContext) {
  // A terminal referenced from inside another terminal's repetition is a
  // lexical fragment of the same token: its alphabet continues that token.
  TerminalRule<std::string> digit{"DIGIT", "0-9"_cr};
  TerminalRule<std::string> id{"ID",
                               "a-zA-Z_"_cr + many("a-zA-Z_"_cr | digit)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(id)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_TRUE(mask.table['a']);
  EXPECT_TRUE(mask.table['5']);
}

TEST(WordCharMaskDerivationTest, ExtendedContinuationAlphabetIsDerived) {
  // A kebab-case identifier alphabet: '-' continues a token in THIS grammar.
  TerminalRule<std::string> id{"ID", "a-zA-Z_"_cr + many("a-zA-Z0-9_-"_cr)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(id)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_TRUE(mask.table['-']);
  EXPECT_TRUE(mask.table['a']);
  EXPECT_TRUE(mask.table['0']);
  EXPECT_TRUE(mask.table['_']);
  EXPECT_FALSE(mask.table['+']);
}

TEST(WordCharMaskDerivationTest, StartClassIsNotCollected) {
  // Only the alphabet under a Repetition contributes: the start class asks
  // "does a token begin here", while the boundary checks the mask feeds ask
  // "does the next codepoint CONTINUE a token".
  TerminalRule<std::string> id{"ID", "A-Z"_cr + many("0-9"_cr)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(id)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_TRUE(mask.table['0']);
  EXPECT_TRUE(mask.table['9']);
  EXPECT_FALSE(mask.table['A']);
  EXPECT_FALSE(mask.table['a']);
}

TEST(WordCharMaskDerivationTest, PredicateGuardedClassIsPruned) {
  // A class reachable only through a NotPredicate guard defines a boundary,
  // not the token's continuation alphabet — the walk must not collect it.
  TerminalRule<std::string> id{"ID", "x-z"_cr + many(!"a-b"_cr + "x-z"_cr)};
  ParserRule<RecoveryAnalysisNode> entry{
      "Entry", "module"_kw + assign<&RecoveryAnalysisNode::name>(id)};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_TRUE(mask.table['x']);
  EXPECT_TRUE(mask.table['z']);
  EXPECT_FALSE(mask.table['a']);
  EXPECT_FALSE(mask.table['b']);
}

TEST(WordCharMaskDerivationTest, GrammarWithoutRepeatedClassFallsBack) {
  // No repeated in-terminal class anywhere: the derivation falls back to the
  // default mask instead of an empty one.
  ParserRule<RecoveryAnalysisNode> entry{"Entry", "service"_kw};
  const auto mask = detail::derive_word_char_mask(entry);
  EXPECT_EQ(mask.table, pegium::utils::is_word_lookup);
}

} // namespace
