#pragma once

/// Named capability flags for the four parse-mode context types, replacing
/// the `if constexpr (requires { ctx.SOMETHING; })` duck-typing sniffs that
/// `TerminalRecoverySupport.hpp` and `RecoveryUtils.hpp` used to spell out
/// inline wherever a templated `Context` needed to know whether it carries a
/// given member. Every flag mirrors a member that genuinely differs between
/// `RecoveryContext` and `ExpectContext` (the two `EditableParseModeContext`
/// types); `ParseContext` and `TrackedParseContext` get specializations too
/// since some of the sniffed helper templates are structurally unconstrained
/// and could in principle be instantiated with either.
///
/// The `Probe*` concepts and the static_asserts below pin each flag's value
/// to what the ORIGINAL `requires`-expression it replaces evaluates to, for
/// that exact type: a future rename of any sniffed member fails one of these
/// asserts instead of silently turning the corresponding `requires` false and
/// rerouting the recovery decision that used to read it. The checks are
/// routed through a template concept (rather than a bare `requires(ConcreteType
/// &c) {...}`) because a requires-expression only gets substitution-failure
/// (non-error) treatment for a requirement that depends on a template
/// parameter being substituted; written directly against a concrete,
/// non-dependent type, an absent member is an ordinary hard error.

#include <concepts>

#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>

