#pragma once

/// Shared delete-scan recovery utilities.

#include <limits>
#include <utility>

#include <pegium/core/parser/ParseContext.hpp>
#include <pegium/core/utils/FunctionRef.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser::detail {

template <typename Context> struct ExtendedDeleteScanBudgetScope {
  Context &ctx;
  std::uint32_t savedMaxConsecutiveDeletes;
  std::uint32_t savedMaxEditsPerAttempt;
  std::uint32_t savedMaxEditCost;
  bool enabled = false;

  explicit ExtendedDeleteScanBudgetScope(Context &ctx) noexcept
      : ctx(ctx),
        savedMaxConsecutiveDeletes(ctx.maxConsecutiveCodepointDeletes),
        savedMaxEditsPerAttempt(ctx.maxEditsPerAttempt),
        savedMaxEditCost(ctx.maxEditCost) {}

  ExtendedDeleteScanBudgetScope(const ExtendedDeleteScanBudgetScope &) = delete;
  ExtendedDeleteScanBudgetScope &
  operator=(const ExtendedDeleteScanBudgetScope &) = delete;

  ~ExtendedDeleteScanBudgetScope() noexcept { restore(); }

  [[nodiscard]] bool tryEnable() noexcept {
    if (enabled) {
      return false;
    }
    ctx.maxConsecutiveCodepointDeletes = std::numeric_limits<std::uint32_t>::max();
    ctx.maxEditsPerAttempt = std::numeric_limits<std::uint32_t>::max();
    ctx.maxEditCost = std::numeric_limits<std::uint32_t>::max();
    enabled = true;
    return true;
  }

  void restore() noexcept {
    if (!enabled) {
      return;
    }
    ctx.maxConsecutiveCodepointDeletes = savedMaxConsecutiveDeletes;
    ctx.maxEditsPerAttempt = savedMaxEditsPerAttempt;
    ctx.maxEditCost = savedMaxEditCost;
    enabled = false;
  }

  void commitOverflowEdits() noexcept {
    if (!enabled) {
      return;
    }
    restore();
    if constexpr (requires { ctx.allowBudgetOverflowEdits(); }) {
      ctx.allowBudgetOverflowEdits();
    }
  }
};

template <typename Context> struct DeleteRetryReplayScope {
  Context &ctx;
  bool savedSkipAfterDelete;
  ExtendedDeleteScanBudgetScope<Context> overflowBudgetScope;

  explicit DeleteRetryReplayScope(Context &ctx) noexcept
      : ctx(ctx), savedSkipAfterDelete(ctx.skipAfterDelete),
        overflowBudgetScope(ctx) {
    ctx.skipAfterDelete = false;
  }

  DeleteRetryReplayScope(const DeleteRetryReplayScope &) = delete;
  DeleteRetryReplayScope &
  operator=(const DeleteRetryReplayScope &) = delete;

  ~DeleteRetryReplayScope() noexcept {
    ctx.skipAfterDelete = savedSkipAfterDelete;
  }

  [[nodiscard]] bool tryEnableExtendedDeleteScan() noexcept {
    return overflowBudgetScope.tryEnable();
  }

  void commitOverflowEdits() noexcept { overflowBudgetScope.commitOverflowEdits(); }
};

struct DeleteScanOptions {
  std::uint32_t maxDeletes = std::numeric_limits<std::uint32_t>::max();
  bool allowOverflow = true;
};

enum class DeleteScanVisitResult : std::uint8_t {
  Continue,
  Stop,
  Accept,
};

struct DeleteScanVisitOptions {
  DeleteScanOptions scan{};
  bool extendThroughHiddenTrivia = false;
  bool stopAtHiddenTriviaBoundary = false;
  bool visitAfterHiddenTriviaExtension = false;
};

struct DeleteScanVisitState {
  bool overflowBudget = false;
  bool hiddenTriviaBoundary = false;
  bool hiddenTriviaExtended = false;
  std::uint32_t deleteCount = 0u;
};

