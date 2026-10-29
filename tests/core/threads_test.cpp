// What runs on threads, which a threadless target cannot build: the scheduler's
// parallel-for, the value read without a lock, the workspace's reader/writer
// lock, and names recorded concurrently.

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/core/references/ImportedNames.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/Published.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceLock.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/test/Check.hpp"

// Pins the contract of the parallel-for, which is what a change of pool has to
// keep: every index runs exactly once, the call returns after the last body,
// the first exception is rethrown, cancellation short-circuits the rest, and a
// body may run another parallel-for without deadlocking.
namespace task_scheduler {

namespace {

// Four workers whatever the machine, so the lanes below are the same run on a
// laptop and on a build farm.
constexpr std::size_t kWorkers = 4;

std::vector<std::size_t> indices(std::size_t count) {
  std::vector<std::size_t> range(count);
  std::iota(range.begin(), range.end(), std::size_t{0});
  return range;
}

struct AdlRange {
  int* first;
  int* last;

  friend int* begin(const AdlRange& range) { return range.first; }
  friend int* end(const AdlRange& range) { return range.last; }
};

void an_adl_range_runs_inline_and_in_parallel() {
  static_assert(std::ranges::random_access_range<AdlRange>);
  static_assert(std::ranges::sized_range<AdlRange>);
  for (const std::size_t workers : {std::size_t{0}, kWorkers}) {
    cursiv::TaskScheduler tasks{workers};
    int values[] = {1, 2, 3};
    tasks.parallelFor(cursiv::kNoCancel, AdlRange{values, values + 3},
                      [](int& value) { ++value; });
    CHECK(values[0] == 2 && values[1] == 3 && values[2] == 4);
  }
}

// The core claim: once each, none missed, none twice. A count far above the
// worker count is what makes the slicing do something.
void every_element_runs_exactly_once() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(10'000);
  std::vector<std::atomic<int>> seen(range.size());

  tasks.parallelFor(cursiv::kNoCancel, range,
                    [&](std::size_t i) { seen[i].fetch_add(1); });

  bool once = true;
  for (const auto& count : seen) once = once && count.load() == 1;
  CHECK(once);
}

// An empty range is not a degenerate slice, it is no work at all.
void an_empty_range_runs_nothing() {
  cursiv::TaskScheduler tasks{kWorkers};
  const std::vector<std::size_t> range;
  std::atomic<int> calls{0};

  tasks.parallelFor(cursiv::kNoCancel, range, [&](std::size_t) { ++calls; });

  CHECK(calls.load() == 0);
}

// Zero workers is a legitimate answer -- WASI gives it -- and it must still run
// every element, inline.
void a_pool_without_workers_still_runs_everything() {
  cursiv::TaskScheduler tasks{0};
  const auto range = indices(1'000);
  std::atomic<int> calls{0};

  tasks.parallelFor(cursiv::kNoCancel, range, [&](std::size_t) { ++calls; });

  CHECK(tasks.workerCount() == 0);
  CHECK(calls.load() == 1'000);
}

// The return is a join: no body is still running when the call returns. A
// counter incremented on entry and decremented on exit is zero only if that
// holds.
void the_call_returns_after_the_last_body() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(4'000);
  std::atomic<int> inside{0};

  tasks.parallelFor(cursiv::kNoCancel, range, [&](std::size_t) {
    inside.fetch_add(1);
    inside.fetch_sub(1);
  });

  CHECK(inside.load() == 0);
}

// The work spreads as a rendezvous, not a high-water mark, so a peak-of-one
// is the truth on a loaded machine, not a defect. The first body blocks
// until a second thread arrives, forcing that outcome; the deadline just
// keeps a pool that cannot deliver from hanging.
void the_work_spreads_across_threads() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(1'000);
  std::atomic<int> entered{0};
  std::atomic<bool> together{false};

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  tasks.parallelFor(cursiv::kNoCancel, range, [&](std::size_t) {
    if (entered.fetch_add(1) != 0) {
      together.store(true);
      return;
    }
    while (!together.load() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
  });

  CHECK(together.load());
}

void the_first_exception_is_rethrown() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(1'000);

  bool thrown = false;
  try {
    tasks.parallelFor(cursiv::kNoCancel, range, [](std::size_t i) {
      if (i == 500) throw std::runtime_error("boom");
    });
  } catch (const std::runtime_error&) {
    thrown = true;
  }
  CHECK(thrown);
}

// Cancellation is cooperative: the remaining items short-circuit rather than
// being torn down, and the call reports it.
void cancellation_short_circuits_the_rest() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(20'000);
  std::stop_source source;
  std::atomic<int> calls{0};

  bool cancelled = false;
  try {
    tasks.parallelFor(source.get_token(), range, [&](std::size_t) {
      if (calls.fetch_add(1) == 100) source.request_stop();
    });
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
}

void inline_runs_report_cancellation_from_the_last_body() {
  const auto check = [](std::size_t workers, const auto& range,
                        std::size_t minParallelCount) {
    cursiv::TaskScheduler tasks{workers};
    cursiv::CancellationTokenSource source;
    std::size_t calls = 0;
    bool cancelled = false;
    try {
      tasks.parallelFor(source.get_token(), range, [&](std::size_t) {
        if (++calls == range.size()) source.request_stop();
      }, minParallelCount);
    } catch (const cursiv::OperationCancelled&) {
      cancelled = true;
    }
    CHECK(cancelled);
    CHECK(calls == range.size());
  };

  check(0, indices(2), 2);
  check(kWorkers, indices(1), 2);
  check(kWorkers, indices(2), 3);
  check(kWorkers, std::list<std::size_t>{0, 1}, 2);
}

// Nesting must not deadlock, and the outer count is deliberately above the
// worker count so every worker is inside a body when the inner runs start.
void a_body_may_run_another_parallel_for() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto outer = indices(64);
  const auto inner = indices(64);
  std::atomic<int> calls{0};

  tasks.parallelFor(cursiv::kNoCancel, outer, [&](std::size_t) {
    tasks.parallelFor(cursiv::kNoCancel, inner, [&](std::size_t) { ++calls; });
  });

  CHECK(calls.load() == 64 * 64);
}

// One more level of nesting than WorkerPool::kCells (64): the deepest calls
// find every cell taken and fall back to running their two indices in
// series -- the tally still has to come out right.
void recurse_past_the_cell_count(cursiv::TaskScheduler& tasks,
                                 std::size_t depth, std::atomic<int>& leaves) {
  const auto pair = indices(2);
  tasks.parallelFor(cursiv::kNoCancel, pair, [&](std::size_t index) {
    if (index == 0 && depth > 0) {
      recurse_past_the_cell_count(tasks, depth - 1, leaves);
      return;
    }
    leaves.fetch_add(1, std::memory_order_relaxed);
  });
}

void a_full_pool_of_cells_still_runs_every_body() {
  constexpr std::size_t kDepth = 70;  // 71 nested runs, past kCells (64)
  cursiv::TaskScheduler tasks{kWorkers};
  std::atomic<int> leaves{0};

  recurse_past_the_cell_count(tasks, kDepth, leaves);

  CHECK(leaves.load() == static_cast<int>(kDepth) + 2);
}

// Two threads may fan out on one scheduler at once -- what a shared workspace
// slot invites.
void two_threads_share_one_scheduler() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(2'000);
  std::atomic<int> calls{0};
  const auto run = [&] {
    for (int pass = 0; pass < 5; ++pass)
      tasks.parallelFor(cursiv::kNoCancel, range,
                        [&](std::size_t) { ++calls; });
  };
  std::thread other(run);
  run();
  other.join();
  CHECK(calls.load() == 2 * 5 * 2'000);
}

// A range that is not sized and random-access still runs -- inline.
void a_list_runs_inline() {
  cursiv::TaskScheduler tasks{kWorkers};
  std::list<int> range;
  for (int i = 0; i < 100; ++i) range.push_back(i);
  std::atomic<int> calls{0};
  std::atomic<bool> offThread{false};
  tasks.parallelFor(cursiv::kNoCancel, range, [&](int) {
    ++calls;
    if (tasks.onWorkerThread()) offThread = true;
  });
  CHECK(calls.load() == 100);
  CHECK(!offThread.load());
}

// An exception outranks a pending cancellation: the throw is what comes back.
void an_exception_outranks_cancellation() {
  const auto range = indices(1'000);
  for (const std::size_t workers : {std::size_t{0}, kWorkers}) {
    cursiv::TaskScheduler tasks{workers};
    cursiv::CancellationTokenSource source;
    bool threw = false;
    try {
      tasks.parallelFor(source.get_token(), range, [&](std::size_t i) {
        if (i == 0) {
          source.request_stop();
          throw std::runtime_error("boom");
        }
      });
    } catch (const cursiv::OperationCancelled&) {
    } catch (const std::runtime_error&) {
      threw = true;
    }
    CHECK(threw);
  }
}

// A pool is reused across phases; the second run must not inherit the first's
// exhausted range.
void a_pool_serves_run_after_run() {
  cursiv::TaskScheduler tasks{kWorkers};
  const auto range = indices(500);
  std::atomic<int> calls{0};

  for (int pass = 0; pass < 20; ++pass)
    tasks.parallelFor(cursiv::kNoCancel, range, [&](std::size_t) { ++calls; });

  CHECK(calls.load() == 20 * 500);
}

}  // namespace

void run() {
  an_adl_range_runs_inline_and_in_parallel();
  every_element_runs_exactly_once();
  an_empty_range_runs_nothing();
  a_pool_without_workers_still_runs_everything();
  the_call_returns_after_the_last_body();
  the_work_spreads_across_threads();
  the_first_exception_is_rethrown();
  cancellation_short_circuits_the_rest();
  inline_runs_report_cancellation_from_the_last_body();
  a_body_may_run_another_parallel_for();
  a_full_pool_of_cells_still_runs_every_body();
  two_threads_share_one_scheduler();
  a_list_runs_inline();
  an_exception_outranks_cancellation();
  a_pool_serves_run_after_run();
}

}  // namespace task_scheduler

// Pins the handshake that lets a value be read without a lock: what is read
// is what was published, a value held by a reader is never freed under it,
// and one nobody holds does not pile up -- the two failure modes that would
// otherwise rot silently, on a loaded machine or after a day of editing.
namespace published {

namespace {

// Counts itself, and carries a value twice: `mirror` must always be `2 * seed`,
// so a reader that lands on a freed or half-built object is caught by the
// mismatch rather than by luck.
struct Payload {
  explicit Payload(int value) : seed(value), mirror(value * 2) { ++live; }
  ~Payload() {
    seed = -1;
    mirror = -1;
    --live;
  }
  Payload(const Payload&) = delete;
  Payload& operator=(const Payload&) = delete;

  [[nodiscard]] bool intact() const { return mirror == seed * 2 && seed >= 0; }

  int seed;
  int mirror;
  static std::atomic<int> live;
};

std::atomic<int> Payload::live{0};

std::shared_ptr<const Payload> payload(int value) {
  return std::make_shared<const Payload>(value);
}

void a_fresh_slot_reads_nothing() {
  const cursiv::Published<Payload> slot;
  const auto read = slot.read();
  CHECK(!read);
  CHECK(read.get() == nullptr);
}

void what_is_published_is_what_is_read() {
  cursiv::Published<Payload> slot;
  slot.publish(payload(7));

  const auto read = slot.read();
  CHECK(static_cast<bool>(read));
  CHECK(read->seed == 7);
  CHECK((*read).mirror == 14);
}

// Retiring is not freeing: a reader that took the pointer before the swap keeps
// reading the value it took.
void a_held_read_keeps_the_retired_value_alive() {
  cursiv::Published<Payload> slot;
  slot.publish(payload(1));

  const auto held = slot.read();
  const int liveBefore = Payload::live.load();
  slot.publish(payload(2));

  CHECK(held->seed == 1);                         // still the old one...
  CHECK(held->intact());                          // ...and still whole.
  CHECK(Payload::live.load() == liveBefore + 1);  // both are alive
  CHECK(slot.retainedCount() == 1);
  CHECK(slot.read()->seed == 2);  // ...while a new reader gets the new one
}

// ...and once the reader is gone, the next publication lets it go. Without
// this, an editing session would keep every snapshot it ever built.
void nothing_is_retained_once_the_readers_leave() {
  cursiv::Published<Payload> slot;
  for (int round = 0; round < 50; ++round) {
    slot.publish(payload(round));
    CHECK(slot.retainedCount() == 0);
  }
  // The live one, and nothing else.
  CHECK(Payload::live.load() == 1);
}

void a_reader_that_outlives_the_slot_is_not_the_contract() {
  {
    cursiv::Published<Payload> slot;
    slot.publish(payload(3));
    CHECK(Payload::live.load() == 1);
  }
  CHECK(Payload::live.load() == 0);
}

// The one that matters. Readers spin on `read()` while writers replace the
// value under them; a value freed too early shows as a broken payload here, and
// as a report under the sanitizers the same lane runs.
void readers_and_writers_race() {
  cursiv::Published<Payload> slot;
  slot.publish(payload(0));

  std::atomic<bool> stop{false};
  std::atomic<long> reads{0};
  std::atomic<int> broken{0};
  std::atomic<int> empty{0};

  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int reader = 0; reader < 6; ++reader)
    threads.emplace_back([&] {
      long seen = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        const auto read = slot.read();
        if (!read) {
          ++empty;
          continue;
        }
        // Read twice with the handle held: the value must not change under it.
        const int first = read->seed;
        if (!read->intact() || read->seed != first) ++broken;
        ++seen;
      }
      reads.fetch_add(seen);
    });
  for (int writer = 0; writer < 2; ++writer)
    threads.emplace_back([&] {
      for (int round = 0; !stop.load(std::memory_order_relaxed); ++round)
        slot.publish(payload(round % 1000));
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  stop.store(true);
  for (std::thread& thread : threads) thread.join();

  CHECK(broken.load() == 0);
  CHECK(empty.load() == 0);
  // The lane proves nothing if nobody actually read.
  CHECK(reads.load() > 1'000);
}

// Retiring to nothing is what an invalidation does, and a reader must see the
// absence rather than a stale value.
void retiring_leaves_nothing_to_read() {
  cursiv::Published<Payload> slot;
  slot.publish(payload(5));
  slot.retire();
  CHECK(!slot.read());
}

// A slot hammered by writers with no reader must not grow: the reclamation runs
// on the publication path, so it has to keep up on its own.
void a_write_only_slot_does_not_grow() {
  cursiv::Published<Payload> slot;
  std::atomic<bool> stop{false};
  std::vector<std::thread> writers;
  writers.reserve(4);
  for (int writer = 0; writer < 4; ++writer)
    writers.emplace_back([&] {
      for (int round = 0; !stop.load(std::memory_order_relaxed); ++round)
        slot.publish(payload(round));
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true);
  for (std::thread& writer : writers) writer.join();

  slot.publish(payload(0));
  CHECK(slot.retainedCount() == 0);
  CHECK(Payload::live.load() == 1);
}

}  // namespace

void run() {
  a_fresh_slot_reads_nothing();
  what_is_published_is_what_is_read();
  a_held_read_keeps_the_retired_value_alive();
  nothing_is_retained_once_the_readers_leave();
  a_reader_that_outlives_the_slot_is_not_the_contract();
  retiring_leaves_nothing_to_read();
  readers_and_writers_race();
  a_write_only_slot_does_not_grow();
}

}  // namespace published

// Pins the reader/writer lock: entry order, supersession against durability,
// the inline read inside a write, the gate wait a cancellation abandons --
// and the settled door an `Immediate` read comes through.
namespace workspace_lock {

namespace {

using cursiv::WorkspaceLock;

void wait_for(const std::atomic<bool>& flag) {
  while (!flag.load()) std::this_thread::yield();
}

void a_write_runs_and_a_read_sees_it() {
  cursiv::DefaultWorkspaceLock lock;
  int value = 0;
  lock.write([&](const cursiv::CancellationToken&) { value = 1; });
  bool seen = false;
  CHECK(lock.read([&] { seen = value == 1; }));
  CHECK(seen);
}

// A newer write cancels the running supersedable one and still lets it run to
// its end: entry order holds, the token is how the old one is told to hurry.
void a_newer_write_supersedes_the_running_one() {
  cursiv::DefaultWorkspaceLock lock;
  std::vector<std::string> order;
  std::atomic<bool> entered{false};
  bool sawCancel = false;

  std::jthread first([&] {
    lock.write([&](const cursiv::CancellationToken& token) {
      entered = true;
      while (!token.stop_requested()) std::this_thread::yield();
      sawCancel = true;
      order.emplace_back("first");
    });
  });
  wait_for(entered);
  // Entering `write` here cancels the previous one before waiting its turn.
  lock.write(
      [&](const cursiv::CancellationToken&) { order.emplace_back("second"); });
  first.join();
  CHECK(sawCancel);
  CHECK(order == std::vector<std::string>({"first", "second"}));
}

// `cancelWrite` on a durable write lands at its settle: the mutation is kept,
// what follows is cancelled.
void cancel_write_lands_at_a_durable_settle() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> entered{false};
  std::atomic<bool> cancelled{false};
  bool cancelledBeforeSettle = false;
  bool sawCancel = false;
  std::jthread writer([&] {
    lock.write(
        [&](const cursiv::CancellationToken& token) {
          entered = true;
          wait_for(cancelled);
          cancelledBeforeSettle = token.stop_requested();
          lock.settle();
          while (!token.stop_requested()) std::this_thread::yield();
          sawCancel = true;
        },
        WorkspaceLock::WriteMode::Durable);
  });
  wait_for(entered);
  lock.cancelWrite();
  cancelled = true;
  writer.join();
  CHECK(!cancelledBeforeSettle);
  CHECK(sawCancel);
}

// A later write does not cancel a durable one's mutation; it runs after it,
// in entry order. The pre-settle check can never fail wrongly (durability
// guarantees it whenever the later write entered in time); the post-settle
// handover is deterministic -- once the later write enters, the token ends
// up cancelled whichever side of the settle the entry landed on, so waiting
// for it always terminates and always proves it.
void a_durable_write_keeps_its_mutation() {
  cursiv::DefaultWorkspaceLock lock;
  std::vector<std::string> order;
  std::atomic<bool> entered{false};
  std::atomic<bool> posted{false};
  bool cancelledBeforeSettle = false;

  std::jthread first([&] {
    lock.write(
        [&](const cursiv::CancellationToken& token) {
          entered = true;
          wait_for(posted);
          cancelledBeforeSettle = token.stop_requested();
          lock.settle();
          while (!token.stop_requested()) std::this_thread::yield();
          order.emplace_back("first");
        },
        WorkspaceLock::WriteMode::Durable);
  });
  wait_for(entered);
  std::jthread second([&] {
    posted = true;
    lock.write([&](const cursiv::CancellationToken&) {
      order.emplace_back("second");
    });
  });
  first.join();
  second.join();
  CHECK(!cancelledBeforeSettle);
  CHECK(order == std::vector<std::string>({"first", "second"}));
}

// A read inside a write runs inline on the writer's own thread.
void a_read_inside_a_write_runs_inline() {
  cursiv::DefaultWorkspaceLock lock;
  bool inner = false;
  lock.write([&](const cursiv::CancellationToken&) {
    CHECK(lock.read([&] { inner = true; }));
  });
  CHECK(inner);
}

// A cancelled reader abandons the gate: `false`, and the action never ran.
void a_cancelled_gate_wait_returns_false() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};
  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) {
      entered = true;
      wait_for(release);
    });
  });
  wait_for(entered);
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool ran = false;
  CHECK(!lock.read([&] { ran = true; }, source.get_token()));
  CHECK(!ran);
  release = true;
}

// An `Immediate` read runs beside a settled write, which is the whole point:
// it does not wait for the write's tail. A `Normal` one in the same window
// does wait, and that difference is what is pinned.
void an_immediate_read_runs_beside_a_settled_write() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> settled{false};
  std::atomic<bool> immediateDone{false};
  std::atomic<bool> release{false};
  std::atomic<bool> normalDone{false};

  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) {
      lock.settle();
      settled = true;
      while (!release.load()) std::this_thread::yield();
    });
  });
  wait_for(settled);

  std::jthread normal([&] {
    (void)lock.read([&] {}, {}, WorkspaceLock::ReadPriority::Normal);
    normalDone = true;
  });
  CHECK(lock.read([&] {}, {}, WorkspaceLock::ReadPriority::Immediate));
  immediateDone = true;

  // The write still holds: the immediate read came through, the normal one
  // has not.
  CHECK(immediateDone.load());
  CHECK(!normalDone.load());
  release = true;
  writer.join();
  normal.join();
  CHECK(normalDone.load());
}

