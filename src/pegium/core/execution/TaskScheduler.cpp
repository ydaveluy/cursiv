#include <pegium/core/execution/TaskScheduler.hpp>

#include <atomic>
#include <exception>
#include <thread>

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

namespace pegium::execution {

// Holds the work-stealing executor. Kept out of the header so Taskflow stays a
// private dependency.
struct TaskScheduler::Impl {
  explicit Impl(std::size_t workers) : executor(workers) {}
  tf::Executor executor;
};

TaskScheduler::TaskScheduler(std::size_t workerCount) {
  if (workerCount > 0) {
    _impl = std::make_unique<Impl>(workerCount);
  }
}

TaskScheduler::~TaskScheduler() noexcept = default;

bool TaskScheduler::onWorkerThread() const noexcept {
  return _impl != nullptr && _impl->executor.this_worker_id() >= 0;
}

std::size_t TaskScheduler::workerCount() const noexcept {
  return _impl != nullptr ? _impl->executor.num_workers() : 0;
}

std::size_t TaskScheduler::defaultWorkerCount() noexcept {
  const auto concurrency = std::thread::hardware_concurrency();
  if (concurrency <= 1U) {
    return 0;
  }
  return static_cast<std::size_t>(concurrency - 1U);
}

void TaskScheduler::parallelForIndexed(
    const utils::CancellationToken &cancelToken, std::size_t count,
    utils::function_ref<void(std::size_t)> body) {
  std::atomic<bool> failed{false};
  std::exception_ptr firstException;

  // for_each_index with the default (guided) partitioner: a sweep over
  // partitioner types and chunk sizes confirmed the default is fastest here —
  // guided's adaptive chunking balances best across heterogeneous cores, and
  // larger fixed chunks / static / dynamic all regress.
  tf::Taskflow taskflow;
  taskflow.for_each_index(
      std::size_t{0}, count, std::size_t{1}, [&](std::size_t index) {
        // Stop doing work once cancelled or once a sibling has failed; the
        // partition still iterates but every item short-circuits cheaply.
        if (cancelToken.stop_requested() ||
            failed.load(std::memory_order_relaxed)) {
          return;
        }
        try {
          body(index);
        } catch (...) {
          // failed.exchange selects the single thread that records the first
          // exception; it is read only by the caller after the executor join
          // below, which establishes the happens-before, so no lock is needed.
          if (!failed.exchange(true)) {
            firstException = std::current_exception();
          }
        }
      });

  // corun (participate) when already on a worker — a worker may not block on
  // run().wait() without risking deadlock; from the main thread, block.
  if (_impl->executor.this_worker_id() >= 0) {
    _impl->executor.corun(taskflow);
  } else {
    _impl->executor.run(taskflow).wait();
  }

  if (firstException != nullptr) {
    std::rethrow_exception(firstException);
  }
  utils::throw_if_cancelled(cancelToken);
}

} // namespace pegium::execution
