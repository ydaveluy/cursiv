#include "cursiv/core/execution/TaskScheduler.hpp"

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

#include "cursiv/core/execution/WorkerPool.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

namespace cursiv {

TaskScheduler::TaskScheduler(std::size_t workerCount) {
  if (workerCount > 0) _pool = std::make_unique<WorkerPool>(workerCount);
}

TaskScheduler::~TaskScheduler() noexcept = default;

bool TaskScheduler::onWorkerThread() const noexcept {
  return _pool != nullptr && _pool->onWorkerThread();
}

std::size_t TaskScheduler::workerCount() const noexcept {
  return _pool != nullptr ? _pool->workerCount() : 0;
}

// The environment speaks first: under WASI `hardware_concurrency()` answers 1
// -- wasi-libc cannot see the host's cores -- so a pool that trusts it is
// never built, threads or no threads. `CURSIV_WORKERS` is how the host says
// what it knows; zero means "run inline", which is a legitimate answer and
// not a refusal to parse one.
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

  // Nothing escapes into the pool: a helper has no caller to unwind into, so
  // the catch is here and the pool never sees an exception.
  _pool->run(count, [&](std::size_t index) noexcept {
    // Once cancelled or once a sibling failed, the slice still iterates but
    // every item short-circuits on two relaxed loads.
    if (token.stop_requested() || failed.load(std::memory_order_relaxed))
      return;
    try {
      body(index);
    } catch (...) {
      // The exchange elects the one thread that records the first exception,
      // read only after the join below -- which is the happens-before, so no
      // lock.
      if (!failed.exchange(true)) first = std::current_exception();
    }
  });

  if (first != nullptr) std::rethrow_exception(first);
  throw_if_cancelled(token);
}

}  // namespace cursiv