// ...and not before the settle. This is the half a single-threaded builder
// does not need: a
// mutation phase drops parses, and a reader admitted there would hold
// pointers into a freed arena.
void an_immediate_read_waits_out_the_mutation() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> mutating{false};
  std::atomic<bool> readEntered{false};
  std::atomic<bool> settle{false};

  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) {
      mutating = true;
      while (!settle.load()) std::this_thread::yield();
      // Nothing was let in while the mutation ran.
      CHECK(!readEntered.load());
      lock.settle();
    });
  });
  wait_for(mutating);

  std::jthread reader([&] {
    (void)lock.read([&] { readEntered = true; }, {},
                    WorkspaceLock::ReadPriority::Immediate);
  });
  // Give the reader every chance to get in early; it must not.
  for (int i = 0; i < 1000; ++i) std::this_thread::yield();
  CHECK(!readEntered.load());
  settle = true;
  reader.join();
  writer.join();
  CHECK(readEntered.load());
}

// A write drains the immediate readers before taking exclusivity: they are
// readers like any other, which is what keeps a reset off a tree one holds.
void a_write_waits_for_immediate_readers() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> reading{false};
  std::atomic<bool> release{false};
  std::atomic<bool> wrote{false};

  std::jthread reader([&] {
    (void)lock.read(
        [&] {
          reading = true;
          while (!release.load()) std::this_thread::yield();
        },
        {}, WorkspaceLock::ReadPriority::Immediate);
  });
  wait_for(reading);

  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) { wrote = true; });
  });
  for (int i = 0; i < 1000; ++i) std::this_thread::yield();
  CHECK(!wrote.load());
  release = true;
  writer.join();
  reader.join();
  CHECK(wrote.load());
}

