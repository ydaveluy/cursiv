#include <gtest/gtest.h>
#include <memory>
#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/core/TestRuleParser.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/parser/RecoveryAnalysis.hpp>
#include <pegium/core/parser/EditableRecoverySupport.hpp>
#include <pegium/core/text/TextSnapshot.hpp>

using namespace pegium::parser;

namespace {

struct StringNode : pegium::AstNode {
  string value;
};

struct ContextDispatchNode : pegium::AstNode {};

struct ContextDispatchExpr final : pegium::grammar::AbstractElement {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = true;

  bool *parseContextUsed = nullptr;
  bool *trackedParseContextUsed = nullptr;

  ContextDispatchExpr(bool &parseContextFlag,
                      bool &trackedParseContextFlag) noexcept
      : AbstractElement(ElementKind::Literal),
        parseContextUsed(&parseContextFlag),
        trackedParseContextUsed(&trackedParseContextFlag) {}

  constexpr bool isNullable() const noexcept override { return nullable; }
  void print(std::ostream &os) const override { os << "<context-dispatch>"; }
  constexpr const char *terminal(const char *begin) const noexcept {
    return begin;
  }

private:
  friend struct pegium::parser::detail::ParseAccess;

  bool parse_impl(ParseContext &ctx) const {
    *parseContextUsed = true;
    ctx.leaf(ctx.cursor() + 1, this);
    return true;
  }

  bool parse_impl(TrackedParseContext &ctx) const {
    *trackedParseContextUsed = true;
    ctx.leaf(ctx.cursor() + 1, this);
    return true;
  }

  template <typename Context>
    requires(!std::same_as<std::remove_cvref_t<Context>, ParseContext> &&
             !std::same_as<std::remove_cvref_t<Context>, TrackedParseContext>)
  bool parse_impl(Context &) const {
    return true;
  }
};

bool containsHiddenNode(const pegium::CstNodeView &node) {
  if (node.isHidden()) {
    return true;
  }
  for (const auto &child : node) {
    if (containsHiddenNode(child)) {
      return true;
    }
  }
  return false;
}

bool containsHiddenNode(const pegium::RootCstNode &root) {
  for (const auto &child : root) {
    if (containsHiddenNode(child)) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST(ContextTest, DefaultContextSkipsNothing) {
  auto skipper = skip();
  auto builderHarness = pegium::test::makeCstBuilderHarness("abc");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();

  auto result = skipper.skip(input.begin(), builder);
  EXPECT_EQ(result, input.begin());

  auto root = builder.getRootCstNode();
  EXPECT_EQ(root->begin(), root->end());
}

TEST(ContextTest, IgnoreSkipsWhitespaceBeforeParsing) {
  TerminalRule<> ws{"WS", some(s)};
  auto skipper = skip(ignored(ws));
  auto builderHarness = pegium::test::makeCstBuilderHarness("   abc");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();

  auto result = skipper.skip(input.begin(), builder);
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result - input.begin(), 3);

  auto root = builder.getRootCstNode();
  EXPECT_EQ(root->begin(), root->end());
}

TEST(ContextTest, IgnoreAndHideAreApplied) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<> comment{"COMMENT", "//"_kw <=> &(eol | eof)};
  DataTypeRule<std::string> rule{"Rule", "a"_kw + "b"_kw};
  ParserRule<StringNode> root{"Root", assign<&StringNode::value>(rule)};

  const auto result = pegium::test::parse_rule_result(
      root, "a// comment\n   b", skip(ignored(ws), hidden(comment)));

  ASSERT_TRUE(result.value);
  auto *typed = pegium::ast_ptr_cast<StringNode>(result.value);
  ASSERT_TRUE(typed != nullptr);
  EXPECT_EQ(typed->value, "ab");
  ASSERT_TRUE(result.cst);
  EXPECT_TRUE(containsHiddenNode(*result.cst));
}

TEST(ContextTest, SkipOwnsTemporaryRules) {
  auto skipper = skip(ignored(TerminalRule<>{"WS", some(s)}),
                      hidden(TerminalRule<>{"Comma", ","_kw}));

  auto builderHarness = pegium::test::makeCstBuilderHarness(" ,x");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  auto result = skipper.skip(input.begin(), builder);

  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result - input.begin(), 2);

  auto root = builder.getRootCstNode();
  auto it = root->begin();
  ASSERT_NE(it, root->end());
  EXPECT_TRUE((*it).isHidden());
  EXPECT_EQ((*it).getText(), ",");
}