namespace pegium::parser::detail {

template <typename Context> struct ContextCaps {
  /// `TrackedParseContext::skip_without_builder(const char *)`: the 1-entry
  /// no-builder skip cache used by recovery probes to peek past trivia
  /// without touching the CST builder. Present on `TrackedParseContext` and
  /// `RecoveryContext` (which inherits it); absent on `ParseContext` (no
  /// probe callers reach it) and `ExpectContext` (frontier exploration skips
  /// through its own `_skipper` directly).
  static constexpr bool hasSkipWithoutBuilder = false;
  /// `RecoveryContext::hasHadEdits()`.
  static constexpr bool tracksHadEdits = false;
  /// `RecoveryContext::extendLastDeleteThroughHiddenTrivia()`.
  static constexpr bool hasExtendLastDeleteThroughHiddenTrivia = false;
  /// `RecoveryContext::allowBudgetOverflowEdits()`.
  static constexpr bool hasAllowBudgetOverflowEdits = false;
  /// `RecoveryContext::skipAfterDelete` (field).
  static constexpr bool hasSkipAfterDeleteField = false;
  /// `RecoveryContext::currentWindowEditCount()`.
  static constexpr bool hasWindowEditCount = false;
  /// `RecoveryContext::literalFuzzyCandidatesCache()`.
  static constexpr bool hasLiteralFuzzyCandidatesCache = false;
  /// `RecoveryContext::allowsScopedLeadingTerminalInsertRecovery()` (paired,
  /// at its one call site, with the always-present `editPolicy.allowDelete`
  /// read — the compound requires-expression it replaces is gated entirely
  /// by this member).
  static constexpr bool hasScopedLeadingTerminalInsertRecovery = false;
  /// `RecoveryContext::isWordCharAt()` / `RecoveryContext::wordChars`: the
  /// grammar-derived word/identifier-character mask (default: the
  /// historical `a-zA-Z0-9_` class). Absent on `ParseContext` and
  /// `TrackedParseContext` — no probe callers reach it there, so the
  /// call sites gated on this flag fall back to the hardcoded
  /// `utils::is_identifier_like_codepoint_at` classifier.
  static constexpr bool hasWordCharMask = false;
};

// `ParseContext` — the plain CST-building context — carries none of the probed
// members, so it uses the all-false primary template above (its parity is still
// asserted below). Only the contexts that own at least one probed member get an
// explicit specialization.

template <> struct ContextCaps<TrackedParseContext> {
  static constexpr bool hasSkipWithoutBuilder = true;
  static constexpr bool tracksHadEdits = false;
  static constexpr bool hasExtendLastDeleteThroughHiddenTrivia = false;
  static constexpr bool hasAllowBudgetOverflowEdits = false;
  static constexpr bool hasSkipAfterDeleteField = false;
  static constexpr bool hasWindowEditCount = false;
  static constexpr bool hasLiteralFuzzyCandidatesCache = false;
  static constexpr bool hasScopedLeadingTerminalInsertRecovery = false;
  static constexpr bool hasWordCharMask = false;
};

template <> struct ContextCaps<RecoveryContext> {
  static constexpr bool hasSkipWithoutBuilder = true;
  static constexpr bool tracksHadEdits = true;
  static constexpr bool hasExtendLastDeleteThroughHiddenTrivia = true;
  static constexpr bool hasAllowBudgetOverflowEdits = true;
  static constexpr bool hasSkipAfterDeleteField = true;
  static constexpr bool hasWindowEditCount = true;
  static constexpr bool hasLiteralFuzzyCandidatesCache = true;
  static constexpr bool hasScopedLeadingTerminalInsertRecovery = true;
  static constexpr bool hasWordCharMask = true;
};

template <> struct ContextCaps<ExpectContext> {
  static constexpr bool hasSkipWithoutBuilder = false;
  static constexpr bool tracksHadEdits = false;
  static constexpr bool hasExtendLastDeleteThroughHiddenTrivia = false;
  static constexpr bool hasAllowBudgetOverflowEdits = false;
  static constexpr bool hasSkipAfterDeleteField = false;
  static constexpr bool hasWindowEditCount = false;
  static constexpr bool hasLiteralFuzzyCandidatesCache = false;
  static constexpr bool hasScopedLeadingTerminalInsertRecovery = false;
  static constexpr bool hasWordCharMask = true;
};

namespace context_caps_parity {

// One concept per flag, each spelling the exact `requires` expression the
// flag replaces at its call site(s). Kept template (rather than inlined
// directly into the static_asserts below) so substitution failure on a
// missing member is a quiet "not satisfied" instead of a hard error — see
// the file comment.

template <typename Context>
concept ProbeSkipWithoutBuilder = requires(const Context &c) {
  c.skip_without_builder(c.cursor());
};

template <typename Context>
concept ProbeHasHadEdits = requires(const Context &c) { c.hasHadEdits(); };

template <typename Context>
concept ProbeExtendLastDeleteThroughHiddenTrivia =
    requires(Context &c) { c.extendLastDeleteThroughHiddenTrivia(); };

template <typename Context>
concept ProbeAllowBudgetOverflowEdits =
    requires(Context &c) { c.allowBudgetOverflowEdits(); };

template <typename Context>
concept ProbeSkipAfterDeleteField = requires(Context &c) { c.skipAfterDelete; };

template <typename Context>
concept ProbeWindowEditCount =
    requires(const Context &c) { c.currentWindowEditCount(); };

template <typename Context>
concept ProbeLiteralFuzzyCandidatesCache = requires(Context &c) {
  { c.literalFuzzyCandidatesCache() } ->
      std::same_as<LiteralFuzzyCandidatesCache &>;
};

template <typename Context>
concept ProbeScopedLeadingTerminalInsertRecovery = requires(Context &c) {
  c.allowsScopedLeadingTerminalInsertRecovery();
  c.editPolicy.allowDelete;
};

template <typename Context>
concept ProbeWordCharMask = requires(const Context &c, const char *p) {
  c.wordChars;
  { c.isWordCharAt(p, c.end) } -> std::same_as<bool>;
};

template <typename Context>
concept ContextCapsParityHolds =
    ContextCaps<Context>::hasSkipWithoutBuilder ==
        ProbeSkipWithoutBuilder<Context> &&
    ContextCaps<Context>::tracksHadEdits == ProbeHasHadEdits<Context> &&
    ContextCaps<Context>::hasExtendLastDeleteThroughHiddenTrivia ==
        ProbeExtendLastDeleteThroughHiddenTrivia<Context> &&
    ContextCaps<Context>::hasAllowBudgetOverflowEdits ==
        ProbeAllowBudgetOverflowEdits<Context> &&
    ContextCaps<Context>::hasSkipAfterDeleteField ==
        ProbeSkipAfterDeleteField<Context> &&
    ContextCaps<Context>::hasWindowEditCount ==
        ProbeWindowEditCount<Context> &&
    ContextCaps<Context>::hasLiteralFuzzyCandidatesCache ==
        ProbeLiteralFuzzyCandidatesCache<Context> &&
    ContextCaps<Context>::hasScopedLeadingTerminalInsertRecovery ==
        ProbeScopedLeadingTerminalInsertRecovery<Context> &&
    ContextCaps<Context>::hasWordCharMask == ProbeWordCharMask<Context>;

static_assert(ContextCapsParityHolds<ParseContext>);
static_assert(ContextCapsParityHolds<TrackedParseContext>);
static_assert(ContextCapsParityHolds<RecoveryContext>);
static_assert(ContextCapsParityHolds<ExpectContext>);

} // namespace context_caps_parity

} // namespace pegium::parser::detail