using DeleteRetryVisitResult = DeleteScanVisitResult;

struct DeleteRetryOptions {
  DeleteScanOptions scan{};
  bool extendThroughHiddenTrivia = true;
  bool stopOverflowAtStructuredVisibleSource = false;
};

using DeleteRetryVisitState = DeleteScanVisitState;

template <typename MatchFn>
[[nodiscard]] inline const char *
invoke_delete_scan_match(MatchFn &matchFn, const char *scanCursor,
                         std::uint32_t deleteCount) {
  if constexpr (requires { matchFn(scanCursor, deleteCount); }) {
    return matchFn(scanCursor, deleteCount);
  } else {
    return matchFn(scanCursor);
  }
}

template <typename OnMatchFn>
inline void invoke_delete_scan_on_match(OnMatchFn &onMatchFn,
                                        const char *matchedEnd,
                                        std::uint32_t deleteCount) {
  if constexpr (requires { onMatchFn(matchedEnd, deleteCount); }) {
    onMatchFn(matchedEnd, deleteCount);
  } else {
    onMatchFn(matchedEnd);
  }
}

template <typename Context>
[[nodiscard]] constexpr bool
position_starts_structured_visible_source(const Context &ctx,
                                          const char *position) noexcept {
  if (position == nullptr || position >= ctx.end) {
    return false;
  }
  if constexpr (requires { ctx.skip_without_builder(position); }) {
    if (ctx.skip_without_builder(position) != position) {
      return false;
    }
  }
  return is_identifier_like_codepoint_at(position, ctx.end);
}

// True iff a hidden-trivia boundary sits at the cursor (the just-deleted
// codepoint abuts hidden trivia). Shared by the delete-scan and delete-retry
// passes, which compute it identically. Contexts without skip_without_builder
// (none on the recovery path today) report no boundary.
template <typename Context>
[[nodiscard]] inline bool detect_hidden_trivia_boundary(Context &ctx) noexcept {
  if constexpr (requires { ctx.skip_without_builder(ctx.cursor()); }) {
    return ctx.skip_without_builder(ctx.cursor()) > ctx.cursor();
  } else {
    return false;
  }
}