TEST(ContextTest, ContextCanBeConvertedToParseContextWithoutOwning) {
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<> comma{"Comma", ","_kw};
  auto skipperContext = SkipperContext{std::tie(comma), std::tie(ws)};
  Skipper skipper = skipperContext;

  auto builderHarness = pegium::test::makeCstBuilderHarness(" ,x");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  auto result = skipper.skip(input.begin(), builder);

  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result - input.begin(), 2);

  auto root = builder.getRootCstNode();
  auto it = root->begin();
  ASSERT_NE(it, root->end());
  EXPECT_TRUE((*it).isHidden());
  EXPECT_EQ((*it).getText(), ",");
}

TEST(ContextTest, ParseContextMarkAndRewindRestoreCursorAndBuilderState) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("ab");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "a"_kw;

  ParseContext ctx{builder, skipper};
  const auto checkpoint = ctx.mark();
  ctx.leaf(input.begin() + 1, std::addressof(literal));

  ASSERT_EQ(ctx.cursorOffset(), 1u);
  ASSERT_EQ(ctx.lastVisibleCursorOffset(), 1u);

  ctx.rewind(checkpoint);

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.lastVisibleCursorOffset(), 0u);
  auto *root = builder.getRootCstNode();
  EXPECT_EQ(root->begin(), root->end());
}

TEST(ContextTest,
     TrackedParseContextRewindRestoresFailureHistoryRecorderState) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("ab");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "a"_kw;

  detail::FailureHistoryRecorder recorder(input.begin());
  TrackedParseContext ctx{builder, skipper, recorder,
                          pegium::utils::default_cancel_token};

  ASSERT_TRUE(ctx.isFailureHistoryRecordingEnabled());

  const auto checkpoint = ctx.mark();
  ctx.leaf(input.begin() + 1, std::addressof(literal));
  ctx.rewind(checkpoint);
  ctx.leaf(input.begin() + 1, std::addressof(literal));

  const auto snapshot = recorder.snapshot(ctx.maxCursorOffset());
  ASSERT_EQ(snapshot.failureLeafHistory.size(), 1u);
  EXPECT_EQ(snapshot.failureLeafHistory.front().beginOffset, 0u);
  EXPECT_EQ(snapshot.failureLeafHistory.front().endOffset, 1u);
}

TEST(ContextTest, SkipWithoutBuilderCacheIsKeyedOnTheActiveSkipper) {
  // Two skippers that disagree at the same offset: A ignores only whitespace and
  // stops at the comment; B also ignores the line comment and skips past it.
  TerminalRule<> ws{"WS", some(s)};
  TerminalRule<> comment{"COMMENT", "//"_kw <=> &(eol | eof)};
  const auto skipperA = skip(ignored(ws));
  const auto skipperB = skip(ignored(ws), ignored(comment));

  auto builderHarness = pegium::test::makeCstBuilderHarness("  // c\nx");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const char *const begin = input.begin();

  // Sanity: the skippers genuinely produce different ends at `begin`.
  ASSERT_NE(skipperA.skip(begin), skipperB.skip(begin));
  const char *const expectedA = skipperA.skip(begin);

  detail::FailureHistoryRecorder recorder(begin);
  TrackedParseContext ctx{builder, skipperA, recorder,
                          pegium::utils::default_cancel_token};

  // Populate the 1-entry no-builder skip cache under skipper A.
  EXPECT_EQ(ctx.skip_without_builder(begin), expectedA);

  {
    // A local-skipper scope recomputes the cache under skipper B.
    auto guard = ctx.with_skipper(skipperB);
    EXPECT_EQ(ctx.skip_without_builder(begin), skipperB.skip(begin));
  }

  // The scope has exited, so the active skipper is A again. The cache must not
  // hand back the value it computed under B for the same offset.
  EXPECT_EQ(ctx.skip_without_builder(begin), expectedA);
}

TEST(ContextTest,
     RecoveryWindowForwardBudgetExtendsFromLaterEditsInCurrentAttempt) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("abc");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto a = "a"_kw;
  const auto b = "b"_kw;
  const auto c = "c"_kw;

  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder,
                      pegium::utils::default_cancel_token};
  ctx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = 0,
      .forwardTokenCount = 2,
  });

  ctx.skip();
  ASSERT_TRUE(ctx.isInRecoveryPhase());

  ASSERT_TRUE(ctx.insertSynthetic(std::addressof(a)));
  ctx.leaf(input.begin() + 1, std::addressof(a));
  EXPECT_TRUE(ctx.isInRecoveryPhase());

  ASSERT_TRUE(ctx.insertSynthetic(std::addressof(b)));
  const auto replayForwardCount = ctx.replayForwardTokenCount();
  ASSERT_TRUE(replayForwardCount.has_value());
  EXPECT_EQ(*replayForwardCount, 3u);

  ctx.leaf(input.begin() + 2, std::addressof(b));
  EXPECT_TRUE(ctx.isInRecoveryPhase());

  ctx.leaf(input.begin() + 3, std::addressof(c));
  EXPECT_FALSE(ctx.isInRecoveryPhase());
}

