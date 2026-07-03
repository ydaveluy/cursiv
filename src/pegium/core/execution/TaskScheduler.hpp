#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/utils/FunctionRef.hpp>

namespace pegium::execution {

/// Minimal parallel-for facade over a work-stealing executor, used by the
/// document builder to run each build phase across the workspace. The executor
/// type is kept out of this header (PIMPL) so it stays a private dependency.
class TaskScheduler {
public:
  explicit TaskScheduler(std::size_t workerCount = defaultWorkerCount());
  ~TaskScheduler() noexcept;

  TaskScheduler(const TaskScheduler &) = delete;
  TaskScheduler &operator=(const TaskScheduler &) = delete;
  TaskScheduler(TaskScheduler &&) = delete;
  TaskScheduler &operator=(TaskScheduler &&) = delete;

  /// Default worker thread count: one fewer than the hardware concurrency so the
  /// submitting thread is not oversubscribed (0 when concurrency is unknown or
  /// 1, in which case parallelFor runs inline on the caller).
  [[nodiscard]] static std::size_t defaultWorkerCount() noexcept;

  /// Applies @p task to every element of @p range in parallel and waits for all
  /// of them. The first exception any task throws, and cooperative cancellation
  /// via @p cancelToken, propagate to the caller. Ranges smaller than
  /// @p minParallelCount (and the no-worker configuration) run inline on the
  /// calling thread, letting callers keep inputs too small to amortise dispatch
  /// off the executor. Safe to call from within a running task: a worker thread
  /// participates (co-runs) instead of blocking, so nesting cannot deadlock.
  template <typename Range, typename F>
  void parallelFor(const utils::CancellationToken &cancelToken, Range &&range,
                   F &&task, std::size_t minParallelCount = 2);

  /// Splits [0, count) into contiguous chunks, runs `chunk(begin, end)` for each
  /// in parallel, and returns their results in ascending chunk order so the
  /// caller can concatenate them deterministically. Each chunk gets its own
  /// result, so a chunk body that accumulates into it needs no locking.
  /// Parallelises only for a standalone caller (off-worker, so the pool is not
  /// already saturated) with enough work; otherwise it runs one inline chunk.
  template <typename ChunkFn>
  [[nodiscard]] auto parallelChunked(const utils::CancellationToken &cancelToken,
                                     std::size_t count, ChunkFn &&chunk)
      -> std::vector<std::invoke_result_t<ChunkFn &, std::size_t, std::size_t>>;

  /// Returns whether the calling thread is one of this scheduler's workers.
  /// A standalone unit of work runs on the caller (a non-worker), whereas work
  /// fanned out across the pool runs on a worker; callers use this to skip
  /// nested dispatch that would only oversubscribe an already-busy pool.
  [[nodiscard]] bool onWorkerThread() const noexcept;

  /// Number of worker threads (0 when there is no executor / no parallelism).
  [[nodiscard]] std::size_t workerCount() const noexcept;

private:
  struct Impl;

  // Runs body(i) for i in [0, count) in parallel on the executor, capturing the
  // first exception thrown and honouring cooperative cancellation. count > 1 and
  // a non-null executor are guaranteed by the caller.
  void parallelForIndexed(const utils::CancellationToken &cancelToken,
                          std::size_t count,
                          utils::function_ref<void(std::size_t)> body);

  std::unique_ptr<Impl> _impl; // null when there are no workers
};

template <typename Range, typename F>
void TaskScheduler::parallelFor(const utils::CancellationToken &cancelToken,
                                Range &&range, F &&task,
                                std::size_t minParallelCount) {
  utils::throw_if_cancelled(cancelToken);
  const auto begin = std::begin(range);
  const auto end = std::end(range);

  if constexpr (std::ranges::random_access_range<Range> &&
                std::ranges::sized_range<Range>) {
    const auto count = static_cast<std::size_t>(std::ranges::size(range));
    if (count == 0U) {
      return;
    }
    // Dispatch to the executor only when the range is large enough to amortise
    // it (at least minParallelCount, and never a single element) and worker
    // threads exist; otherwise run inline on the calling thread.
    if (_impl != nullptr && count > 1U && count >= minParallelCount) {
      parallelForIndexed(cancelToken, count, [&](std::size_t index) {
        task(*(begin + static_cast<std::ptrdiff_t>(index)));
      });
      return;
    }
  }

  std::size_t pollCounter = 0;
  for (auto current = begin; current != end; ++current) {
    if ((pollCounter++ & 0x3fU) == 0U) {
      utils::throw_if_cancelled(cancelToken);
    }
    task(*current);
  }
}

template <typename ChunkFn>
auto TaskScheduler::parallelChunked(const utils::CancellationToken &cancelToken,
                                    std::size_t count, ChunkFn &&chunk)
    -> std::vector<std::invoke_result_t<ChunkFn &, std::size_t, std::size_t>> {
  using Result = std::invoke_result_t<ChunkFn &, std::size_t, std::size_t>;
  // One inline chunk unless this is a standalone caller (off-worker, so the pool
  // is not already saturated) with enough work to amortise dispatch; then split
  // into a few chunks per worker for work-stealing balance.
  constexpr std::size_t kMinParallelCount = 256;
  constexpr std::size_t kTargetChunkSize = 64;
  std::size_t chunkCount = count == 0U ? 0U : 1U;
  if (workerCount() > 0U && !onWorkerThread() && count >= kMinParallelCount) {
    chunkCount =
        std::max<std::size_t>(1U, std::min(workerCount() * 4U,
                                           count / kTargetChunkSize));
  }

  std::vector<Result> results(chunkCount);
  if (chunkCount != 0U) {
    parallelFor(
        cancelToken, std::views::iota(std::size_t{0}, chunkCount),
        [&](std::size_t index) {
          results[index] = chunk(index * count / chunkCount,
                                 (index + 1U) * count / chunkCount);
        },
        2);
  }
  return results;
}

} // namespace pegium::execution
