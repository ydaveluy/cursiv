/// Shared terminal local-recovery driver for `Literal` and `TerminalRule`
/// (see TerminalLocalRecovery.hpp). Compiled once; the combinators reach it
/// through per-call `TerminalRecoveryProfile` tables.

#include <algorithm>

#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/TerminalLocalRecovery.hpp>

namespace pegium::parser::detail {

namespace {

[[nodiscard]] const char *profile_match(const TerminalRecoveryProfile &profile,
                                        const char *begin) noexcept {
  return profile.match(profile.self, begin);
}

/// `Literal`'s word-boundary rule: a word-like keyword match must not end on
/// an identifier-like codepoint. `TerminalRule` profiles carry
/// `wordLike == false`, so the rule never fires for them.
[[nodiscard]] bool
profile_word_boundary_violation(const RecoveryContext &ctx,
                                const TerminalRecoveryProfile &profile,
                                const char *end) noexcept {
  return profile.wordLike && ctx.isWordCharAt(end, ctx.end);
}

/// Extended-delete-scan match probe shared by the select
/// (`complete_terminal_recovery_choice`) and apply
/// (`recover_by_terminal_delete_scan`) passes: from `scanStart`, returns the
/// terminal's end iff the scan is admissible AND the match can be committed,
/// else nullptr.
template <EditableParseModeContext Context>
[[nodiscard]] const char *
delete_scan_match_end(Context &ctx, const TerminalRecoveryProfile &profile,
                      const char *cursorStart, const char *scanStart) noexcept {
  if (!allows_extended_terminal_delete_scan_match(ctx, cursorStart,
                                                  scanStart)) {
    return nullptr;
  }
  const char *const scanEnd = profile_match(profile, scanStart);
  if (scanEnd == nullptr || !can_apply_recovery_match(ctx, scanEnd)) {
    return nullptr;
  }
  return scanEnd;
}

} // namespace

template <EditableParseModeContext Context>
LiteralFuzzyCandidates collect_terminal_replace_candidates(
    Context &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const TerminalRecoveryFacts &facts) {
  if (!profile.hasLiteralText ||
      (context_has_had_edits(ctx) &&
       !allows_fuzzy_replace_after_prior_edits(profile.shape))) {
    return {};
  }
  return collect_literal_replace_candidates(ctx, cursorStart,
                                            profile.literalValue,
                                            profile.caseSensitive,
                                            profile.shape, facts);
}

template LiteralFuzzyCandidates collect_terminal_replace_candidates(
    RecoveryContext &, const TerminalRecoveryProfile &, const char *,
    const TerminalRecoveryFacts &);
template LiteralFuzzyCandidates collect_terminal_replace_candidates(
    ExpectContext &, const TerminalRecoveryProfile &, const char *,
    const TerminalRecoveryFacts &);

bool run_terminal_local_recovery(RecoveryContext &ctx,
                                 const TerminalRecoveryProfile &profile,
                                 const char *cursorStart,
                                 const char *matchedEnd,
                                 const LiteralFuzzyCandidates &fuzzyCandidates,
                                 const TerminalRecoveryFacts &facts) {
  TerminalRecoveryCandidate bestChoice;
  const auto considerChoice =
      [&bestChoice](const TerminalRecoveryCandidate &choice) {
        if (is_better_recovery_key(terminal_recovery_key(choice),
                                   terminal_recovery_key(bestChoice))) {
          bestChoice = choice;
        }
      };

  const bool splitInsertConsidered =
      profile.splitInsertEligible && matchedEnd != nullptr &&
      has_immediate_visible_continuation(ctx, matchedEnd);
  if (splitInsertConsidered) {
    considerChoice(evaluate_insert_synthetic_gap_terminal_candidate(
        ctx, cursorStart, matchedEnd, profile.element, 3u));
  }

  // forbid-Replace sibling probe: when re-descending with the whole-keyword
  // fuzzy Replace forbidden AND a boundary split-Insert is already enumerable
  // (the keyword matched exactly as a prefix with a visible continuation),
  // suppress the Replace candidates so the keep-keyword split-Insert is the
  // surviving choice.
  const bool suppressFuzzyReplace =
      ctx.forbidWholeKeywordFuzzyReplace && splitInsertConsidered;
  if (!suppressFuzzyReplace) {
    consider_replace_leaf_candidates(ctx, cursorStart, profile.element,
                                     fuzzyCandidates, bestChoice);
  }

  // Insert admissibility per kind: `TerminalRule` gates on
  // `allows_terminal_rule_insert` (a canonical-text-less rule terminal is not
  // inserted in front of visible source) and on shape/metadata consistency;
  // `Literal` admits inserts from its shape alone.
  const bool allowInsert =
      profile.insertUsesRuleVisibilityGate
          ? allows_terminal_rule_insert(ctx, profile.shape) &&
                (!profile.hasLiteralText || profile.shape.allowsInsert())
          : profile.shape.allowsInsert();

  // The extended-delete-scan match probe is shared by the completion (select)
  // and the delete-scan application below.
  const auto matchRecoverableTerminal =
      [&ctx, &profile, cursorStart](const char *scanStart) noexcept {
        return delete_scan_match_end(ctx, profile, cursorStart, scanStart);
      };

  const auto choice = complete_terminal_recovery_choice(
      ctx, cursorStart, profile.element, facts, profile.shape, allowInsert,
      bestChoice, matchRecoverableTerminal,
      [&ctx, &profile, cursorStart](const char *scanEnd) {
        if (profile.deleteScanLeafBeginsAtCursorStart) {
          ctx.leaf(cursorStart, scanEnd, profile.element, false, true);
        } else {
          ctx.leaf(scanEnd, profile.element, false, true);
        }
      });

  return apply_terminal_recovery_choice(
      ctx, choice, profile.element,
      [&ctx, &profile, matchedEnd]() {
        if (!profile.splitInsertEligible || matchedEnd == nullptr ||
            !apply_insert_synthetic_gap_and_match_recovery_edit(
                ctx, matchedEnd, profile.element, "Expecting separator")) {
          return false;
        }
        PEGIUM_RECOVERY_TRACE("[literal rule] split direct match for '",
                              profile.traceLabel, "' at ",
                              static_cast<TextOffset>(matchedEnd - ctx.begin));
        return true;
      },
      [&ctx, &profile, cursorStart, &choice]() {
        const char *const replaceEnd = cursorStart + choice.consumed;
        if (!can_apply_recovery_match(ctx, replaceEnd) ||
            !ctx.replaceLeaf(replaceEnd, profile.element,
                             choice.cost.budgetCost,
                             /*hidden=*/false, choice.distance)) {
          return false;
        }
        if (profile.isKeyword) {
          PEGIUM_RECOVERY_TRACE("[literal rule] fuzzy match '",
                                profile.traceLabel, "' at ", ctx.cursorOffset(),
                                " distance=", choice.distance,
                                " cost=", choice.cost.budgetCost,
                                " rank=", choice.cost.primaryRankCost);
        } else {
          PEGIUM_RECOVERY_TRACE("[terminal rule] replace-literal ",
                                profile.traceLabel,
                                " offset=", ctx.cursorOffset(),
                                " distance=", choice.distance,
                                " cost=", choice.cost.budgetCost,
                                " rank=", choice.cost.primaryRankCost);
        }
        return true;
      },
      [&ctx, &profile]() {
        if (profile.isKeyword) {
          PEGIUM_RECOVERY_TRACE("[literal rule] inserted '", profile.traceLabel,
                                "' at ", ctx.cursorOffset());
        } else {
          PEGIUM_RECOVERY_TRACE("[terminal rule] insert-synthetic ",
                                profile.traceLabel,
                                " offset=", ctx.cursorOffset());
        }
      },
      [&ctx, &profile, cursorStart, &facts, &matchRecoverableTerminal]() {
        return recover_by_terminal_delete_scan(
            ctx, matchRecoverableTerminal,
            [&ctx, &profile, cursorStart](const char *scanEnd) {
              if (profile.isKeyword) {
                PEGIUM_RECOVERY_TRACE("[literal rule] delete-scan match '",
                                      profile.traceLabel, "' at ",
                                      ctx.cursorOffset());
              } else {
                PEGIUM_RECOVERY_TRACE("[terminal rule] delete-scan match ",
                                      profile.traceLabel,
                                      " offset=", ctx.cursorOffset());
              }
              if (profile.deleteScanLeafBeginsAtCursorStart) {
                ctx.leaf(cursorStart, scanEnd, profile.element, false, true);
              } else {
                ctx.leaf(scanEnd, profile.element, false, true);
              }
            },
            facts, profile.shape);
      });
}

bool run_terminal_local_recovery(ExpectContext &ctx,
                                 const TerminalRecoveryProfile &profile,
                                 const char *cursorStart,
                                 const char *matchedEnd,
                                 const LiteralFuzzyCandidates &fuzzyCandidates,
                                 const TerminalRecoveryFacts &facts) {
  TerminalRecoveryCandidate bestChoice;
  const auto considerChoice =
      [&bestChoice](const TerminalRecoveryCandidate &choice) {
        if (is_better_recovery_key(terminal_recovery_key(choice),
                                   terminal_recovery_key(bestChoice))) {
          bestChoice = choice;
        }
      };

  const bool splitInsertConsidered =
      profile.splitInsertEligible && matchedEnd != nullptr &&
      has_immediate_visible_continuation(ctx, matchedEnd);
  if (splitInsertConsidered) {
    considerChoice(evaluate_insert_synthetic_gap_terminal_candidate(
        ctx, cursorStart, matchedEnd, profile.element, 3u));
  }

  // `ExpectContext` carries no forbid-Replace sibling-probe flag: the
  // whole-keyword fuzzy Replace is never suppressed on the expect path.
  consider_replace_leaf_candidates(ctx, cursorStart, profile.element,
                                   fuzzyCandidates, bestChoice);

  // Insert admissibility per kind, mirroring the recovery overload above.
  const bool allowInsert =
      profile.insertUsesRuleVisibilityGate
          ? allows_terminal_rule_insert(ctx, profile.shape) &&
                (!profile.hasLiteralText || profile.shape.allowsInsert())
          : profile.shape.allowsInsert();

  const auto matchRecoverableTerminal =
      [&ctx, &profile, cursorStart](const char *scanStart) noexcept {
        return delete_scan_match_end(ctx, profile, cursorStart, scanStart);
      };

  const auto commitDeleteScanLeaf =
      [&ctx, &profile, cursorStart](const char *scanEnd) {
        if (profile.deleteScanLeafBeginsAtCursorStart) {
          ctx.leaf(cursorStart, scanEnd, profile.element, false, true);
        } else {
          ctx.leaf(scanEnd, profile.element, false, true);
        }
      };

  const auto choice = complete_terminal_recovery_choice(
      ctx, cursorStart, profile.element, facts, profile.shape, allowInsert,
      bestChoice, matchRecoverableTerminal, commitDeleteScanLeaf);

  return apply_terminal_recovery_choice(
      ctx, choice, profile.element,
      [&ctx, &profile, matchedEnd]() {
        return profile.splitInsertEligible && matchedEnd != nullptr &&
               apply_insert_synthetic_gap_and_match_recovery_edit(
                   ctx, matchedEnd, profile.element, "Expecting separator");
      },
      [&ctx, &profile, cursorStart, &choice]() {
        const char *const replaceEnd = cursorStart + choice.consumed;
        return can_apply_recovery_match(ctx, replaceEnd) &&
               ctx.replaceLeaf(replaceEnd, profile.element,
                               choice.cost.budgetCost,
                               /*hidden=*/false, choice.distance);
      },
      []() {},
      [&ctx, &profile, &facts, &matchRecoverableTerminal,
       &commitDeleteScanLeaf]() {
        return recover_by_terminal_delete_scan(ctx, matchRecoverableTerminal,
                                               commitDeleteScanLeaf, facts,
                                               profile.shape);
      });
}

bool recover_terminal_after_strict_miss(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart, const char *matchedEnd,
    const TerminalRecoveryFacts &effectiveFacts) {
  if (effectiveFacts.localRecoveryBlocked) {
    return false;
  }
  if (!ctx.canEdit()) {
    if (!profile.isKeyword) {
      PEGIUM_RECOVERY_TRACE("[terminal rule] no-edit-window fail ",
                            profile.traceLabel,
                            " offset=", ctx.cursorOffset());
    }
    return false;
  }
  const auto fuzzyCandidates = collect_terminal_replace_candidates(
      ctx, profile, cursorStart, effectiveFacts);
  if (run_terminal_local_recovery(ctx, profile, cursorStart, matchedEnd,
                                  fuzzyCandidates, effectiveFacts)) {
    return true;
  }
  if (profile.isKeyword) {
    PEGIUM_RECOVERY_TRACE("[literal rule] fail '", profile.traceLabel,
                          "' at ", ctx.cursorOffset());
  } else {
    PEGIUM_RECOVERY_TRACE("[terminal rule] fail ", profile.traceLabel,
                          " offset=", ctx.cursorOffset());
  }
  return false;
}

namespace {

/// Nearby delete-scan probe over the profile's strict matcher.
[[nodiscard]] bool
probe_profile_delete_scan(RecoveryContext &ctx,
                          const TerminalRecoveryProfile &profile,
                          const TerminalRecoveryFacts &localRecoveryFacts) {
  return probe_nearby_delete_scan_match(
      ctx,
      [&profile](const char *scanCursor) noexcept {
        return profile_match(profile, scanCursor);
      },
      localRecoveryFacts, profile.shape);
}

/// `TerminalRule`'s fuzzy probe leg: iterates literal-replace fuzzy
/// candidates near the cursor and returns true as soon as
/// `predicate(candidate)` accepts one. Returns false when no literal
/// metadata is available, a prior edit disqualifies the Replace leg, or no
/// candidate matches.
[[nodiscard]] bool terminal_rule_fuzzy_candidate_match(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile,
    const char *cursorStart,
    bool (*predicate)(const LiteralFuzzyCandidate &)) {
  if (!profile.hasLiteralText ||
      (ctx.hasHadEdits() &&
       !allows_fuzzy_replace_after_prior_edits(profile.shape))) {
    return false;
  }
  const auto replaceCandidates = collect_literal_replace_candidates(
      ctx, cursorStart, profile.literalValue, profile.caseSensitive,
      profile.shape);
  return std::ranges::any_of(replaceCandidates, predicate);
}

} // namespace

bool literal_probe_recoverable(RecoveryContext &ctx,
                               const TerminalRecoveryProfile &profile) {
  const auto localRecoveryFacts = local_skip_terminal_recovery_facts(ctx);
  const char *const cursorStart = ctx.cursor();
  if (const char *const matchedEnd = profile_match(profile, cursorStart);
      matchedEnd != nullptr &&
      has_immediate_visible_continuation(ctx, matchedEnd)) {
    return true;
  }
  if (ctx.hasHadEdits() &&
      !allows_fuzzy_replace_after_prior_edits(profile.shape)) {
    return false;
  }
  const auto fuzzyCandidates = collect_literal_replace_candidates(
      ctx, cursorStart, profile.literalValue, profile.caseSensitive,
      profile.shape);
  if (!fuzzyCandidates.empty()) {
    return true;
  }
  return probe_profile_delete_scan(ctx, profile, localRecoveryFacts);
}

bool literal_probe_recoverable_at_entry(RecoveryContext &ctx,
                                        const TerminalRecoveryProfile &profile) {
  const auto localRecoveryFacts = local_skip_terminal_recovery_facts(ctx);
  if (profile.shape.allowsInsert() &&
      allows_terminal_entry_probe(ctx, profile.shape)) {
    return true;
  }
  if (ctx.hasHadEdits() &&
      !allows_fuzzy_replace_after_prior_edits(profile.shape)) {
    return false;
  }
  const char *const cursorStart = ctx.cursor();
  if (const char *const matchedEnd = profile_match(profile, cursorStart);
      matchedEnd != nullptr &&
      has_immediate_visible_continuation(ctx, matchedEnd)) {
    return true;
  }
  const auto fuzzyCandidates = collect_literal_replace_candidates(
      ctx, cursorStart, profile.literalValue, profile.caseSensitive,
      profile.shape);
  if (std::ranges::any_of(
          fuzzyCandidates, [](const auto &candidate) constexpr noexcept {
            return allows_entry_probe_fuzzy_candidate(candidate);
          })) {
    return true;
  }
  return probe_profile_delete_scan(ctx, profile, localRecoveryFacts);
}

bool literal_probe_recoverable_at_entry_consumes_visible(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile) {
  const auto localRecoveryFacts = local_skip_terminal_recovery_facts(ctx);
  if (ctx.hasHadEdits() &&
      !allows_fuzzy_replace_after_prior_edits(profile.shape)) {
    return false;
  }
  const char *const cursorStart = ctx.cursor();
  const char *const matchedEnd = profile_match(profile, cursorStart);
  if (matchedEnd != nullptr &&
      !profile_word_boundary_violation(ctx, profile, matchedEnd) &&
      matchedEnd > cursorStart) {
    return true;
  }
  const auto fuzzyCandidates = collect_literal_replace_candidates(
      ctx, cursorStart, profile.literalValue, profile.caseSensitive,
      profile.shape);
  if (std::ranges::any_of(
          fuzzyCandidates, [](const auto &candidate) constexpr noexcept {
            return candidate.consumed > 0u &&
                   allows_entry_probe_fuzzy_candidate(candidate);
          })) {
    return true;
  }
  return probe_profile_delete_scan(ctx, profile, localRecoveryFacts);
}

bool terminal_rule_probe_recoverable(RecoveryContext &ctx,
                                     const TerminalRecoveryProfile &profile) {
  if (terminal_rule_probe_recoverable_at_entry(ctx, profile)) {
    return true;
  }
  return terminal_rule_fuzzy_candidate_match(
             ctx, profile, ctx.cursor(),
             [](const LiteralFuzzyCandidate &) noexcept { return true; }) ||
         probe_profile_delete_scan(ctx, profile,
                                   local_skip_terminal_recovery_facts(ctx));
}

bool terminal_rule_probe_recoverable_at_entry(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile) {
  return terminal_rule_fuzzy_candidate_match(
             ctx, profile, ctx.cursor(),
             allows_entry_probe_fuzzy_candidate) ||
         allows_terminal_entry_probe(ctx, profile.shape) ||
         probe_profile_delete_scan(ctx, profile,
                                   local_skip_terminal_recovery_facts(ctx));
}

bool terminal_rule_probe_recoverable_at_entry_consumes_visible(
    RecoveryContext &ctx, const TerminalRecoveryProfile &profile) {
  const char *const cursorStart = ctx.cursor();
  if (const char *const matchedEnd = profile_match(profile, cursorStart);
      matchedEnd != nullptr && matchedEnd > cursorStart) {
    return true;
  }
  return terminal_rule_fuzzy_candidate_match(
             ctx, profile, cursorStart,
             [](const LiteralFuzzyCandidate &candidate) noexcept {
               return candidate.consumed > 0u &&
                      allows_entry_probe_fuzzy_candidate(candidate);
             }) ||
         probe_profile_delete_scan(ctx, profile,
                                   local_skip_terminal_recovery_facts(ctx));
}

} // namespace pegium::parser::detail