TEST(ContextTest, RecoveryDeleteCanBridgeAdjacentHiddenTriviaIntoSameEdit) {
  TerminalRule<> ws{"WS", some(s)};
  const auto skipper = SkipperBuilder().ignore(ws).build();
  auto builderHarness = pegium::test::makeCstBuilderHarness("x    y");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();

  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder,
                      pegium::utils::default_cancel_token};
  ctx.setEditWindow(RecoveryContext::EditWindow{
      .beginOffset = 0,
      .editFloorOffset = 0,
      .maxCursorOffset = static_cast<pegium::TextOffset>(input.size()),
      .forwardTokenCount = 1,
  });

  ctx.skip();
  ctx.skipAfterDelete = false;
  ASSERT_TRUE(ctx.deleteOneCodepoint());
  ASSERT_TRUE(ctx.extendLastDeleteThroughHiddenTrivia());
  EXPECT_EQ(ctx.cursorOffset(), 5u);
  ASSERT_TRUE(ctx.deleteOneCodepoint());
  EXPECT_EQ(ctx.cursorOffset(), 6u);
  EXPECT_EQ(ctx.recoveryEditCount(), 1u);

  const auto diagnostics = detail::materialize_syntax_diagnostics(
      detail::normalize_syntax_script(ctx.snapshotRecoveryEdits()));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().kind, ParseDiagnosticKind::Deleted);
  EXPECT_EQ(diagnostics.front().beginOffset, 0u);
  EXPECT_EQ(diagnostics.front().endOffset, 6u);
}

TEST(ContextTest, RecoveryContextWordCharMaskDefaultsToHistoricalIdentifierClass) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("x");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  detail::FailureHistoryRecorder recorder(input.begin());
  const RecoveryContext ctx{builder, skipper, recorder,
                            pegium::utils::default_cancel_token};

  for (unsigned byteValue = 0; byteValue < 256; ++byteValue) {
    const char byte = static_cast<char>(byteValue);
    EXPECT_EQ(ctx.isWordCharAt(&byte, &byte + 1),
              pegium::utils::is_identifier_like_codepoint_at(&byte, &byte + 1))
        << "byte=" << byteValue;
  }

  // A real multi-byte (non-ASCII) codepoint: the >=0x80 passthrough branch,
  // not just the single-byte truncation short-circuit above.
  constexpr std::string_view accentedE = "\xC3\xA9"; // U+00E9 'é'
  EXPECT_EQ(ctx.isWordCharAt(accentedE.data(), accentedE.data() + accentedE.size()),
            pegium::utils::is_identifier_like_codepoint_at(
                accentedE.data(), accentedE.data() + accentedE.size()));
  EXPECT_TRUE(ctx.isWordCharAt(accentedE.data(), accentedE.data() + accentedE.size()));
}

TEST(ContextTest, ExpectContextWordCharMaskDefaultsToHistoricalIdentifierClass) {
  const auto skipper = NoOpSkipper();
  const ExpectContext ctx{"x", skipper, 0};

  for (unsigned byteValue = 0; byteValue < 256; ++byteValue) {
    const char byte = static_cast<char>(byteValue);
    EXPECT_EQ(ctx.isWordCharAt(&byte, &byte + 1),
              pegium::utils::is_identifier_like_codepoint_at(&byte, &byte + 1))
        << "byte=" << byteValue;
  }
}

TEST(ContextTest,
     PositionStartsStructuredVisibleSourceUsesInstalledWordCharMask) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("+");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder,
                      pegium::utils::default_cancel_token};

  // Default C-family mask: '+' is not identifier-like.
  EXPECT_FALSE(
      detail::position_starts_structured_visible_source(ctx, input.begin()));

  // Installing a grammar-specific mask that treats '+' as word-like flips
  // the decision — proving the gate reads the per-context mask rather than
  // the hardcoded `utils::is_identifier_like_codepoint` classifier.
  ctx.wordChars.table[static_cast<unsigned char>('+')] = true;
  EXPECT_TRUE(
      detail::position_starts_structured_visible_source(ctx, input.begin()));
}

TEST(ContextTest,
     CursorStartsStructuredVisibleSourceUsesInstalledWordCharMask) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("+");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder,
                      pegium::utils::default_cancel_token};
  ctx.skip();

  EXPECT_FALSE(detail::cursor_starts_structured_visible_source(ctx));

  ctx.wordChars.table[static_cast<unsigned char>('+')] = true;
  EXPECT_TRUE(detail::cursor_starts_structured_visible_source(ctx));
}

