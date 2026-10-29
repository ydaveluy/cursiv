#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cursiv/core/execution/AtomicNotifier.hpp"
#include "cursiv/core/execution/TaskQueue.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// An indexed parallel-for over a shared claim counter, load-balanced by
// work-stealing. The publisher runs one part itself and then keeps taking
// tasks until its own run is done, so a nested `run` call never blocks a
// worker on itself.

namespace cursiv {

class WorkerPool {
 public:
  explicit WorkerPool(std::size_t workers);
  ~WorkerPool() noexcept;
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  [[nodiscard]] std::size_t workerCount() const noexcept {
    return _workers.size();
  }
  [[nodiscard]] bool onWorkerThread() const noexcept;

  // Calls `body(index)` for every index below `count` and returns once every
  // call has returned. `body` must not throw: a helper has no caller to unwind
  // into, so the exception is the publisher's to catch inside the body.
  void run(std::size_t count, cursiv::function_ref<void(std::size_t)> body);

 private:
  // The shared claim counter of one run, on the publisher's stack. It outlives
  // every task of that run because the publisher co-runs until `pending` is 0.
  // Above any plausible count of runs in flight; a run that finds them all
  // taken simply runs alone, which is slow and never wrong.
  static constexpr std::size_t kCells = 64;

  // The count of parts still running, and it belongs to the pool: a publisher
  // that is not a worker sleeps on it, so the part that empties it has to
  // signal -- and signalling a counter on the publisher's stack would touch it
  // after that publisher was free to return. A cell outlives every run.
  struct alignas(128) Cell {
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> taken{false};
  };

  struct Job {
    cursiv::function_ref<void(std::size_t)> body;
    std::size_t count;
    std::size_t parties;
    Cell* cell;
    // On its own line: every part hammers the claim counter for the whole run.
    alignas(128) std::atomic<std::size_t> next{0};
  };

  // One object, pushed once per part: every part of a run does the same thing,
  // so they need not be distinct, and the queue holds the same pointer N times.
  // That saves one allocation per run, which matters on a stream of small runs.
  struct Task {
    Job* job;
  };

  struct Worker {
    TaskQueue<Task*> queue;
    std::size_t victim = 0;  // where the last steal succeeded
    // A xorshift, and the range taken by multiply-shift: the standard engine
    // and distribution cost a division apiece, on the loop a whole idle pool
    // runs. Only the spread matters here, never the statistical quality.
    std::uint64_t random = 0;
    std::thread thread;
  };

  // Where a non-worker publishes: a deque may only be pushed by its owner.
  // Fewer buckets than workers on purpose, since every bucket is one more
  // place a thief has to look.
  //
  // Spread by rank, not by address: one run's parts share one pointer pushed
  // many times, so hashing the pointer would land every part in a single
  // bucket behind a single mutex -- one lock taken once per part instead of
  // once for the batch.
  struct Bucket {
    std::mutex mutex;
    TaskQueue<Task*> queue;
  };

  // Coarse slices while the range is long, then one at a time at the tail.
  void drain(Job& job) const noexcept;
  void runTask(Task& task) noexcept;
  // Null when nothing was found. `self` is the worker index, or the worker
  // count for a caller that is not one.
  [[nodiscard]] Task* steal(std::size_t victim) noexcept;
  [[nodiscard]] bool queueEmpty(std::size_t victim) const noexcept;
  [[nodiscard]] bool explore(Worker& worker, Task*& task) noexcept;
  [[nodiscard]] bool waitForTask(Worker& worker, std::size_t index,
                                 Task*& task) noexcept;
  void workerLoop(std::size_t index) noexcept;
  void stopAndJoin() noexcept;
  [[nodiscard]] Cell* claimCell() noexcept;
  // The publisher's own scheduling loop, run until its job is done. A worker
  // never leaves it: blocking one would hold a thread the parts it is waiting
  // for may need. A caller that is not a worker gives up after a short while
  // and sleeps, which is what keeps it off a core the parts are using.
  void corun(const Job& job, std::size_t self) noexcept;
  [[nodiscard]] std::size_t selfIndex() const noexcept;
  // Publishes `parts` copies of `task`, which is what makes them stealable.
  void submit(Task& task, std::size_t self, std::size_t parts);

  [[nodiscard]] std::size_t victimCount() const noexcept {
    return _workers.size() + _buckets.size();
  }

  std::array<Cell, kCells> _cells;
  std::vector<std::unique_ptr<Worker>> _workers;
  std::vector<std::unique_ptr<Bucket>> _buckets;
  AtomicNotifier _idle;
  std::atomic<bool> _stopping{false};
};

}  // namespace cursiv
