#pragma once
/// Parser terminal matching a fixed literal.
///
/// The strict-mode members (the strict overload of `probeMatchHere`,
/// `probe_impl`, the strict branch of `parse_impl`, and the compile-time
/// `terminal()` matcher — including the word-boundary rule guarding it) are
/// defined in-class below; this is the hot path exercised by clean input.
/// Recovery- and expect-mode members (`terminal_recovery_profile`,
/// `findReplaceCandidates`, `parse_terminal_recovery_impl`, the recovery
/// overload of `probeMatchHere`, and `probeRecoverable` /
/// `probeRecoverableAtEntry` / `probeRecoverableAtEntryConsumesVisible`) are
/// declared here and defined out-of-class in LiteralRecovery.hpp — one file
/// rather than a recovery/expect pair, because `parse_terminal_recovery_impl`
/// is a single template dispatching between the two modes with `if
/// constexpr`, not two separate members. A translation unit that drives a
/// Literal through recovery or expect mode must include that header in
/// addition to this one.
#include <algorithm>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/LiteralFuzzyMatcher.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/TerminalLocalRecovery.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <string>

namespace pegium::parser {

template <Expression... Elements> struct Group;

template <auto literal, bool case_sensitive = true>
struct Literal final : grammar::Literal {
  static_assert(std::ranges::none_of(literal, [](char c) { return c == '\0'; }),
                "Literal cannot contain '\\0'.");
  static constexpr bool nullable = literal.empty();
  static constexpr bool isFailureSafe = true;
  using type = std::string_view;
  using grammar::Literal::getValue;
  std::string_view getValue() const noexcept override {
    return {literal.begin(), literal.size()};
  }
  std::string_view getValue(const CstNodeView &node) const noexcept override {
    if (!node.isRecovered()) [[likely]] {
      return node.getText();
    }
    return getValue();
  }
  std::string_view getRawValue(const CstNodeView &node) const noexcept {
    return getValue(node);
  }
  bool isCaseSensitive() const noexcept override { return case_sensitive; }
  std::string_view getDocumentation() const noexcept override {
    return _documentation;
  }

  /// Attach human-readable documentation to this keyword, surfaced on hover via
  /// the DocumentationProvider. Returns a copy so it composes naturally:
  /// `"class"_kw.doc("…") + …`. The text is stored as a non-owning view — pass
  /// a string literal or a string that outlives the grammar.
  [[nodiscard]] constexpr Literal doc(std::string_view documentation) const {
    Literal copy{*this};
    copy._documentation = documentation;
    return copy;
  }

private:
  std::string_view _documentation;
  friend struct detail::ParseAccess;
  friend struct detail::ProbeAccess;
  template <Expression... Elements> friend struct Group;
  template <typename> friend struct SkipperWrapped;

  /// Type-erased view of this literal consumed by the shared terminal
  /// local-recovery driver (TerminalLocalRecovery.cpp). The matcher
  /// trampoline is static per specialization; the shape/word-likeness fields
  /// are the specialization's compile-time facts, so the profile is built on
  /// the stack per call. Recovery-mode; defined out-of-class in
  /// LiteralRecovery.hpp.
  [[nodiscard]] detail::TerminalRecoveryProfile
  terminal_recovery_profile() const noexcept;

  /// Recovery-mode; defined out-of-class in LiteralRecovery.hpp.
  template <EditableParseModeContext Context>
  [[nodiscard]] detail::LiteralFuzzyCandidates findReplaceCandidates(
      Context &ctx, const char *cursorStart,
      detail::TerminalRecoveryFacts terminalRecoveryFacts = {}) const noexcept;

  bool probe_impl(const ParseContext &ctx) const noexcept {
    const char *const matchEnd = terminal(ctx.cursor());
    if (matchEnd == nullptr) {
      return false;
    }
    return !has_word_boundary_violation(ctx, matchEnd);
  }

  /// Error-tolerant match of this literal at the cursor, walking a fixed ladder
  /// from strongest to weakest match: (1) an exact terminal match commits a
  /// leaf; otherwise, unless local recovery is blocked or no edit budget
  /// remains, (2) a low-cost fuzzy Replace repairs a truncated/typoed
  /// occurrence (the shared `detail::run_terminal_local_recovery` ladder). In
  /// expect/editable mode the ladder additionally admits the completion anchor,
  /// an anchor-bounded exact match, and a partial literal prefix up to the
  /// anchor (each registered as a keyword) before the same fuzzy fallback.
  /// Recovery- and expect-mode (dispatched by `if constexpr` on `Context`);
  /// defined out-of-class in LiteralRecovery.hpp.
  template <EditableParseModeContext Context>
  bool parse_terminal_recovery_impl(
      Context &ctx,
      const detail::TerminalRecoveryFacts &terminalRecoveryFacts) const;

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (StrictParseModeContext<Context>) {
      const char *const matchedEnd = terminal(ctx.cursor());
      if (matchedEnd == nullptr ||
          has_word_boundary_violation(ctx, matchedEnd)) {
        return false;
      }
      PEGIUM_RECOVERY_TRACE("[literal rule] direct match '", getValue(),
                            "' at ", ctx.cursorOffset());
      ctx.leaf(matchedEnd, this);
      return true;
    } else {
      return parse_terminal_recovery_impl(ctx, {});
    }
  }

public:
  /// True iff the literal matches AT the cursor either strictly or via a
  /// low-cost fuzzy candidate. Unlike `probeRecoverable`, this never scans
  /// past the cursor (no delete-scan, no entry-probe gating). Used by
  /// `NotPredicate` in recovery mode so a many-loop guarded by
  /// `!"keyword"_kw` stops at a truncated/typoed occurrence of that keyword
  /// instead of greedily consuming it as an identifier. Recovery-mode;
  /// defined out-of-class in LiteralRecovery.hpp.
  bool probeMatchHere(RecoveryContext &ctx) const noexcept;