TEST(ContextTest,
     MaterializeRecoveryEditsMergesCompatibleInsertedDiagnosticsAtSameOffset) {
  const std::vector<RecoveryContext::RecoveryEdit> edits{
      {.kind = ParseDiagnosticKind::Inserted,
       .offset = 5,
       .beginOffset = 5,
       .endOffset = 5,
       .element = nullptr,
       .message = "expected terminator"},
      {.kind = ParseDiagnosticKind::Inserted,
       .offset = 5,
       .beginOffset = 5,
       .endOffset = 5,
       .element = nullptr,
       .message = "expected terminator"},
  };

  const auto diagnostics = detail::materialize_syntax_diagnostics(
      detail::normalize_syntax_script(edits));

  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().kind, ParseDiagnosticKind::Inserted);
  EXPECT_EQ(diagnostics.front().offset, 5u);
  EXPECT_EQ(diagnostics.front().message, "expected terminator");
}

TEST(ContextTest, RunStrictParseUsesPlainContextWithoutFailureRecorder) {
  bool parseContextUsed = false;
  bool trackedParseContextUsed = false;
  ParserRule<ContextDispatchNode> entry{
      "Entry", ContextDispatchExpr{parseContextUsed, trackedParseContextUsed}};

  const auto text = pegium::text::TextSnapshot::copy("a");

  const auto result = detail::run_strict_parse(entry, NoOpSkipper(), text);

  EXPECT_TRUE(result.summary.entryRuleMatched);
  EXPECT_TRUE(parseContextUsed);
  EXPECT_FALSE(trackedParseContextUsed);
}

TEST(ContextTest,
     RunStrictParseUsesTrackedContextWhenFailureRecorderIsPresent) {
  bool parseContextUsed = false;
  bool trackedParseContextUsed = false;
  ParserRule<ContextDispatchNode> entry{
      "Entry", ContextDispatchExpr{parseContextUsed, trackedParseContextUsed}};

  const auto text = pegium::text::TextSnapshot::copy("a");
  detail::FailureHistoryRecorder recorder(text.view().data());

  const auto result = detail::run_strict_parse(
      entry, NoOpSkipper(), text, pegium::utils::default_cancel_token,
      &recorder);

  EXPECT_TRUE(result.summary.entryRuleMatched);
  EXPECT_FALSE(parseContextUsed);
  EXPECT_TRUE(trackedParseContextUsed);
}

TEST(
    ContextTest,
    RecoveryContextRewindRestoresCursorWithoutRestoringFurthestExploredCursor) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("ab");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "a"_kw;
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder};

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);

  const auto checkpoint = ctx.mark();
  ctx.leaf(input.begin() + 1, std::addressof(literal));
  EXPECT_EQ(ctx.cursorOffset(), 1u);
  EXPECT_EQ(ctx.maxCursorOffset(), 1u);

  ctx.rewind(checkpoint);

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 1u);
}

TEST(ContextTest,
     RecoveryHelpersProbeAndSideEffectFreeParseRestoreCursorAndFrontier) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("()");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder};
  auto delimited = "("_kw + "name"_kw + ")"_kw;
  auto literal = "("_kw;

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);

  EXPECT_TRUE(probe_started_without_edits(ctx, delimited));
  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);

  EXPECT_TRUE(detail::attempt_parse_without_side_effects(ctx, literal));
  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);
}

TEST(ProbeRestoreScopeTest, RestoresCheckpointAndFurthestOnDestruction) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("hello");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "h"_kw;
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder};

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);

  {
    detail::ProbeRestoreScope guard{ctx};
    ctx.leaf(input.begin() + 3, std::addressof(literal));
    EXPECT_EQ(ctx.cursorOffset(), 3u);
    EXPECT_EQ(ctx.maxCursorOffset(), 3u);
  }

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);
}

TEST(ProbeRestoreScopeTest, CommitSuppressesRestore) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("hello");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "h"_kw;
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder};

  {
    detail::ProbeRestoreScope guard{ctx};
    ctx.leaf(input.begin() + 3, std::addressof(literal));
    guard.commit();
  }

  EXPECT_EQ(ctx.cursorOffset(), 3u);
}

TEST(ProbeRestoreScopeTest, RestoresOnEarlyReturnPath) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("hello");
  auto &builder = builderHarness.builder;
  const auto input = builder.getText();
  const auto skipper = NoOpSkipper();
  const auto literal = "h"_kw;
  detail::FailureHistoryRecorder recorder(input.begin());
  RecoveryContext ctx{builder, skipper, recorder};

  const auto withGuardThatBailsEarly = [&]() {
    detail::ProbeRestoreScope guard{ctx};
    ctx.leaf(input.begin() + 3, std::addressof(literal));
    if (ctx.cursorOffset() > 0u) {
      return;  // early-return path; guard restores on dtor
    }
    guard.commit();
  };
  withGuardThatBailsEarly();

  EXPECT_EQ(ctx.cursorOffset(), 0u);
  EXPECT_EQ(ctx.maxCursorOffset(), 0u);
}