/// Guarded delete-scan: walk forward deleting one codepoint at a time (up to
/// `options.scan.maxDeletes`, while `canDeleteStep()` holds), invoking `visit`
/// at each cursor. Two passes: a normal-budget pass, then — only if
/// `allowOverflow` and the overflow budget can be enabled — an overflow pass
/// whose edits are committed only when `visit` returns Accept. At a hidden-
/// trivia boundary the last delete is optionally extended through the trivia
/// (and re-visited, unless `visitAfterHiddenTriviaExtension` is off), otherwise
/// the scan may stop. Any non-Continue result ends the walk: the checkpoint is
/// rewound unless the result is Accept; reaching the end rewinds and returns
/// Stop. `recover_by_guarded_delete_scan` is the match / on-match wrapper.
template <typename Context>
[[nodiscard]] inline DeleteScanVisitResult
visit_guarded_delete_scan_positions(
    Context &ctx, utils::function_ref<bool()> canDeleteStep,
    utils::function_ref<DeleteScanVisitResult(const DeleteScanVisitState &)>
        visit,
    DeleteScanVisitOptions options = {}) {
  if (!ctx.canDelete()) {
    return DeleteScanVisitResult::Stop;
  }

  const auto recoveryCheckpoint = ctx.mark();
  ExtendedDeleteScanBudgetScope overflowBudgetScope{ctx};
  std::uint32_t deleteCount = 0u;
  const auto try_visit_at_current_position =
      [&](const DeleteScanVisitState &state) {
        const auto result = visit(state);
        if (result == DeleteScanVisitResult::Accept && state.overflowBudget) {
          overflowBudgetScope.commitOverflowEdits();
        }
        return result;
      };
  const auto try_visit_pass = [&](bool overflowBudget) {
    while (deleteCount < options.scan.maxDeletes && canDeleteStep() &&
           ctx.deleteOneCodepoint()) {
      ++deleteCount;
      const bool hiddenTriviaBoundary = detect_hidden_trivia_boundary(ctx);
      const auto visitState = DeleteScanVisitState{
          .overflowBudget = overflowBudget,
          .hiddenTriviaBoundary = hiddenTriviaBoundary,
          .hiddenTriviaExtended = false,
          .deleteCount = deleteCount,
      };
      if (const auto result = try_visit_at_current_position(visitState);
          result != DeleteScanVisitResult::Continue) {
        return result;
      }
      if (!hiddenTriviaBoundary) {
        continue;
      }
      if (options.extendThroughHiddenTrivia) {
        if constexpr (requires { ctx.extendLastDeleteThroughHiddenTrivia(); }) {
          if (ctx.extendLastDeleteThroughHiddenTrivia()) {
            if (!options.visitAfterHiddenTriviaExtension) {
              continue;
            }
            const auto extendedVisitState = DeleteScanVisitState{
                .overflowBudget = overflowBudget,
                .hiddenTriviaBoundary = false,
                .hiddenTriviaExtended = true,
                .deleteCount = deleteCount,
            };
            if (const auto result =
                    try_visit_at_current_position(extendedVisitState);
                result != DeleteScanVisitResult::Continue) {
              return result;
            }
            continue;
          }
        }
      }
      if (options.stopAtHiddenTriviaBoundary) {
        return DeleteScanVisitResult::Stop;
      }
    }
    return DeleteScanVisitResult::Continue;
  };

  if (const auto result = try_visit_pass(false);
      result != DeleteScanVisitResult::Continue) {
    if (result != DeleteScanVisitResult::Accept) {
      ctx.rewind(recoveryCheckpoint);
    }
    return result;
  }

  if (options.scan.allowOverflow && overflowBudgetScope.tryEnable()) {
    if (const auto result = try_visit_pass(true);
        result != DeleteScanVisitResult::Continue) {
      if (result != DeleteScanVisitResult::Accept) {
        ctx.rewind(recoveryCheckpoint);
      }
      return result;
    }
  }

  ctx.rewind(recoveryCheckpoint);
  return DeleteScanVisitResult::Stop;
}

template <typename Context, typename CanDeleteStepFn, typename MatchFn,
          typename OnMatchFn>
[[nodiscard]] inline bool recover_by_guarded_delete_scan(
    Context &ctx, CanDeleteStepFn &&canDeleteStepFn, MatchFn &&matchFn,
    OnMatchFn &&onMatchFn, DeleteScanOptions options = {}) {
  auto &&match = matchFn;
  auto &&onMatch = onMatchFn;
  return visit_guarded_delete_scan_positions(
             ctx, std::forward<CanDeleteStepFn>(canDeleteStepFn),
             [&](const DeleteScanVisitState &state) {
               if (const char *const matchedEnd =
                       invoke_delete_scan_match(match, ctx.cursor(),
                                                state.deleteCount);
                   matchedEnd != nullptr) {
                 invoke_delete_scan_on_match(onMatch, matchedEnd,
                                             state.deleteCount);
                 return DeleteScanVisitResult::Accept;
               }
               return DeleteScanVisitResult::Continue;
             },
             {.scan = options}) == DeleteScanVisitResult::Accept;
}

