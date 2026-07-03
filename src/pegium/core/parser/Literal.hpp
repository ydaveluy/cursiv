#pragma once
/// Parser terminal matching a fixed literal.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/LiteralFuzzyMatcher.hpp>
#include <pegium/core/parser/ParseContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/TerminalRecoverySupport.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <ranges>
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

  using LocalRecoveryChoice = detail::TerminalRecoveryCandidate;

  template <EditableParseModeContext Context>
  [[nodiscard]] detail::LiteralFuzzyCandidates findReplaceCandidates(
      Context &ctx, const char *cursorStart,
      detail::TerminalRecoveryFacts terminalRecoveryFacts = {}) const noexcept {
    detail::LiteralFuzzyCandidates filteredCandidates;
    if constexpr (!allow_replace) {
      (void)ctx;
      (void)cursorStart;
      return filteredCandidates;
    } else {
      return detail::collect_literal_replace_candidates(
          ctx, cursorStart, getValue(), case_sensitive, terminalShape,
          terminalRecoveryFacts);
    }
  }

  /// Extended-delete-scan match probe shared by the select/apply terminal
  /// recovery passes: from `scanStart`, returns the terminal's end iff the scan
  /// is admissible AND the match can be committed, else nullptr.
  template <EditableParseModeContext Context>
  [[nodiscard]] const char *
  delete_scan_match_end(Context &ctx, const char *cursorStart,
                        const char *scanStart) const noexcept {
    if (!detail::allows_extended_terminal_delete_scan_match(ctx, cursorStart,
                                                            scanStart)) {
      return nullptr;
    }
    const char *const scanEnd = terminal(scanStart);
    if (scanEnd == nullptr || !detail::can_apply_recovery_match(ctx, scanEnd)) {
      return nullptr;
    }
    return scanEnd;
  }

  /// Terminal recovery verdict-ladder (recovery branch only): enumerate the
  /// local repair candidates, keep the best by `terminal_recovery_key`
  /// (`is_better_recovery_key`), then realize it — select and apply in one
  /// flow (mirrors TerminalRule::try_local_recovery), so the delete-scan match
  /// probe is wired once.
  ///   1. split-Insert — synthesize a separator when the keyword matched as a
  ///      prefix and a visible token immediately follows (`splitInsertConsidered`);
  ///   2. fuzzy-Replace leaves — each enumerated fuzzy candidate, unless
  ///      `allow_replace` is off or the forbid-Replace sibling probe suppresses
  ///      them (see the comment in the body);
  ///   3. the insert / extended-delete-scan fallback inside
  ///      `complete_terminal_recovery_choice`.
  template <EditableParseModeContext Context>
  bool try_local_recovery(
      Context &ctx, const char *cursorStart, const char *matchedEnd,
      const detail::LiteralFuzzyCandidates &fuzzyCandidates,
      detail::TerminalRecoveryFacts terminalRecoveryFacts) const {
    LocalRecoveryChoice bestChoice;
    auto considerChoice = [&bestChoice](const LocalRecoveryChoice &choice) {
      if (detail::is_better_recovery_key(
              detail::terminal_recovery_key(choice),
              detail::terminal_recovery_key(bestChoice))) {
        bestChoice = choice;
      }
    };

    const bool splitInsertConsidered =
        matchedEnd != nullptr &&
        detail::has_immediate_visible_continuation(ctx, matchedEnd);
    if (splitInsertConsidered) {
      considerChoice(detail::evaluate_insert_synthetic_gap_terminal_candidate(
          ctx, cursorStart, matchedEnd, this, 3u));
    }

    // forbid-Replace sibling probe: when re-descending with the whole-keyword
    // fuzzy Replace forbidden AND a boundary split-Insert is already enumerable
    // (the keyword matched exactly as a prefix with a visible continuation),
    // suppress the Replace candidates so the keep-keyword split-Insert is the
    // surviving choice. Contexts without the flag (non-recovery) never suppress.
    const bool suppressFuzzyReplace = [&]() constexpr noexcept {
      if constexpr (requires { ctx.forbidWholeKeywordFuzzyReplace; }) {
        return ctx.forbidWholeKeywordFuzzyReplace && splitInsertConsidered;
      } else {
        return false;
      }
    }();

    if constexpr (allow_replace) {
      if (!suppressFuzzyReplace) {
        for (const auto &fuzzyCandidate : fuzzyCandidates) {
          const char *const fuzzyEnd = cursorStart + fuzzyCandidate.consumed;
          if (detail::can_apply_recovery_match(ctx, fuzzyEnd)) {
            considerChoice(detail::evaluate_replace_leaf_terminal_candidate(
                ctx, cursorStart, fuzzyEnd, this, fuzzyCandidate.cost,
                fuzzyCandidate.distance));
          }
        }
      }
    }

    // The extended-delete-scan match probe is shared by the completion (select)
    // and the delete-scan application below.
    const auto matchRecoverableTerminal =
        [this, &ctx, cursorStart](const char *scanStart) noexcept {
          return delete_scan_match_end(ctx, cursorStart, scanStart);
        };

    const auto choice = detail::complete_terminal_recovery_choice(
        ctx, cursorStart, this, terminalRecoveryFacts, terminalShape,
        allow_insert, bestChoice, matchRecoverableTerminal,
        [this, &ctx, cursorStart](const char *scanEnd) {
          ctx.leaf(cursorStart, scanEnd, this, false, true);
        });

    return detail::apply_terminal_recovery_choice(
        ctx, choice, this,
        [this, &ctx, matchedEnd]() {
          if (matchedEnd == nullptr ||
              !detail::apply_insert_synthetic_gap_and_match_recovery_edit(
                  ctx, matchedEnd, this, "Expecting separator")) {
            return false;
          }
          if constexpr (RecoveryParseModeContext<Context>) {
            PEGIUM_RECOVERY_TRACE(
                "[literal rule] split direct match for '", getValue(), "' at ",
                static_cast<TextOffset>(matchedEnd - ctx.begin));
          }
          return true;
        },
        [this, &ctx, cursorStart, &choice]() {
          const char *const fuzzyEnd = cursorStart + choice.consumed;
          if (!detail::can_apply_recovery_match(ctx, fuzzyEnd) ||
              !ctx.replaceLeaf(fuzzyEnd, this, choice.cost.budgetCost,
                               /*hidden=*/false, choice.distance)) {
            return false;
          }
          if constexpr (RecoveryParseModeContext<Context>) {
            PEGIUM_RECOVERY_TRACE("[literal rule] fuzzy match '", getValue(),
                                  "' at ", ctx.cursorOffset(),
                                  " distance=", choice.distance,
                                  " cost=", choice.cost.budgetCost,
                                  " rank=", choice.cost.primaryRankCost);
          }
          return true;
        },
        [this, &ctx]() {
          if constexpr (RecoveryParseModeContext<Context>) {
            PEGIUM_RECOVERY_TRACE("[literal rule] inserted '", getValue(),
                                  "' at ", ctx.cursorOffset());
          }
        },
        [this, &ctx, cursorStart, terminalRecoveryFacts,
         &matchRecoverableTerminal]() {
          return detail::recover_by_terminal_delete_scan(
              ctx, matchRecoverableTerminal,
              [this, &ctx, cursorStart](const char *scanEnd) {
                if constexpr (RecoveryParseModeContext<Context>) {
                  PEGIUM_RECOVERY_TRACE("[literal rule] delete-scan match '",
                                        getValue(), "' at ",
                                        ctx.cursorOffset());
                }
                ctx.leaf(cursorStart, scanEnd, this, false, true);
              },
              terminalRecoveryFacts, terminalShape);
        });
  }

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
  /// occurrence (`applyFuzzyRecovery` -> `try_local_recovery`). In
  /// expect/editable mode the ladder additionally admits the completion anchor,
  /// an anchor-bounded exact match, and a partial literal prefix up to the
  /// anchor (each registered as a keyword) before the same fuzzy fallback.
  template <EditableParseModeContext Context>
  bool parse_terminal_recovery_impl(
      Context &ctx,
      const detail::TerminalRecoveryFacts &terminalRecoveryFacts) const {
    const auto effectiveRecoveryFacts =
        detail::effective_terminal_recovery_facts(ctx, terminalRecoveryFacts);
    const char *const cursorStart = ctx.cursor();
    const bool hasHadEdits = detail::context_has_had_edits(ctx);
    const char *const matchedEnd = terminal(cursorStart);
    const auto applyFuzzyRecovery = [&]() -> bool {
      const auto fuzzyCandidates =
          (hasHadEdits &&
           !detail::allows_fuzzy_replace_after_prior_edits(terminalShape))
              ? detail::LiteralFuzzyCandidates{}
              : findReplaceCandidates(ctx, cursorStart, effectiveRecoveryFacts);
      return try_local_recovery(ctx, cursorStart, matchedEnd, fuzzyCandidates,
                                effectiveRecoveryFacts);
    };

    if constexpr (RecoveryParseModeContext<Context>) {
      if (matchedEnd != nullptr && !has_word_boundary_violation(ctx, matchedEnd)) {
        PEGIUM_RECOVERY_TRACE("[literal rule] direct match '", getValue(),
                              "' at ", ctx.cursorOffset());
        ctx.leaf(matchedEnd, this);
        return true;
      }
      if (effectiveRecoveryFacts.localRecoveryBlocked) {
        return false;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      if (applyFuzzyRecovery()) {
        return true;
      }

      PEGIUM_RECOVERY_TRACE("[literal rule] fail '", getValue(), "' at ",
                            ctx.cursorOffset());
      return false;
    } else {
      if (ctx.reachedAnchor()) {
        ctx.addKeyword(this);
        return true;
      }

      if (matchedEnd != nullptr && ctx.canTraverseUntil(matchedEnd) &&
          !has_word_boundary_violation(ctx, matchedEnd)) {
        ctx.leaf(matchedEnd, this);
        return true;
      }
      if (const auto anchorDistance =
              static_cast<std::size_t>(ctx.anchor - cursorStart);
          cursorStart < ctx.anchor && anchorDistance <= literal.size()) {
        bool prefixMatches = true;
        for (std::size_t index = 0; index < anchorDistance; ++index) {
          const auto lhs = static_cast<unsigned char>(cursorStart[index]);
          const auto rhs = static_cast<unsigned char>(literal[index]);
          if constexpr (case_sensitive) {
            if (lhs != rhs) {
              prefixMatches = false;
              break;
            }
          } else if (utils::tolower(lhs) != rhs) {
            prefixMatches = false;
            break;
          }
        }
        if (prefixMatches) {
          ctx.addKeyword(this);
          return true;
        }
      }

      if (effectiveRecoveryFacts.localRecoveryBlocked) {
        return false;
      }
      if (!ctx.canEdit()) {
        return false;
      }
      return applyFuzzyRecovery();
    }
  }

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
  /// instead of greedily consuming it as an identifier.
  bool probeMatchHere(RecoveryContext &ctx) const noexcept {
    const char *const cursorStart = ctx.cursor();
    if (const char *const matchedEnd = terminal(cursorStart);
        matchedEnd != nullptr && !has_word_boundary_violation(ctx, matchedEnd)) {
      return true;
    }
    if (ctx.hasHadEdits() &&
        !detail::allows_fuzzy_replace_after_prior_edits(terminalShape)) {
      return false;
    }
    if constexpr (allow_replace) {
      const auto fuzzyCandidates = findReplaceCandidates(ctx, cursorStart);
      if (!fuzzyCandidates.empty()) {
        return true;
      }
    }
    return false;
  }

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

  bool probeRecoverable(RecoveryContext &ctx) const noexcept {
    const auto localRecoveryFacts =
        detail::local_skip_terminal_recovery_facts(ctx);
    const char *const cursorStart = ctx.cursor();
    if (const char *const matchedEnd = terminal(cursorStart);
        matchedEnd != nullptr &&
        detail::has_immediate_visible_continuation(ctx, matchedEnd)) {
      return true;
    }
    if (ctx.hasHadEdits() &&
        !detail::allows_fuzzy_replace_after_prior_edits(
            terminalShape)) {
      return false;
    }
    if constexpr (allow_replace) {
      const auto fuzzyCandidates = findReplaceCandidates(ctx, cursorStart);
      if (!fuzzyCandidates.empty()) {
        return true;
      }
    }
    return detail::probe_nearby_delete_scan_match(
        ctx,
        [this](const char *scanCursor) noexcept {
          return terminal(scanCursor);
        },
        localRecoveryFacts, terminalShape);
  }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const noexcept {
    const auto localRecoveryFacts =
        detail::local_skip_terminal_recovery_facts(ctx);
    if constexpr (allow_insert) {
      if (detail::allows_terminal_entry_probe(ctx, terminalShape)) {
        return true;
      }
    }
    if (ctx.hasHadEdits() &&
        !detail::allows_fuzzy_replace_after_prior_edits(
            terminalShape)) {
      return false;
    }
    const char *const cursorStart = ctx.cursor();
    if (const char *const matchedEnd = terminal(cursorStart);
        matchedEnd != nullptr &&
        detail::has_immediate_visible_continuation(ctx, matchedEnd)) {
      return true;
    }
    if constexpr (allow_replace) {
      const auto fuzzyCandidates = findReplaceCandidates(ctx, cursorStart);
      if (std::ranges::any_of(
              fuzzyCandidates, [](const auto &candidate) constexpr noexcept {
                return detail::allows_entry_probe_fuzzy_candidate(candidate);
              })) {
        return true;
      }
    }
    return detail::probe_nearby_delete_scan_match(
        ctx,
        [this](const char *scanCursor) noexcept {
          return terminal(scanCursor);
        },
        localRecoveryFacts, terminalShape);
  }

  bool
  probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const noexcept {
    const auto localRecoveryFacts =
        detail::local_skip_terminal_recovery_facts(ctx);
    if (ctx.hasHadEdits() &&
        !detail::allows_fuzzy_replace_after_prior_edits(
            terminalShape)) {
      return false;
    }
    const char *const cursorStart = ctx.cursor();
    const char *const matchedEnd = terminal(cursorStart);
    if (matchedEnd != nullptr &&
        !has_word_boundary_violation(ctx, matchedEnd) &&
        matchedEnd > cursorStart) {
      return true;
    }
    if constexpr (allow_replace) {
      const auto fuzzyCandidates = findReplaceCandidates(ctx, cursorStart);
      if (std::ranges::any_of(
              fuzzyCandidates, [](const auto &candidate) constexpr noexcept {
                return candidate.consumed > 0u &&
                       detail::allows_entry_probe_fuzzy_candidate(candidate);
              })) {
        return true;
      }
    }
    return detail::probe_nearby_delete_scan_match(
        ctx,
        [this](const char *scanCursor) noexcept {
          return terminal(scanCursor);
        },
        localRecoveryFacts, terminalShape);
  }

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
  static constexpr bool allow_insert = terminalShape.allowsInsert();
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
