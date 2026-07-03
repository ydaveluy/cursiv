#pragma once

/// Non-template local-recovery driver shared by the terminal combinators
/// `Literal` and `TerminalRule`.
///
/// Both combinators repair a failed strict terminal match through the same
/// verdict ladder: seed the candidate set (boundary split-Insert for
/// keywords, fuzzy-Replace leaves), complete it with the insert /
/// extended-delete-scan fallback (`complete_terminal_recovery_choice`), then
/// realize the winning candidate (`apply_terminal_recovery_choice`). The
/// ladder, the post-strict-miss gates and the recovery probes touch the
/// combinator only through a `TerminalRecoveryProfile` — the element
/// identity, a type-erased strict matcher, the terminal shape and the
/// per-kind policy switches — so they are hosted here once, compiled in
/// TerminalLocalRecovery.cpp, instead of being stamped per combinator
/// instantiation.
///
/// What stays in the combinator headers: the strict paths (including
/// `Literal`'s word-boundary rule on the nominal path), the direct-match
/// fast paths, the expect-frontier arms (`addKeyword` / `addRule`, the
/// anchor-bounded and prefix matches), and the `probeRecoverable*` CPO
/// methods themselves — each forwards to its kind's driver below, because
/// the two kinds' probe ladders have genuinely different arm orders and
/// prior-edits gate scopes.

#include <string_view>
#include <type_traits>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>

namespace pegium::parser::detail {

/// Type-erased view of one terminal combinator consumed by the shared local
/// recovery driver. `self` is the combinator instance; `match` downcasts it
/// and runs its strict terminal matcher. The remaining fields are the
/// grammar-shape facts and the per-kind policy switches the driver bodies
/// read; each combinator materializes the profile on the stack per call.
struct TerminalRecoveryProfile {
  const void *self = nullptr;
  /// Element identity committed into leaves and synthetic inserts.
  const grammar::AbstractElement *element = nullptr;
  /// Strict terminal matcher: the end of a match starting at `begin`, or
  /// nullptr. `Literal` binds its compile-time literal matcher,
  /// `TerminalRule` its wrapped runtime matcher.
  const char *(*match)(const void *self, const char *begin) noexcept = nullptr;
  /// Lexical shape driving Replace/Insert admissibility and the fuzzy
  /// lookahead window.
  TerminalShape shape{};
  /// Canonical literal text and case contract feeding the fuzzy-Replace
  /// matcher. `hasLiteralText` is false for a `TerminalRule` without
  /// direct-literal metadata, which disables the Replace leg entirely.
  std::string_view literalValue{};
  /// Trace label: the keyword text (`Literal`) or the rule name
  /// (`TerminalRule`).
  std::string_view traceLabel{};
  bool caseSensitive = true;
  bool hasLiteralText = false;
  /// Selects the keyword ("[literal rule]") vs rule-terminal
  /// ("[terminal rule]") trace vocabulary.
  bool isKeyword = false;
  /// Keyword-only: seed the boundary split-Insert candidate (the keyword
  /// matched as a prefix of a longer token and a visible token immediately
  /// follows) and its forbid-Replace sibling-probe interaction.
  /// `TerminalRule` never seeds it.
  bool splitInsertEligible = false;
  /// Insert admissibility: `TerminalRule` gates synthetic inserts on
  /// `allows_terminal_rule_insert` (a rule terminal without canonical text
  /// is not inserted in front of visible source); `Literal` admits inserts
  /// from its shape alone.
  bool insertUsesRuleVisibilityGate = false;
  /// Span of a committed delete-scan leaf: `Literal` commits
  /// [scan origin, match end); `TerminalRule` commits
  /// [current cursor, match end).
  bool deleteScanLeafBeginsAtCursorStart = false;
  /// Word-like keyword: a strict match must not end on an identifier-like
  /// codepoint (word-boundary rule). Always false for `TerminalRule`.
  bool wordLike = false;
};
static_assert(std::is_trivially_copyable_v<TerminalRecoveryProfile>);

/// Fuzzy-Replace candidates near `cursorStart`, gated by the profile's
/// literal metadata and the prior-edits rule (a context that has already
/// edited keeps the Replace leg only when the shape allows it). Empty when
/// the Replace leg is disabled.
[[nodiscard]] LiteralFuzzyCandidates collect_terminal_replace_candidates(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const TerminalRecoveryFacts &facts);
[[nodiscard]] LiteralFuzzyCandidates collect_terminal_replace_candidates(
    ExpectContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const TerminalRecoveryFacts &facts);

/// Terminal recovery verdict ladder: enumerate the local repair candidates,
/// keep the best by `terminal_recovery_key` (`is_better_recovery_key`), then
/// realize it — select and apply in one flow, so the extended-delete-scan
/// match probe is wired once.
///   1. split-Insert — synthesize a separator when the keyword matched as a
///      prefix and a visible token immediately follows
///      (`profile.splitInsertEligible`);
///   2. fuzzy-Replace leaves — each candidate in `fuzzyCandidates`, unless
///      the forbid-Replace sibling probe suppresses them (see the comment in
///      the body; the expect overload never suppresses);
///   3. the insert / extended-delete-scan fallback inside
///      `complete_terminal_recovery_choice`.
[[nodiscard]] bool run_terminal_local_recovery(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const char *matchedEnd,
    const LiteralFuzzyCandidates &fuzzyCandidates,
    const TerminalRecoveryFacts &facts);
[[nodiscard]] bool run_terminal_local_recovery(
    ExpectContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const char *matchedEnd,
    const LiteralFuzzyCandidates &fuzzyCandidates,
    const TerminalRecoveryFacts &facts);

/// Recovery-mode continuation after the strict terminal match failed: the
/// local-recovery-blocked and edit-budget gates, the gated fuzzy-candidate
/// collection, and the verdict ladder.
[[nodiscard]] bool recover_terminal_after_strict_miss(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const char *matchedEnd,
    const TerminalRecoveryFacts &effectiveFacts);

/// `Literal`'s recovery probes. Arm order per probe (each early-returns):
/// `probeRecoverable` — direct match with visible continuation, prior-edits
/// gate, fuzzy candidates, nearby delete-scan; the entry variant admits the
/// terminal entry probe first and requires single-edit fuzzy candidates; the
/// consumes-visible variant requires a non-empty direct match and consuming
/// fuzzy candidates. The prior-edits gate blocks every later arm, including
/// the delete-scan.
[[nodiscard]] bool literal_probe_recoverable(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);
[[nodiscard]] bool literal_probe_recoverable_at_entry(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);
[[nodiscard]] bool literal_probe_recoverable_at_entry_consumes_visible(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);

/// `TerminalRule`'s recovery probes. Unlike `Literal`'s, the prior-edits
/// gate here scopes to the fuzzy leg only (the delete-scan stays reachable),
/// and the entry variant leads with the fuzzy single-edit arm.
[[nodiscard]] bool terminal_rule_probe_recoverable(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);
[[nodiscard]] bool terminal_rule_probe_recoverable_at_entry(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);
[[nodiscard]] bool terminal_rule_probe_recoverable_at_entry_consumes_visible(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile);

} // namespace pegium::parser::detail
