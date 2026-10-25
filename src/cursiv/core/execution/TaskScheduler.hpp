#pragma once

#include <cstddef>
#include <memory>
#include <ranges>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// A parallel-for over a pool of workers. The pool is only forward-declared, so
// no consumer of the workspace compiles a thread header for it and swapping it
// changes one file.

namespace cursiv {

class WorkerPool;

class TaskScheduler {
 public:
  explicit TaskScheduler(std::size_t workerCount = defaultWorkerCount());
  ~TaskScheduler() noexcept;
  TaskScheduler(const TaskScheduler&) = delete;
  TaskScheduler& operator=(const TaskScheduler&) = delete;

  // One fewer than the hardware, so the submitting thread is not
  // oversubscribed. Zero on a single core, where everything below runs inline.
  [[nodiscard]] static std::size_t defaultWorkerCount() noexcept;

  // Applies `task` to every element and waits. The first exception a task
  // throws is rethrown here, before any cancellation is reported; cancellation
  // is cooperative and short-circuits the rest with `OperationCancelled`.
  // Below `minParallelCount` -- and for any range that is not sized and
  // random-access -- the run is inline on the calling thread. Safe to call
  // from inside a task (a worker participates in the nested run) and from two
  // threads at once on one scheduler. Neither the range nor the task is
  // owned: both must outlive the call, which always returns after the last
  // element.
  template <class Range, class F>
  void parallelFor(const CancellationToken& token, Range&& range, F&& task,
                   std::size_t minParallelCount = 2);

  // Used to skip a nested fan-out that would only oversubscribe a busy pool.
  [[nodiscard]] bool onWorkerThread() const noexcept;
  [[nodiscard]] std::size_t workerCount() const noexcept;

 private:
  // Precondition: `count > 1` and a live pool, both tested above.
  void parallelForIndexed(const CancellationToken& token, std::size_t count,
                          cursiv::function_ref<void(std::size_t)> body);

  std::unique_ptr<WorkerPool> _pool;  // null when there are no workers
};

template <class Range, class F>
void TaskScheduler::parallelFor(const CancellationToken& token, Range&& range,
                                F&& task, std::size_t minParallelCount) {
  throw_if_cancelled(token);
  const auto begin = std::ranges::begin(range);
  const auto end = std::ranges::end(range);

  if constexpr (std::ranges::random_access_range<Range> &&
                std::ranges::sized_range<Range>) {
    const auto count = static_cast<std::size_t>(std::ranges::size(range));
    if (count == 0) return;
    if (_pool != nullptr && count > 1 && count >= minParallelCount) {
      parallelForIndexed(token, count, [&](std::size_t index) {
        task(*(begin + static_cast<std::ptrdiff_t>(index)));
      });
      return;
    }
  }

  // The inline path polls rather than testing per item: the token is an atomic
  // load, which a tight range of cheap bodies would feel.
  std::size_t polls = 0;
  for (auto current = begin; current != end; ++current) {
    if ((polls++ & 0x3f) == 0) throw_if_cancelled(token);
    task(*current);
  }
  throw_if_cancelled(token);
}

}  // namespace cursiv