// `settle` off the writer's own thread opens nothing: a caller with no write
// in hand cannot admit itself.
void settle_is_the_writers_alone() {
  cursiv::DefaultWorkspaceLock lock;
  std::atomic<bool> mutating{false};
  std::atomic<bool> readEntered{false};
  std::atomic<bool> release{false};

  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) {
      mutating = true;
      while (!release.load()) std::this_thread::yield();
    });
  });
  wait_for(mutating);
  lock.settle();  // not the writer's thread: a no-op

  std::jthread reader([&] {
    (void)lock.read([&] { readEntered = true; }, {},
                    WorkspaceLock::ReadPriority::Immediate);
  });
  for (int i = 0; i < 1000; ++i) std::this_thread::yield();
  CHECK(!readEntered.load());
  release = true;
  writer.join();
  reader.join();
  CHECK(readEntered.load());
}

// `settled` is the query a mutation asserts against: false with no write in
// hand, false through the mutation, true from the settle to the end of the
// action, and false again after -- so a build outside any lock reads the same
// answer as one whose write has not settled yet.
void settled_says_when_a_mutation_may_not_run() {
  cursiv::DefaultWorkspaceLock lock;
  CHECK(!lock.settled());

  std::atomic<bool> mutating{false};
  std::atomic<bool> go{false};
  std::atomic<bool> release{false};
  bool duringMutation = true;
  bool afterSettle = false;

  std::jthread writer([&] {
    lock.write([&](const cursiv::CancellationToken&) {
      mutating = true;
      wait_for(go);
      duringMutation = lock.settled();
      lock.settle();
      afterSettle = lock.settled();
      wait_for(release);
    });
  });
  wait_for(mutating);
  // Asked from outside the writer too: the answer is the serving write's, not
  // the asker's.
  CHECK(!lock.settled());
  go = true;
  while (!lock.settled()) std::this_thread::yield();
  release = true;
  writer.join();

  CHECK(!duringMutation);
  CHECK(afterSettle);
  CHECK(!lock.settled());
}

