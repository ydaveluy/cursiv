#pragma once

/// Lightweight step counters used by parser instrumentation hooks.

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace pegium::parser::detail {

enum class StepCounter : std::size_t {

  ParseContextMark,
  ParseContextRewind,
  ParseContextEnter,
  ParseContextExit,
  ParseContextLeaf,
  ParseContextInsert,
  ParseContextDelete,
  ParseContextReplace,
  ParsePhaseRuns,
  RecoveryPhaseRuns,
  ChoiceRecoverCalls,
  ChoiceStrictPasses,
  ChoiceEditablePasses,
  UnorderedRecoverCalls,
  UnorderedStrictPasses,
  UnorderedEditablePasses,
  RepetitionRecoverCalls,
  RepetitionFastAttempts,
  RepetitionFastSuccess,
  RepetitionFastFailures,
  /// The four labeled transitions `Group::parse_elements_recovery` can take
  /// at a sequence-element boundary (see `Group::SequenceElementTransition`).
  /// Instrumentation only, so a differential fuzzer can report which fired.
  GroupInsertMissingCurrent,
  GroupSkipNullable,
  GroupKeepCurrent,
  GroupRepairTail,
  /// Each `minimize_recovery_edits` invocation increments this
  /// counter. `MinimizeRecoveryEditsRuns` measures *how often* the
  /// global compensation runs; `MinimizeRecoveryEditsDropped` totals
  /// the number of edits the compensation actually removed across
  /// all runs (the value the compensation provides). The ratio
  /// answers "is the global pruning pass still earning its keep, or
  /// is the local enumeration tight enough to retire it?".
  MinimizeRecoveryEditsRuns,
  MinimizeRecoveryEditsDropped,
  /// The minimal-edit strategy probe runs when the primary attempt
  /// produced multi-edit output; `MinimalEditProbeRuns` counts every
  /// probe invocation, `MinimalEditProbeWins` counts the cases where
  /// the probe's single-edit attempt actually beat the primary on
  /// the shared `RecoveryKey` ranker.
  MinimalEditProbeRuns,
  MinimalEditProbeWins,
  /// The root-prefix delete-retry global compensation runs when the
  /// primary attempt did not match the entry rule (or matched only
  /// a weak zero-prefix). `RootPrefixRetryRuns` counts every
  /// invocation; `RootPrefixRetryWins` counts the cases where the
  /// retry actually unblocked an entry-rule match.
  RootPrefixRetryRuns,
  RootPrefixRetryWins,
  Count
};

#if defined(PEGIUM_ENABLE_STEP_TRACE)

#define PEGIUM_STEP_TRACE_INC(counter, ...)                                     \
  ::pegium::parser::detail::stepTraceInc(counter, ##__VA_ARGS__)
#define PEGIUM_STEP_TRACE_RESET() ::pegium::parser::detail::stepTraceReset()
#define PEGIUM_STEP_TRACE_DUMP_SUMMARY(...)                                     \
  ::pegium::parser::detail::stepTraceDumpSummary(__VA_ARGS__)

inline constexpr std::size_t
step_trace_index(StepCounter counter) noexcept {
  return static_cast<std::size_t>(counter);
}

inline const char *step_trace_name(StepCounter counter) noexcept {
  switch (counter) {
  case StepCounter::ParseContextMark:
    return "ParseContextMark";
  case StepCounter::ParseContextRewind:
    return "ParseContextRewind";
  case StepCounter::ParseContextEnter:
    return "ParseContextEnter";
  case StepCounter::ParseContextExit:
    return "ParseContextExit";
  case StepCounter::ParseContextLeaf:
    return "ParseContextLeaf";
  case StepCounter::ParseContextInsert:
    return "ParseContextInsert";
  case StepCounter::ParseContextDelete:
    return "ParseContextDelete";
  case StepCounter::ParseContextReplace:
    return "ParseContextReplace";
  case StepCounter::ParsePhaseRuns:
    return "ParsePhaseRuns";
  case StepCounter::RecoveryPhaseRuns:
    return "RecoveryPhaseRuns";
  case StepCounter::ChoiceRecoverCalls:
    return "ChoiceRecoverCalls";
  case StepCounter::ChoiceStrictPasses:
    return "ChoiceStrictPasses";
  case StepCounter::ChoiceEditablePasses:
    return "ChoiceEditablePasses";
  case StepCounter::UnorderedRecoverCalls:
    return "UnorderedRecoverCalls";
  case StepCounter::UnorderedStrictPasses:
    return "UnorderedStrictPasses";
  case StepCounter::UnorderedEditablePasses:
    return "UnorderedEditablePasses";
  case StepCounter::RepetitionRecoverCalls:
    return "RepetitionRecoverCalls";
  case StepCounter::RepetitionFastAttempts:
    return "RepetitionFastAttempts";
  case StepCounter::RepetitionFastSuccess:
    return "RepetitionFastSuccess";
  case StepCounter::RepetitionFastFailures:
    return "RepetitionFastFailures";
  case StepCounter::GroupInsertMissingCurrent:
    return "GroupInsertMissingCurrent";
  case StepCounter::GroupSkipNullable:
    return "GroupSkipNullable";
  case StepCounter::GroupKeepCurrent:
    return "GroupKeepCurrent";
  case StepCounter::GroupRepairTail:
    return "GroupRepairTail";
  case StepCounter::MinimizeRecoveryEditsRuns:
    return "MinimizeRecoveryEditsRuns";
  case StepCounter::MinimizeRecoveryEditsDropped:
    return "MinimizeRecoveryEditsDropped";
  case StepCounter::MinimalEditProbeRuns:
    return "MinimalEditProbeRuns";
  case StepCounter::MinimalEditProbeWins:
    return "MinimalEditProbeWins";
  case StepCounter::RootPrefixRetryRuns:
    return "RootPrefixRetryRuns";
  case StepCounter::RootPrefixRetryWins:
    return "RootPrefixRetryWins";
  case StepCounter::Count:
    return "Count";
  }
  return "Unknown";
}

inline std::array<std::uint64_t, step_trace_index(StepCounter::Count)>
    stepTraceCounters{};

inline void stepTraceReset() noexcept {
  stepTraceCounters.fill(0);
}

inline void stepTraceInc(StepCounter counter,
                         std::uint64_t delta = 1) noexcept {
  stepTraceCounters[step_trace_index(counter)] += delta;
}

template <typename Stream>
inline void stepTraceDumpSummary(Stream &stream) noexcept {
  for (std::size_t i = 0; i < stepTraceCounters.size(); ++i) {
    const auto count = stepTraceCounters[i];
    if (count == 0) {
      continue;
    }
    const auto counter = static_cast<StepCounter>(i);
    stream << step_trace_name(counter) << ": " << count << '\n';
  }
}

template <typename... Args>
  requires(sizeof...(Args) != 1)
inline void stepTraceDumpSummary(Args &&...) noexcept {}

#else

#define PEGIUM_STEP_TRACE_INC(...) ((void)0)
#define PEGIUM_STEP_TRACE_RESET() ((void)0)
#define PEGIUM_STEP_TRACE_DUMP_SUMMARY(...) ((void)0)

inline void stepTraceReset() noexcept { /* step counters disabled */ }
inline void stepTraceInc(StepCounter, std::uint64_t = 1) noexcept { /* step counters disabled */
}
template <typename... Args>
inline void stepTraceDumpSummary(Args &&...) noexcept { /* step counters disabled */ }

#endif

} // namespace pegium::parser::detail
