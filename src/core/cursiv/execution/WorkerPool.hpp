#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cursiv/execution/AtomicNotifier.hpp"
#include "cursiv/execution/TaskQueue.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// An indexed parallel-for over a shared claim counter, balanced by work
// stealing. The publisher runs one part, then takes tasks until its run is
// done, so a nested `run` never blocks a worker on itself.

namespace cursiv::detail {

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
  // call has returned. A call that returns false ends the run. `body` must not
  // throw.
  void run(std::size_t count, cursiv::function_ref<bool(std::size_t)> body);

 private:
  // Runs in flight at once; when every cell is taken, a run runs alone.
  static constexpr std::size_t kCells = 64;

  // The parts still running, owned by the pool so the last part never touches
  // the publisher's stack. `asleep` says the publisher sleeps on `done`, so the
  // last part takes the mutex only then.
  struct alignas(128) Cell {
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> taken{false};
    std::atomic<bool> asleep{false};
    std::mutex mutex;
    std::condition_variable done;
  };

  // One run's claim counter, on the publisher's stack, which outlives every
  // task of the run.
  struct Job {
    cursiv::function_ref<bool(std::size_t)> body;
    std::size_t count;
    Cell* cell;
    // On its own line: every part hammers the claim counter for the whole run.
    alignas(128) std::atomic<std::size_t> next{0};
  };

  // One per run, on the publisher's stack and pushed once per part: the parts
  // are identical, so the queues hold the same pointer N times.
  struct Task {
    Job* job;
  };

  struct Worker {
    TaskQueue<Task*> queue;
    std::size_t victim = 0;  // where the last steal succeeded
    // A xorshift reduced by multiply-shift: `<random>` costs a division per
    // draw on the loop every idle worker runs, and only the spread matters.
    std::uint64_t random = 0;
    std::thread thread;
  };

  // Where a non-worker publishes, as a deque is pushed by its owner only. Fewer
  // buckets than workers, and spread by rank: one run's parts share a pointer,
  // so hashing it would put them all behind one mutex.
  struct Bucket {
    std::mutex mutex;
    TaskQueue<Task*> queue;
  };

  // Claims and runs one index at a time until none is left.
  void drain(Job& job) const noexcept;
  void runTask(Task& task) const noexcept;
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
  // The publisher's scheduling loop, run until its job is done. A worker never
  // leaves it; another caller sleeps after a short while to free the core.
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

}  // namespace cursiv::detail