// The declared contract: an escaping `OperationCancelled` is absorbed, any
// other exception propagates once exclusivity is released.
void write_absorbs_a_cancellation_and_propagates_anything_else() {
  cursiv::DefaultWorkspaceLock lock;
  bool ran = false;
  lock.write([&](const cursiv::CancellationToken&) {
    ran = true;
    throw cursiv::OperationCancelled();
  });
  CHECK(ran);  // absorbed: nothing escaped

  bool thrown = false;
  try {
    lock.write([&](const cursiv::CancellationToken&) {
      throw std::runtime_error("boom");
    });
  } catch (const std::runtime_error& e) {
    thrown = std::string_view(e.what()) == "boom";
  }
  CHECK(thrown);

  // Exclusivity was released both times: a write after either runs normally.
  int value = 0;
  lock.write([&](const cursiv::CancellationToken&) { value = 1; });
  CHECK(value == 1);
}

}  // namespace

void run() {
  a_write_runs_and_a_read_sees_it();
  a_newer_write_supersedes_the_running_one();
  cancel_write_lands_at_a_durable_settle();
  a_durable_write_keeps_its_mutation();
  a_read_inside_a_write_runs_inline();
  a_cancelled_gate_wait_returns_false();
  an_immediate_read_runs_beside_a_settled_write();
  an_immediate_read_waits_out_the_mutation();
  a_write_waits_for_immediate_readers();
  settle_is_the_writers_alone();
  settled_says_when_a_mutation_may_not_run();
  write_absorbs_a_cancellation_and_propagates_anything_else();
}

}  // namespace workspace_lock

