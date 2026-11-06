#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// A parallel-for over a pool of workers. The pool is only forward-declared, so
// no consumer of the workspace compiles a thread header for it and swapping it
// changes one file.

namespace cursiv {

namespace detail {
class WorkerPool;
}  // namespace detail

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

  // As above, for many small pieces of work, where handing each to a worker
  // would cost more than doing it: `task` takes a run of `items` instead, of
  // at least `minRun` of them. Runs start at multiples of `minRun` and the
  // last one takes the rest, so `items.size() / minRun` runs in all; a span
  // shorter than two runs goes to `task` whole, on the calling thread.
  template <class T, class F>
  void parallelFor(const CancellationToken& token, std::span<T> items,
                   std::size_t minRun, F&& task);

  // Used to skip a nested fan-out that would only oversubscribe a busy pool.
  [[nodiscard]] bool onWorkerThread() const noexcept;
  [[nodiscard]] std::size_t workerCount() const noexcept;

 private:
  // Precondition: `count > 1` and a live pool, both tested above.
  void parallelForIndexed(const CancellationToken& token, std::size_t count,
                          cursiv::function_ref<void(std::size_t)> body);

  std::unique_ptr<detail::WorkerPool> _pool;  // null when there are no workers
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

template <class T, class F>
void TaskScheduler::parallelFor(const CancellationToken& token,
                                std::span<T> items, std::size_t minRun,
                                F&& task) {
  assert(minRun > 0 && "a run holds at least one item");
  const std::size_t runs =
      items.empty() ? 0 : std::max<std::size_t>(1, items.size() / minRun);
  parallelFor(token, std::views::iota(std::size_t{0}, runs),
              [&](std::size_t run) {
                const std::span<T> rest = items.subspan(run * minRun);
                task(run + 1 == runs ? rest : rest.first(minRun));
              });
}

}  // namespace cursiv
