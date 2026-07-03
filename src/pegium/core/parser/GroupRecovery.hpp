#pragma once

/// Out-of-class definitions of `Group`'s recovery-mode members declared in
/// Group.hpp: the public `probeRecoverable`/`probeRecoverableAtEntry`/
/// `probeRecoverableAtEntryConsumesVisible` accessors,
/// `element_is_terminal_like_for_missing_insert`, `note_sequence_transition`,
/// the 4-transition `parse_elements_recovery` driver, `sequence_ops` (the
/// per-visit type-erased ops-table builder consumed by
/// SequenceRecoveryEngine.cpp), `with_current_follow_probe`, and the
/// index-recursive probe walkers that back the driver
/// (`nullable_element_allows_recovery_skip`, `probe_recoverable_elements`,
/// `suffix_starts_without_edits`, `probe_recoverable_entry_elements`,
/// `probe_recoverable_entry_consumes_visible_elements`,
/// `recoverable_follow_consumes_visible_probe`, `analyze_suffix_entry_facts`,
/// `probe_recoverable_suffix_after_missing_current`,
/// `probe_nullable_current_looks_started`, `previous_element_is_terminalish`,
/// `previous_nullable_sibling_owns_cursor`,
/// `nullable_current_consumed_visible_since`,
/// `allows_synthetic_terminal_insert`, `suffix_after_is_fully_nullable`,
/// `first_required_tail_element`, `tail_supports_single_terminal_recovery`,
/// `parse_clean_tail_without_edits`). A translation unit that drives a Group
/// through recovery mode must include this header in addition to
/// Group.hpp.

#include <pegium/core/parser/Group.hpp>

namespace pegium::parser {

template <Expression... Elements>
bool Group<Elements...>::probeRecoverable(RecoveryContext &ctx) const {
  return probe_recoverable_elements<0>(ctx);
}

template <Expression... Elements>
bool Group<Elements...>::probeRecoverableAtEntry(RecoveryContext &ctx) const {
  return probe_recoverable_entry_elements<0>(ctx);
}

template <Expression... Elements>
bool Group<Elements...>::probeRecoverableAtEntryConsumesVisible(
    RecoveryContext &ctx) const {
  return probe_recoverable_entry_consumes_visible_elements<0>(ctx);
}

template <Expression... Elements>
constexpr bool Group<Elements...>::element_is_terminal_like_for_missing_insert(
    const grammar::AbstractElement &element) noexcept {
  return element.getKind() == ElementKind::Literal ||
         element.getKind() == ElementKind::TerminalRule;
}

template <Expression... Elements>
void Group<Elements...>::note_sequence_transition(
    SequenceElementTransition transition) noexcept {
  switch (transition) {
  case SequenceElementTransition::InsertMissingCurrent:
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::GroupInsertMissingCurrent);
    break;
  case SequenceElementTransition::SkipNullable:
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::GroupSkipNullable);
    break;
  case SequenceElementTransition::KeepCurrent:
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::GroupKeepCurrent);
    break;
  case SequenceElementTransition::RepairTail:
    PEGIUM_STEP_TRACE_INC(detail::StepCounter::GroupRepairTail);
    break;
  }
}