namespace imported_names {

namespace {

void zero_and_colliding_hashes_are_kept() {
  cursiv::ImportedNames names;
  CHECK(names.snapshot().empty());
  names.record(0);
  names.record(0);
  CHECK(names.snapshot() == std::vector<std::uint64_t>{0});
  for (const std::uint64_t hash : {1, 9, 17, 1, 9, 0, 17, 1})
    names.record(hash);
  const std::vector<std::uint64_t> expected{0, 1, 9, 17};
  CHECK(names.snapshot() == expected);
  names.record(9);
  names.record(1);
  CHECK(names.snapshot() == expected);
}

void clear_forgets_cached_hashes() {
  cursiv::ImportedNames names;
  for (std::uint64_t hash = 0; hash < 32; ++hash) names.record(hash);
  CHECK(names.snapshot().size() == 32);
  names.clear();
  CHECK(names.snapshot().empty());
  for (std::uint64_t hash = 24; hash < 32; ++hash) names.record(hash);
  names.record(0);
  CHECK(names.snapshot() ==
        std::vector<std::uint64_t>{0, 24, 25, 26, 27, 28, 29, 30, 31});
  names.clear();
  names.record(0);
  names.record(31);
  CHECK(names.snapshot() == std::vector<std::uint64_t>{0, 31});
}

void concurrent_recording_and_snapshots_keep_every_name() {
  cursiv::ImportedNames names;
  std::barrier start(5);
  std::atomic<unsigned> finished{0};
  std::vector<std::jthread> writers;
  for (unsigned writer = 0; writer < 4; ++writer)
    writers.emplace_back([&, writer] {
      start.arrive_and_wait();
      for (unsigned round = 0; round < 16; ++round)
        for (std::uint64_t hash = 0; hash < 128; ++hash) {
          const auto value = (hash + writer * 17) % 128;
          names.record(value);
          names.record(value);
        }
      ++finished;
    });
  start.arrive_and_wait();
  std::vector<std::uint64_t> previous;
  do {
    auto current = names.snapshot();
    CHECK(std::ranges::is_sorted(current));
    CHECK(std::ranges::adjacent_find(current) == current.end());
    CHECK(std::ranges::includes(current, previous));
    CHECK(current.empty() || current.back() < 128);
    previous = std::move(current);
  } while (finished.load() != 4);
  writers.clear();
  std::vector<std::uint64_t> expected(128);
  std::iota(expected.begin(), expected.end(), std::uint64_t{0});
  CHECK(names.snapshot() == expected);
}

}  // namespace

void run() {
  zero_and_colliding_hashes_are_kept();
  clear_forgets_cached_hashes();
  concurrent_recording_and_snapshots_keep_every_name();
}

}  // namespace imported_names

