#include <atomic>
#include <barrier>
#include <cstdlib>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <thread>

#include "cursiv/core/execution/TaskQueue.hpp"
#include "cursiv/core/execution/WorkerPool.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/utils/Published.hpp"
#include "cursiv/test/Check.hpp"

namespace {
std::atomic<int> failAfter{-1};
std::atomic<std::thread::id> failThread;
std::atomic<int> allocations{0};
void fail_allocation(int after) {
  failThread = std::this_thread::get_id();
  failAfter.store(after, std::memory_order_release);
}
}  // namespace

// One controlled allocation failure on the submitting thread. Other workers
// can finish normally, which is the lifetime guarantee under test.
void* operator new(std::size_t size) {
  if (failAfter.load(std::memory_order_acquire) >= 0 &&
      failThread.load(std::memory_order_relaxed) ==
          std::this_thread::get_id() &&
      failAfter.fetch_sub(1, std::memory_order_relaxed) == 0)
    throw std::bad_alloc();
  if (void* p = std::malloc(size == 0 ? 1 : size)) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    return p;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
  if (p != nullptr) {
    allocations.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
  }
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
struct RefusingResource : std::pmr::memory_resource {
  std::size_t requested = 0;
  void* do_allocate(std::size_t bytes, std::size_t) override {
    requested = bytes;
    throw std::bad_alloc();
  }
  void do_deallocate(void*, std::size_t, std::size_t) override {}
  bool do_is_equal(const memory_resource& other) const noexcept override {
    return this == &other;
  }
};

void reservation_sizes_do_not_wrap() {
  RefusingResource memory;
  cursiv::CstRun run;
  run.bind(&memory);
  bool threw = false;
  try {
    run.reserve_for(std::numeric_limits<std::size_t>::max());
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  CHECK(threw && memory.requested > 64 * sizeof(cursiv::CstNode));
  memory.requested = 0;
  cursiv::AstArena arena(&memory);
  threw = false;
  try {
    (void)arena.carve_bytes(std::numeric_limits<std::size_t>::max(), 8);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  CHECK(threw && memory.requested == 0);
}

void a_failed_retirement_keeps_the_published_value_alive() {
  cursiv::Published<int> published;
  auto first = std::make_shared<const int>(1);
  const std::weak_ptr<const int> life = first;
  published.publish(std::move(first));
  auto second = std::make_shared<const int>(2);
  bool threw = false;
  fail_allocation(0);
  try {
    published.publish(second);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  failAfter = -1;
  CHECK(threw);
  CHECK(!life.expired());
  if (!life.expired()) CHECK(*published.read() == 1);
  published.publish(second);
  CHECK(*published.read() == 2);
}

void a_queue_allocation_failure_leaves_no_leak_or_lost_item() {
  for (int fail = 0; fail < 3; ++fail) {
    const int before = allocations.load();
    bool threw = false;
    fail_allocation(fail);
    try {
      cursiv::TaskQueue<int*> queue(0);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    failAfter = -1;
    CHECK(threw);
    CHECK(allocations.load() == before);
  }
  for (int fail = 0; fail < 2; ++fail) {
    cursiv::TaskQueue<int*> queue(0);
    int a = 1, b = 2;
    queue.push(&a);
    const int before = allocations.load();
    bool threw = false;
    fail_allocation(fail);
    try {
      queue.push(&b);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    failAfter = -1;
    CHECK(threw);
    CHECK(allocations.load() == before);
    CHECK(queue.steal() == &a);
    CHECK(queue.empty());
    queue.push(&b);
    CHECK(queue.pop() == &b);
  }
}

void a_partial_submission_joins_before_unwinding() {
  cursiv::WorkerPool pool(17);
  std::barrier entered(18);
  std::latch release(1);
  std::atomic<bool> elected{false};
  bool caught = false;
  // Other workers are occupied, so the nested submissions pile up in one
  // worker's queue until it stands four slots from full; the next submission
  // fails after publishing part of its jobs.
  const auto nested = [&](auto&& self, unsigned depth) -> void {
    if (depth == 60) fail_allocation(0);
    pool.run(18, [&](std::size_t i) noexcept {
      if (i == 0 && depth < 60) {
        try {
          self(self, depth + 1);
        } catch (const std::bad_alloc&) {
          caught = true;
        }
      }
    });
  };
  pool.run(18, [&](std::size_t) noexcept {
    entered.arrive_and_wait();
    if (pool.onWorkerThread() && !elected.exchange(true)) {
      nested(nested, 0);
      failAfter = -1;
      release.count_down();
    } else {
      release.wait();
    }
  });
  CHECK(caught);
  std::atomic<unsigned> calls{0};
  pool.run(100, [&](std::size_t) noexcept { ++calls; });
  CHECK(calls == 100);
}
}  // namespace

int main() {
  reservation_sizes_do_not_wrap();
  a_failed_retirement_keeps_the_published_value_alive();
  a_queue_allocation_failure_leaves_no_leak_or_lost_item();
  a_partial_submission_joins_before_unwinding();
  return cursiv::test::report("PublicationFailure_test");
}