/// Guarded delete-retry: the same two-pass delete walk as
/// `visit_guarded_delete_scan_positions`, but each position is first gated by
/// `shouldRetry` before `retry` runs, replay / commit go through a
/// `DeleteRetryReplayScope`, and the overflow pass additionally bails
/// (Continue) at a position that starts structured visible source once anything
/// has been deleted, so overflow deletions never cut into real source.
/// `recover_by_guarded_delete_retry` is the bool wrapper.
template <typename Context>
[[nodiscard]] inline DeleteRetryVisitResult
visit_guarded_delete_retry_positions(
    Context &ctx,
    utils::function_ref<bool(const DeleteRetryVisitState &)> shouldRetry,
    utils::function_ref<bool(const DeleteRetryVisitState &)> retry,
    DeleteRetryOptions options = {}) {
  if (!ctx.canDelete()) {
    return DeleteRetryVisitResult::Stop;
  }

  const auto recoveryCheckpoint = ctx.mark();
  DeleteRetryReplayScope retryScope{ctx};
  std::uint32_t deleteCount = 0u;
  const auto try_retry_at_current_position =
      [&](const DeleteRetryVisitState &state) {
        if (!shouldRetry(state)) {
          return DeleteRetryVisitResult::Continue;
        }
        const auto result = retry(state) ? DeleteRetryVisitResult::Accept
                                         : DeleteRetryVisitResult::Continue;
        if (result == DeleteRetryVisitResult::Accept && state.overflowBudget) {
          retryScope.commitOverflowEdits();
        }
        return result;
  };
  const auto try_retry_pass = [&](bool overflowBudget) {
    while (deleteCount < options.scan.maxDeletes) {
      if (overflowBudget && options.stopOverflowAtStructuredVisibleSource &&
          deleteCount != 0u &&
          position_starts_structured_visible_source(ctx, ctx.cursor())) {
        return DeleteRetryVisitResult::Continue;
      }
      if (!ctx.deleteOneCodepoint()) {
        break;
      }
      ++deleteCount;
      const bool hiddenTriviaBoundary = detect_hidden_trivia_boundary(ctx);
      const auto visitState = DeleteRetryVisitState{
          .overflowBudget = overflowBudget,
          .hiddenTriviaBoundary = hiddenTriviaBoundary,
          .hiddenTriviaExtended = false,
          .deleteCount = deleteCount,
      };
      if (const auto result = try_retry_at_current_position(visitState);
          result != DeleteRetryVisitResult::Continue) {
        return result;
      }
      if (!hiddenTriviaBoundary) {
        continue;
      }
      if (options.extendThroughHiddenTrivia) {
        if constexpr (requires { ctx.extendLastDeleteThroughHiddenTrivia(); }) {
          if (ctx.extendLastDeleteThroughHiddenTrivia()) {
            const auto extendedVisitState = DeleteRetryVisitState{
                .overflowBudget = overflowBudget,
                .hiddenTriviaBoundary = false,
                .hiddenTriviaExtended = true,
                .deleteCount = deleteCount,
            };
            if (const auto result =
                    try_retry_at_current_position(extendedVisitState);
                result != DeleteRetryVisitResult::Continue) {
              return result;
            }
            continue;
          }
        }
      }
    }
    return DeleteRetryVisitResult::Continue;
  };

  if (const auto result = try_retry_pass(false);
      result != DeleteRetryVisitResult::Continue) {
    if (result != DeleteRetryVisitResult::Accept) {
      ctx.rewind(recoveryCheckpoint);
    }
    return result;
  }

  if (options.scan.allowOverflow && retryScope.tryEnableExtendedDeleteScan()) {
    if (const auto result = try_retry_pass(true);
        result != DeleteRetryVisitResult::Continue) {
      if (result != DeleteRetryVisitResult::Accept) {
        ctx.rewind(recoveryCheckpoint);
      }
      return result;
    }
  }

  ctx.rewind(recoveryCheckpoint);
  return DeleteRetryVisitResult::Stop;
}

template <typename Context, typename ShouldRetryFn, typename RetryFn>
[[nodiscard]] inline bool recover_by_guarded_delete_retry(
    Context &ctx, ShouldRetryFn &&shouldRetryFn, RetryFn &&retryFn,
    DeleteRetryOptions options = {}) {
  const auto result = visit_guarded_delete_retry_positions(
      ctx, std::forward<ShouldRetryFn>(shouldRetryFn),
      std::forward<RetryFn>(retryFn), options);
  return result == DeleteRetryVisitResult::Accept;
}

} // namespace pegium::parser::detail
