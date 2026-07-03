#include <gtest/gtest.h>

#include <pegium/core/TestCstBuilderHarness.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>

using namespace pegium::parser;

namespace {

struct FastProbeOnlyExpression : pegium::grammar::AbstractElement {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = false;

  mutable std::size_t strictParseCalls = 0;
  mutable std::size_t fastProbeCalls = 0;

  constexpr FastProbeOnlyExpression() noexcept
      : AbstractElement(ElementKind::ParserRule) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }

  void print(std::ostream &os) const override {
    os << "FastProbeOnlyExpression";
  }

  bool parse_impl(ParseContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool parse_impl(TrackedParseContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool parse_impl(RecoveryContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool parse_impl(ExpectContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool fast_probe_impl(TrackedParseContext &) const {
    ++fastProbeCalls;
    return true;
  }

  bool fast_probe_impl(RecoveryContext &) const {
    ++fastProbeCalls;
    return true;
  }
};

struct StartedWithoutEditsExpression : pegium::grammar::AbstractElement {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = false;

  mutable std::size_t strictParseCalls = 0;
  mutable std::size_t fastProbeCalls = 0;

  constexpr StartedWithoutEditsExpression() noexcept
      : AbstractElement(ElementKind::ParserRule) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }

  void print(std::ostream &os) const override {
    os << "StartedWithoutEditsExpression";
  }

  bool parse_impl(TrackedParseContext &ctx) const {
    ++strictParseCalls;
    if (ctx.cursor() < ctx.end) {
      ctx.leaf(ctx.cursor() + 1, this);
    }
    return false;
  }

  bool parse_impl(RecoveryContext &ctx) const {
    return parse_impl(static_cast<TrackedParseContext &>(ctx));
  }

  bool parse_impl(ParseContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool parse_impl(ExpectContext &) const {
    ++strictParseCalls;
    return false;
  }

  bool fast_probe_impl(TrackedParseContext &) const {
    ++fastProbeCalls;
    return false;
  }

  bool fast_probe_impl(RecoveryContext &) const {
    ++fastProbeCalls;
    return false;
  }
};

struct EntryRecoverableExpression : pegium::grammar::AbstractElement {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = false;

  mutable std::size_t probeCalls = 0;

  constexpr EntryRecoverableExpression() noexcept
      : AbstractElement(ElementKind::ParserRule) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }

  void print(std::ostream &os) const override {
    os << "EntryRecoverableExpression";
  }

  bool parse_impl(ParseContext &) const { return false; }
  bool parse_impl(TrackedParseContext &) const { return false; }
  bool parse_impl(RecoveryContext &) const { return false; }
  bool parse_impl(ExpectContext &) const { return false; }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    ++probeCalls;
    return ctx.editPolicy.allowInsert;
  }
};

struct ActiveRecoverySensitiveExpression : pegium::grammar::AbstractElement {
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = false;

  mutable std::size_t probeCalls = 0;

  constexpr ActiveRecoverySensitiveExpression() noexcept
      : AbstractElement(ElementKind::ParserRule) {}

  [[nodiscard]] constexpr bool isNullable() const noexcept override {
    return false;
  }

  void print(std::ostream &os) const override {
    os << "ActiveRecoverySensitiveExpression";
  }

  bool parse_impl(ParseContext &) const { return false; }
  bool parse_impl(TrackedParseContext &) const { return false; }
  bool parse_impl(RecoveryContext &) const { return false; }
  bool parse_impl(ExpectContext &) const { return false; }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    ++probeCalls;
    return ctx.isActiveRecovery(this);
  }
};

} // namespace

TEST(ParseAttemptTest, ProbeStartedWithoutEditsUsesFastProbeBeforeStrictParse) {
  auto builderHarness = pegium::test::makeCstBuilderHarness("x");
  auto &builder = builderHarness.builder;
  detail::FailureHistoryRecorder recorder(builder.input_begin());
  auto skipper = SkipperBuilder().build();
  RecoveryContext ctx{builder, skipper, recorder};
  ctx.skip();

  FastProbeOnlyExpression expression;

  EXPECT_TRUE(probe_started_without_edits(ctx, expression));
  EXPECT_EQ(expression.fastProbeCalls, 1u);
  EXPECT_EQ(expression.strictParseCalls, 0u);
}