// Pins that the grammar dumps may run on several threads at once: each reads
// an image of its own, which fills itself as the printer reads it.
namespace grammar_dump {

namespace {

struct Item : cursiv::Extends<cursiv::AstNode> {};

// Each rule calls the next, so a dump lays one rule after another.
struct Chain : cursiv::GrammarSpec {
  template <int I>
  struct R : rule<ref<R<I - 1>>> {};
  using entry = R<24>;
};

template <>
struct Chain::R<0> : Chain::rule<Chain::create<Item>(Chain::kw<"item">)> {};

void concurrent_dumps_read_the_same_grammar() {
  constexpr std::size_t kThreads = 8;
  std::vector<std::string> dumps(kThreads);
  std::barrier start(static_cast<std::ptrdiff_t>(kThreads));
  {
    std::vector<std::jthread> threads;
    for (std::size_t t = 0; t < kThreads; ++t)
      threads.emplace_back([&, t] {
        start.arrive_and_wait();
        for (int n = 0; n < 10; ++n) dumps[t] = cursiv::dump_declared<Chain>();
      });
  }
  const std::string expected = cursiv::dump_declared<Chain>();
  CHECK(expected.find("item") != std::string::npos);
  for (const std::string& dump : dumps) CHECK(dump == expected);
}

}  // namespace

void run() { concurrent_dumps_read_the_same_grammar(); }

}  // namespace grammar_dump

int main() {
  task_scheduler::run();
  published::run();
  workspace_lock::run();
  imported_names::run();
  grammar_dump::run();
  return cursiv::test::report("threads");
}
