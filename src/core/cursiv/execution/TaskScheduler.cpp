#include "cursiv/execution/TaskScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <system_error>
#include <thread>

#include "cursiv/execution/WorkerPool.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv {

TaskScheduler::TaskScheduler(std::size_t workerCount) {
  if (workerCount > 0)
    _pool = std::make_unique<detail::WorkerPool>(workerCount);
}

TaskScheduler::~TaskScheduler() noexcept = default;

bool TaskScheduler::onWorkerThread() const noexcept {
  return _pool != nullptr && _pool->onWorkerThread();
}

std::size_t TaskScheduler::workerCount() const noexcept {
  return _pool != nullptr ? _pool->workerCount() : 0;
}

// `CURSIV_WORKERS` first, as WASI's `hardware_concurrency()` answers 1. Zero
// means run inline.
std::size_t TaskScheduler::defaultWorkerCount() noexcept {
  if (const char* const asked = std::getenv("CURSIV_WORKERS");
      asked != nullptr) {
    // Bounded: no machine has 1024 cores, and a thread per typo is a crash.
    constexpr std::size_t kMostWorkers = 1024;
    const std::string_view text = asked;
    std::size_t value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size())
      return std::min(value, kMostWorkers);
  }
  const unsigned concurrency = std::thread::hardware_concurrency();
  return concurrency <= 1 ? 0 : static_cast<std::size_t>(concurrency - 1);
}

void TaskScheduler::parallelForIndexed(
    const CancellationToken& token, std::size_t count,
    cursiv::function_ref<void(std::size_t)> body) {
  std::atomic<bool> failed{false};
  std::exception_ptr first;

  // No exception reaches the pool: a worker has no caller to unwind into.
  _pool->run(count, [&](std::size_t index) noexcept {
    // Once cancelled or once a sibling failed, the run stops: each part
    // returns at its next item, and the rest is never handed out.
    if (token.stop_requested() || failed.load(std::memory_order_relaxed))
      return false;
    try {
      body(index);
    } catch (...) {
      // One thread records the first exception; it is read only after the
      // join, which orders it, so no lock is needed.
      if (!failed.exchange(true)) first = std::current_exception();
      return false;
    }
    return true;
  });

  if (first != nullptr) std::rethrow_exception(first);
  throw_if_cancelled(token);
}

}  // namespace cursiv