  /// Strict-pass overload: same shape as the recovery probe but bypasses the
  /// edit-state machinery and never consults caches that only exist in
  /// recovery contexts. The fuzzy window is sized from the literal length
  /// alone so the failure-tracking pass can detect a near-keyword (e.g.
  /// `initialStat` for `initialState`) and bail out instead of letting an
  /// outer `many(!K + Item)` swallow it as an identifier. The acceptance
  /// threshold here is intentionally tighter than recovery's: only single-
  /// edit candidates with a length close to the literal qualify, otherwise
  /// the strict pass would over-reject identifiers that happen to share a
  /// prefix with a keyword.
  template <typename Context>
    requires StrictParseModeContext<Context>
  bool probeMatchHere(Context &ctx) const noexcept {
    const char *const cursorStart = ctx.cursor();
    if (const char *const matchedEnd = terminal(cursorStart);
        matchedEnd != nullptr && !has_word_boundary_violation(ctx, matchedEnd)) {
      return true;
    }
    if constexpr (allow_replace) {
      constexpr std::size_t kLiteralSize = literal.size();
      // Single-edit threshold: only worth probing for keywords long
      // enough that a 1-edit near-match is meaningful (4+ chars). Below
      // that most identifiers would fuzzy-match every short keyword and
      // a number of existing recovery unit tests intentionally cover the
      // delete-prefix-then-low-confidence-replace path.
      if constexpr (kLiteralSize < 4U) {
        return false;
      }
      constexpr std::size_t kSlack = 1U;
      const std::size_t window = kLiteralSize + kSlack;
      const auto remaining = static_cast<std::size_t>(ctx.end - cursorStart);
      const std::string_view view{cursorStart, std::min(window, remaining)};
      // Linear O(N) single-edit detector keeps the failure-tracking strict
      // pass free of the recovery Levenshtein DP. For single-byte (ASCII)
      // codepoints the acceptance set matches the recovery DP probe
      // (`distance == 1 && |consumed - N| <= 1`); multibyte single-edit cases
      // are conservatively deferred to that DP (this byte-granular probe only
      // declines to fire the strict hint for them).
      return detail::literal_has_single_edit_strict_match(getValue(), view,
                                                          case_sensitive);
    }
    return false;
  }

  /// Recovery-mode; defined out-of-class in LiteralRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const noexcept;

  /// Recovery-mode; defined out-of-class in LiteralRecovery.hpp.
  bool probeRecoverableAtEntry(RecoveryContext &ctx) const noexcept;

  /// Recovery-mode; defined out-of-class in LiteralRecovery.hpp.
  bool
  probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const noexcept;

  constexpr const char *terminal(const char *begin) const noexcept {
    for (std::size_t charIndex = 0; charIndex < literal.size(); ++charIndex) {
      if constexpr (case_sensitive) {
        if (begin[charIndex] != literal[charIndex]) {
          return nullptr;
        }
      } else {
        if (utils::tolower(begin[charIndex]) != literal[charIndex]) {
          return nullptr;
        }
      }
    }
    return begin + literal.size();
  }

  constexpr const char *terminal(const std::string &text) const noexcept {
    return terminal(text.c_str());
  }

  /// Create an insensitive Literal
  /// @return the insensitive Literal
  constexpr auto i() const noexcept {
    return Literal<toLower(), isCaseSensitive(literal)>{};
  }

  constexpr bool isNullable() const noexcept override { return nullable; }

private:
  static constexpr std::string_view literalValue{literal.data(),
                                                 literal.size()};
  // Keep generic recovery able to synthesize only compact literals and avoid
  // inventing long grammar keywords.
  static constexpr auto terminalShape =
      detail::make_terminal_shape_from_literal(literalValue);
  static constexpr bool allow_replace = terminalShape.allowsReplace();
  // Cache word-likeness per specialization so the strict match hot path does
  // not re-scan the literal's bytes for every successful match.
  static constexpr bool is_word_like_literal =
      detail::is_word_like_terminal(literalValue);

  template <typename Context>
  [[nodiscard]] static constexpr bool
  has_word_boundary_violation(const Context &ctx,
                              const char *end) noexcept {
    if constexpr (!is_word_like_literal) {
      (void)ctx;
      (void)end;
      return false;
    } else {
      // Bounds-checked decode+classify (handles truncated multi-byte
      // tails in the last 1-3 bytes of the buffer — ASan-flagged on
      // adversarial fuzz input).
      return detail::is_identifier_like_codepoint_at(end, ctx.end);
    }
  }

  static constexpr auto toLower() {
    decltype(literal) newLiteral;
    std::ranges::transform(literal, newLiteral.begin(),
                           [](char c) { return utils::tolower(c); });
    return newLiteral;
  }

  static constexpr bool isCaseSensitive(auto lit) {
    return std::ranges::none_of(
        lit, [](char c) { return utils::isLetter(c); });
  }
};

template <typename T> struct IsLiteralImpl : std::false_type {};
template <auto literal, bool case_sensitive>
struct IsLiteralImpl<Literal<literal, case_sensitive>> : std::true_type {};

template <auto literal, bool case_sensitive>
struct detail::IsTerminalAtom<Literal<literal, case_sensitive>>
    : std::true_type {};

template <typename T>
concept IsLiteral = IsLiteralImpl<T>::value;

} // namespace pegium::parser