template <Expression... Elements>
template <RecoveryParseModeContext Context, std::size_t I>
bool Group<Elements...>::parse_elements_recovery(
    Context &ctx, const char *cursorBeforeSkip,
    bool previousNullableSiblingConsumedVisible) const {
  if (ctx.recoveryDescentInactive()) {
    // Track lastVisibleCursorOffset across the strict parse so we can
    // forward `currentNullableConsumedVisible` to element I+1: if element
    // I+1's recovery later consults `previous_nullable_sibling_owns_cursor`,
    // it needs to know whether element I (a nullable sibling) consumed
    // visible input. The full recovery branch below already does this via
    // `nullable_current_consumed_visible_since`; the fast-path must too.
    using CurrentElement =
        std::remove_cvref_t<decltype(std::get<I>(elements))>;
    const TextOffset visibleBefore =
        CurrentElement::nullable ? ctx.lastVisibleCursorOffset() : 0u;
    if (TrackedParseContext &strictCtx = ctx;
        !parse(std::get<I>(elements), strictCtx)) {
      return false;
    }
    const bool currentNullableConsumedVisible =
        CurrentElement::nullable &&
        ctx.lastVisibleCursorOffset() > visibleBefore;
    return parse_elements<Context, I + 1>(ctx,
                                          currentNullableConsumedVisible);
  }
  const auto ops = this->template sequence_ops<I>();
  const bool nullableCurrentLooksStarted =
      this->template probe_nullable_current_looks_started<I>(ctx);
  const bool previousNullableSiblingOwnsCursor =
      this->template previous_nullable_sibling_owns_cursor<I>(
          ctx, previousNullableSiblingConsumedVisible);
  // Transition: InsertMissingCurrent (early-exit replay path).
  if (detail::sequence_select_missing_element_insert_replay(ctx, ops)) {
    note_sequence_transition(SequenceElementTransition::InsertMissingCurrent);
    bool matchedCleanTail = false;
    return detail::sequence_replay_insert_missing_element_attempt(
        ctx, ops, matchedCleanTail);
  }
  const auto checkpoint = ctx.mark();
  const auto &current = std::get<I>(elements);
  const auto terminalRecoveryState =
      (current.getKind() == ElementKind::Literal ||
       current.getKind() == ElementKind::TerminalRule)
          ? detail::sequence_build_terminal_recovery_state(
                ctx, ops, checkpoint, cursorBeforeSkip,
                previousNullableSiblingOwnsCursor)
          : detail::TerminalRecoveryState{};
  bool strictSuffixStartsAtEntry = false;
  if constexpr (std::remove_cvref_t<
                    decltype(std::get<I>(elements))>::nullable) {
    strictSuffixStartsAtEntry =
        this->template suffix_starts_without_edits<I + 1>(ctx);
  }
  const bool visibleNullableRepairCompetesWithStrictSuffix =
      detail::sequence_nullable_current_visible_repair_competes_with_strict_suffix(
          ctx, ops, strictSuffixStartsAtEntry);
  if (visibleNullableRepairCompetesWithStrictSuffix) {
    ctx.rewind(checkpoint);
    if (detail::sequence_parse_competing_nullable_current_repair(
            ctx, ops, terminalRecoveryState.facts)) {
      const bool currentCommittedProgress =
          ctx.committedProgressSince(checkpoint);
      if (currentCommittedProgress) {
        const bool currentNullableConsumedVisible =
            this->template nullable_current_consumed_visible_since<I>(
                ctx, checkpoint);
        if (parse_elements<Context, I + 1>(ctx,
                                           currentNullableConsumedVisible)) {
          return true;
        }
      }
    }
    ctx.rewind(checkpoint);
  }
  // Transition: SkipNullable.
  if (!visibleNullableRepairCompetesWithStrictSuffix &&
      detail::sequence_try_skip_nullable_transition(
          ctx, ops, nullableCurrentLooksStarted, strictSuffixStartsAtEntry)) {
    note_sequence_transition(SequenceElementTransition::SkipNullable);
    return true;
  }
  // Transition: KeepCurrent (no-edit) when the strict suffix is
  // already visible at entry, otherwise the regular strict attempt
  // that may trigger RepairCurrent on failure.
  if (strictSuffixStartsAtEntry) {
    note_sequence_transition(SequenceElementTransition::KeepCurrent);
    auto noEditGuard = ctx.withEditTrackingDisabled();
    (void)noEditGuard;
    if (!detail::sequence_parse_current_element(
            ctx, ops, terminalRecoveryState.facts)) {
      return detail::sequence_recover_after_current_parse_failure(
          ctx, ops, checkpoint, terminalRecoveryState,
          nullableCurrentLooksStarted);
    }
  } else if (!detail::sequence_parse_current_element(
                  ctx, ops, terminalRecoveryState.facts)) {
    return detail::sequence_recover_after_current_parse_failure(
        ctx, ops, checkpoint, terminalRecoveryState,
        nullableCurrentLooksStarted);
  }
  const auto checkpointAfterCurrent = ctx.mark();
  const bool currentCommittedProgress =
      ctx.committedProgressSince(checkpoint);
  const bool currentNullableConsumedVisible =
      this->template nullable_current_consumed_visible_since<I>(
          ctx, checkpoint);
  if (parse_elements<Context, I + 1>(
          ctx, currentNullableConsumedVisible)) {
    return true;
  }
  // Transition: RepairTail — admissible iff Current actually committed
  // something (cursor advanced or edits accumulated). A nullable
  // Current that matched ε falls through here without admission so
  // that the equivalent SkipNullable path (already tried earlier) is
  // not wastefully retried.
  if (!currentCommittedProgress) {
    return false;
  }
  note_sequence_transition(SequenceElementTransition::RepairTail);
  return detail::sequence_recover_after_tail_parse_failure(
      ctx, ops, checkpoint, checkpointAfterCurrent,
      terminalRecoveryState.facts);
}

template <Expression... Elements>
template <std::size_t I>
constexpr bool
Group<Elements...>::nullable_element_allows_recovery_skip() const noexcept {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    // Predicates (NotPredicate / AndPredicate) are nullable — they
    // consume nothing — but they are guards, not optional content. The
    // recovery skip-nullable transition would treat them as ε and
    // bypass them entirely, which silently drops the lookahead check.
    // For example, `many(!"K"_kw + Item)` would let `Item` swallow `K`
    // because the surrounding skip-nullable path never evaluates the
    // NotPredicate. Same hazard applies to AndPredicate guards.
    return current.getKind() != ElementKind::Create &&
           current.getKind() != ElementKind::NotPredicate &&
           current.getKind() != ElementKind::AndPredicate;
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::probe_recoverable_elements(
    RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    if (attempt_fast_probe(ctx, current) ||
        probe_locally_recoverable(current, ctx)) {
      return true;
    }
    if (current.getKind() == ElementKind::AndPredicate) {
      return false;
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return probe_recoverable_elements<I + 1>(ctx);
    } else {
      return probe_recoverable_suffix_after_missing_current<I + 1>(ctx);
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::suffix_starts_without_edits(
    RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    if (attempt_fast_probe(ctx, current)) {
      return true;
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return suffix_starts_without_edits<I + 1>(ctx);
    } else {
      return false;
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::probe_recoverable_entry_elements(
    RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    if (probe_recoverable_at_entry(current, ctx)) {
      return true;
    }
    if (current.getKind() == ElementKind::AndPredicate) {
      return false;
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return probe_recoverable_entry_elements<I + 1>(ctx);
    } else {
      return false;
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::probe_recoverable_entry_consumes_visible_elements(
    RecoveryContext &ctx, bool allowSyntheticLeadingTerminalContinuation,
    RecoveryContext::FollowProbeFn outerRecoverableConsumesVisible,
    const void *outerRecoverableConsumesVisibleData) const {
  if constexpr (I == sizeof...(Elements)) {
    return outerRecoverableConsumesVisible != nullptr &&
           outerRecoverableConsumesVisible(
               ctx, outerRecoverableConsumesVisibleData);
  } else {
    const auto &current = std::get<I>(elements);
    if (attempt_fast_probe(ctx, current) ||
        probe_recoverable_at_entry_consumes_visible(current, ctx)) {
      return true;
    }
    // Synthetic-leading-terminal insert probe. The continuation variant
    // additionally allows a sequence-literal insert; allows_synthetic_terminal_insert
    // with allowSequenceLiteralInsert=false is the strict subset, so the two
    // former blocks (scoped+strict-insert / continuation+literal-insert)
    // collapse to one guarded by the gate flag + the matching insert mode.
    if ((allowSyntheticLeadingTerminalContinuation ||
         ctx.allowsScopedLeadingTerminalInsertRecovery()) &&
        element_is_terminal_like_for_missing_insert(current) &&
        allows_synthetic_terminal_insert(
            ctx, current,
            /*allowSequenceLiteralInsert=*/
            allowSyntheticLeadingTerminalContinuation) &&
        detail::cursor_starts_visible_source(ctx)) {
      const bool suffixConsumesVisible =
          this->template suffix_starts_without_edits<I + 1>(ctx) ||
          (I + 1 == sizeof...(Elements) &&
           outerRecoverableConsumesVisible != nullptr &&
           outerRecoverableConsumesVisible(
               ctx, outerRecoverableConsumesVisibleData));
      if (suffixConsumesVisible) {
        return true;
      }
    }
    if (current.getKind() == ElementKind::AndPredicate) {
      return false;
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return probe_recoverable_entry_consumes_visible_elements<I + 1>(
          ctx, allowSyntheticLeadingTerminalContinuation,
          outerRecoverableConsumesVisible,
          outerRecoverableConsumesVisibleData);
    } else {
      return false;
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::recoverable_follow_consumes_visible_probe(
    RecoveryContext &ctx, const void *data) {
  const auto &probe =
      *static_cast<const RecoverableFollowConsumesVisibleProbe *>(data);
  return probe.self
      ->template probe_recoverable_entry_consumes_visible_elements<I>(
          ctx, /*allowSyntheticLeadingTerminalContinuation=*/true,
          probe.outerRecoverableConsumesVisible,
          probe.outerRecoverableConsumesVisibleData);
}

template <Expression... Elements>
template <std::size_t I>
detail::SequenceSuffixEntryFacts
Group<Elements...>::analyze_suffix_entry_facts(RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return {};
  } else {
    const auto &current = std::get<I>(elements);
    const bool strictStartsAtCurrentCursor =
        attempt_fast_probe(ctx, current);
    const bool recoverableStartsAtCurrentCursor =
        probe_recoverable_at_entry(current, ctx);
    if (strictStartsAtCurrentCursor || recoverableStartsAtCurrentCursor) {
      return {
          .strictStartsAtCurrentCursor = strictStartsAtCurrentCursor,
          .recoverableStartsAtCurrentCursor =
              recoverableStartsAtCurrentCursor,
      };
    }
    if (current.getKind() == ElementKind::AndPredicate) {
      return {};
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return analyze_suffix_entry_facts<I + 1>(ctx);
    } else {
      return {};
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::probe_recoverable_suffix_after_missing_current(
    RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    if (attempt_fast_probe(ctx, current) ||
        probe_recoverable_at_entry(current, ctx) ||
        probe_locally_recoverable(current, ctx)) {
      return true;
    }
    if constexpr (std::remove_cvref_t<decltype(current)>::nullable) {
      return probe_recoverable_suffix_after_missing_current<I + 1>(ctx);
    } else {
      return false;
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::probe_nullable_current_looks_started(
    RecoveryContext &ctx) const {
  if constexpr (!std::remove_cvref_t<
                    decltype(std::get<I>(elements))>::nullable) {
    (void)ctx;
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    if (current.getKind() == ElementKind::AndPredicate ||
        current.getKind() == ElementKind::NotPredicate) {
      return false;
    }
    detail::ProbeRestoreScope guard{ctx};
    return attempt_fast_probe(ctx, current);
  }
}

template <Expression... Elements>
template <std::size_t I>
constexpr bool
Group<Elements...>::previous_element_is_terminalish() const noexcept {
  if constexpr (I == 0) {
    return false;
  } else {
    const auto &previous = std::get<I - 1>(elements);
    return previous.getKind() == ElementKind::Literal ||
           previous.getKind() == ElementKind::TerminalRule;
  }
}

template <Expression... Elements>
template <std::size_t I>
detail::SequenceElementOps Group<Elements...>::sequence_ops() const noexcept {
  const auto &current = std::get<I>(elements);
  using Current = std::remove_cvref_t<decltype(std::get<I>(elements))>;
  return detail::SequenceElementOps{
      .self = this,
      .current = std::addressof(current),
      .currentKind = current.getKind(),
      .currentNullable = Current::nullable,
      .currentAllowsRecoverySkip =
          this->template nullable_element_allows_recovery_skip<I>(),
      .previousIsTerminalish =
          this->template previous_element_is_terminalish<I>(),
      .suffixFullyNullable = suffix_after_is_fully_nullable<I + 1>(),
      .isFirstElement = I == 0,
      .parseCurrent =
          +[](const void *self, RecoveryContext &ctx) {
            return parse(
                std::get<I>(static_cast<const Group *>(self)->elements),
                ctx);
          },
      .parseCurrentTerminalRecovery =
          +[](const void *self, RecoveryContext &ctx,
              const detail::TerminalRecoveryFacts &facts) {
            const auto &element =
                std::get<I>(static_cast<const Group *>(self)->elements);
            if constexpr (requires {
                            element.parse_terminal_recovery_impl(ctx, facts);
                          }) {
              return element.parse_terminal_recovery_impl(ctx, facts);
            } else {
              return parse(element, ctx);
            }
          },
      .parseCurrentWithoutSideEffects =
          +[](const void *self, RecoveryContext &ctx) {
            return detail::attempt_parse_without_side_effects(
                ctx,
                std::get<I>(static_cast<const Group *>(self)->elements));
          },
      .fastProbeCurrent =
          +[](const void *self, RecoveryContext &ctx) {
            return attempt_fast_probe(
                ctx,
                std::get<I>(static_cast<const Group *>(self)->elements));
          },
      .probeCurrentAtEntry =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_recoverable_at_entry(
                std::get<I>(static_cast<const Group *>(self)->elements),
                ctx);
          },
      .probeCurrentLocallyRecoverable =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_locally_recoverable(
                std::get<I>(static_cast<const Group *>(self)->elements),
                ctx);
          },
      .probeCurrentAtEntryConsumesVisible =
          +[](const void *self, RecoveryContext &ctx) {
            return probe_recoverable_at_entry_consumes_visible(
                std::get<I>(static_cast<const Group *>(self)->elements),
                ctx);
          },
      .allowsSyntheticInsertCurrent =
          +[](const void *self, RecoveryContext &ctx,
              bool allowSequenceLiteralInsert) {
            return allows_synthetic_terminal_insert(
                ctx,
                std::get<I>(static_cast<const Group *>(self)->elements),
                allowSequenceLiteralInsert);
          },
      .currentInnerElement =
          +[](const void *self) noexcept -> const grammar::AbstractElement * {
            const auto &element =
                std::get<I>(static_cast<const Group *>(self)->elements);
            if constexpr (requires { element.getElement(); }) {
              return element.getElement();
            } else {
              return nullptr;
            }
          },
      .analyzeSuffixEntryFacts =
          +[](const void *self, RecoveryContext &ctx) {
            return static_cast<const Group *>(self)
                ->template analyze_suffix_entry_facts<I + 1>(ctx);
          },
      .suffixProbeRecoverableEntry =
          +[](const void *self, RecoveryContext &ctx) {
            return static_cast<const Group *>(self)
                ->template probe_recoverable_entry_elements<I + 1>(ctx);
          },
      .parseTail =
          +[](const void *self, RecoveryContext &ctx,
              bool currentNullableConsumedVisible) {
            return static_cast<const Group *>(self)
                ->template parse_elements<RecoveryContext, I + 1>(
                    ctx, currentNullableConsumedVisible);
          },
      .parseCleanTailNoEdits =
          +[](const void *self, RecoveryContext &ctx) {
            return static_cast<const Group *>(self)
                ->template parse_clean_tail_without_edits<I + 1>(ctx);
          },
      .tailSupportsSingleTerminalRecovery =
          +[](const void *self) {
            return static_cast<const Group *>(self)
                ->template tail_supports_single_terminal_recovery<I + 1>();
          },
      .firstRequiredTailElement =
          +[](const void *self) -> const grammar::AbstractElement * {
            return static_cast<const Group *>(self)
                ->template first_required_tail_element<I + 1>();
          },
      .withCurrentFollow =
          +[](const void *self, RecoveryContext &ctx,
              pegium::utils::function_ref<bool()> body) {
            return static_cast<const Group *>(self)
                ->template with_current_follow_probe<I>(ctx, body);
          },
  };
}

template <Expression... Elements>
template <std::size_t I, typename Fn>
bool Group<Elements...>::with_current_follow_probe(RecoveryContext &ctx,
                                                    Fn &&fn) const {
  // Install the current element's follow only while the current element
  // itself is parsed or probed. Keeping this guard alive while parsing the
  // recursive tail would make a last child pass through to the previous
  // sibling's local follow instead of the enclosing follow.
  if constexpr (I + 1 == sizeof...(Elements)) {
    // Last element: install NO local follow. The previous siblings' follow
    // guards are RAII-scoped per element and already torn down, so the live
    // probe is the enclosing scope's follow — exactly the semantic follow the
    // last element must see. (A pass-through guard used to forward to this
    // same outer probe; installing nothing is observationally identical on
    // probe results and avoids fingerprinting a transient guard stack
    // address into the choice-recover cache.)
    return std::forward<Fn>(fn)();
  } else {
    constexpr auto strictProbeFn =
        +[](RecoveryContext &c, const void *data) -> bool {
      const auto *self = static_cast<const Group *>(data);
      return self->template suffix_starts_without_edits<I + 1>(c);
    };
    constexpr auto recoverableProbeFn =
        +[](RecoveryContext &c, const void *data) -> bool {
      const auto *self = static_cast<const Group *>(data);
      return self->template probe_recoverable_entry_elements<I + 1>(c);
    };
    constexpr auto recoverableConsumesVisibleProbeFn =
        &Group::template recoverable_follow_consumes_visible_probe<I + 1>;
    RecoverableFollowConsumesVisibleProbe recoverableConsumesVisibleProbe{
        .self = this,
        .outerRecoverableConsumesVisible =
            ctx._followProbes.recoverableConsumesVisible.fn,
        .outerRecoverableConsumesVisibleData =
            ctx._followProbes.recoverableConsumesVisible.data,
    };
    auto followGuard = ctx.withFollowProbe(
        strictProbeFn, this, recoverableProbeFn, this,
        recoverableConsumesVisibleProbeFn, &recoverableConsumesVisibleProbe,
        /*stableOwner=*/this);
    (void)followGuard;
    return std::forward<Fn>(fn)();
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::previous_nullable_sibling_owns_cursor(
    RecoveryContext &ctx,
    bool previousNullableSiblingConsumedVisible) const {
  if constexpr (I == 0) {
    (void)ctx;
    return false;
  } else {
    const auto &previous = std::get<I - 1>(elements);
    using Previous = std::remove_cvref_t<decltype(previous)>;
    if constexpr (!Previous::nullable) {
      (void)ctx;
      (void)previousNullableSiblingConsumedVisible;
      return false;
    } else {
      detail::ProbeRestoreScope guard{ctx};
      return this->template with_current_follow_probe<I - 1>(ctx, [&]() {
        auto deleteObservationGuard =
            ctx.withEditPermissions(ctx.editPolicy.allowInsert, true);
        (void)deleteObservationGuard;
        if (previousNullableSiblingConsumedVisible &&
            !detail::nullable_sibling_allows_recoverable_continuation_v<
                Previous>) {
          return false;
        }
        bool previousStrictProgress = false;
        {
          detail::ProbeRestoreScope strictGuard{ctx};
          const TextOffset previousStrictStartOffset = ctx.cursorOffset();
          previousStrictProgress =
              attempt_parse_no_edits(ctx, previous) &&
              ctx.cursorOffset() > previousStrictStartOffset;
        }
        if (previousStrictProgress) {
          return true;
        }
        if (const auto &current = std::get<I>(elements);
            element_is_terminal_like_for_missing_insert(current) &&
            allows_synthetic_terminal_insert(
                ctx, current, /*allowSequenceLiteralInsert=*/true) &&
            detail::cursor_starts_visible_source(ctx)) {
          const auto suffixEntryFacts =
              this->template analyze_suffix_entry_facts<I + 1>(ctx);
          if (suffix_after_is_fully_nullable<I + 1>() ||
              suffixEntryFacts.strictStartsAtCurrentCursor ||
              suffixEntryFacts.recoverableStartsAtCurrentCursor) {
            return false;
          }
        }
        if (attempt_fast_probe(ctx, previous)) {
          return true;
        }
        return probe_recoverable_at_entry_consumes_visible(previous, ctx);
      });
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::nullable_current_consumed_visible_since(
    const RecoveryContext &ctx,
    const RecoveryContext::Checkpoint &checkpoint) const noexcept {
  if constexpr (!std::remove_cvref_t<
                    decltype(std::get<I>(elements))>::nullable) {
    (void)ctx;
    (void)checkpoint;
    return false;
  } else {
    return ctx.lastVisibleCursorOffset() >
           static_cast<TextOffset>(
               checkpoint.parseCheckpoint.parseCheckpoint.lastVisibleCursor -
               ctx.begin);
  }
}

template <Expression... Elements>
template <typename Current>
constexpr bool Group<Elements...>::allows_synthetic_terminal_insert(
    RecoveryContext &ctx, const Current &current,
    bool allowSequenceLiteralInsert) noexcept {
  const auto allowsEofLiteralInsert =
      [&ctx](detail::TerminalShape shape) constexpr noexcept {
        return ctx.cursor() >= ctx.end && shape.hasCanonicalText;
      };
  const auto allowsSequenceLiteralInsert =
      [allowSequenceLiteralInsert](
          detail::TerminalShape shape) constexpr noexcept {
        return allowSequenceLiteralInsert && shape.hasCanonicalText;
      };
  const auto allowsScopedCompactInsert =
      [&ctx](detail::TerminalShape shape) constexpr noexcept {
        return ctx.allowsScopedLeadingTerminalInsertRecovery() &&
               shape.hasCanonicalText &&
               shape.canonicalTextLength <=
                   detail::kMaxScopedSyntheticInsertCanonicalTextLength;
      };
  using CurrentType = std::remove_cvref_t<Current>;
  if constexpr (requires { CurrentType::terminalShape; }) {
    (void)current;
    return CurrentType::terminalShape.allowsInsert() ||
           allowsSequenceLiteralInsert(CurrentType::terminalShape) ||
           allowsEofLiteralInsert(CurrentType::terminalShape) ||
           allowsScopedCompactInsert(CurrentType::terminalShape);
  } else if constexpr (requires {
                         current._literalRecoveryMetadata;
                         current._terminalShape;
                       }) {
    if (current._literalRecoveryMetadata.has_value() &&
        !current._terminalShape.allowsInsert() &&
        !allowsSequenceLiteralInsert(current._terminalShape) &&
        !allowsEofLiteralInsert(current._terminalShape) &&
        !allowsScopedCompactInsert(current._terminalShape)) {
      return false;
    }
    return detail::allows_terminal_rule_insert(ctx,
                                               current._terminalShape);
  }
  return true;
}

template <Expression... Elements>
template <std::size_t I>
constexpr bool Group<Elements...>::suffix_after_is_fully_nullable() noexcept {
  if constexpr (I >= sizeof...(Elements)) {
    return true;
  } else {
    using Current = std::remove_cvref_t<decltype(std::get<I>(
        std::declval<const std::tuple<Elements...> &>()))>;
    if constexpr (!Current::nullable) {
      return false;
    } else {
      return suffix_after_is_fully_nullable<I + 1>();
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
constexpr const grammar::AbstractElement *
Group<Elements...>::first_required_tail_element() const noexcept {
  if constexpr (I >= sizeof...(Elements)) {
    return nullptr;
  } else {
    const auto &current = std::get<I>(elements);
    using Current = std::remove_cvref_t<decltype(current)>;
    if constexpr (!Current::nullable) {
      return std::addressof(current);
    } else {
      return first_required_tail_element<I + 1>();
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
constexpr bool
Group<Elements...>::tail_supports_single_terminal_recovery() const noexcept {
  if constexpr (I >= sizeof...(Elements)) {
    return false;
  } else {
    const auto &current = std::get<I>(elements);
    using Current = std::remove_cvref_t<decltype(current)>;
    if constexpr (!Current::nullable) {
      return element_is_terminal_like_for_missing_insert(current) &&
             suffix_after_is_fully_nullable<I + 1>();
    } else {
      return tail_supports_single_terminal_recovery<I + 1>();
    }
  }
}

template <Expression... Elements>
template <std::size_t I>
bool Group<Elements...>::parse_clean_tail_without_edits(
    RecoveryContext &ctx) const {
  if constexpr (I == sizeof...(Elements)) {
    return true;
  } else {
    return attempt_parse_no_edits(ctx, std::get<I>(elements)) &&
           this->template parse_clean_tail_without_edits<I + 1>(ctx);
  }
}

} // namespace pegium::parser
