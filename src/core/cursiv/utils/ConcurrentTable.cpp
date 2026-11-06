#include "cursiv/utils/ConcurrentTable.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <linux/membarrier.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace cursiv::detail {

namespace {

// Enough for every thread a process runs at once; past it a thread waits for
// one to be given back.
constexpr std::size_t kSlots = 1024;

// Retirements between two collections: a collection fences every thread,
// which costs microseconds, so a few retirements share one. Fewer when they
// hold a lot: a few large tables do not wait for dozens of small ones.
constexpr std::size_t kCollectEvery = 32;
constexpr std::size_t kCollectBytes = std::size_t{8} << 20;

// What a slot holds while its thread reads nothing: every bit set, as a
// guard stores on leaving.
constexpr std::uint64_t kIdle = ~std::uint64_t{0};

struct alignas(64) Slot {
  std::atomic<std::uint64_t> epoch{kIdle};
  std::atomic<bool> taken{false};
};

struct Retired {
  void* object;
  void (*free)(void*);
  std::uint64_t epoch;
};

struct State {
  std::array<Slot, kSlots> slots;
  // Past the last slot ever taken: a scan stops there.
  std::atomic<std::size_t> used{0};
  std::mutex mutex;
  // Oldest first: each is retired under the lock with a later epoch, so
  // what may be freed is always a prefix, and a reader that stays long
  // costs a collection a look at the front, not a pass over the whole list.
  std::deque<Retired> retired;
  // Retirements since the last collection, and what they hold: what spaces
  // collections out, not the length of the list, which a long reader keeps
  // long.
  std::size_t sinceCollection = 0;
  std::size_t bytesSinceCollection = 0;
};

// Never destroyed: a thread that ends after static destruction still gives
// its slot back to live memory.
State& state() {
  static State* const s = new State;
  return *s;
}

Slot& slot_of(std::atomic<std::uint64_t>* epoch) {
  return *reinterpret_cast<Slot*>(reinterpret_cast<char*>(epoch) -
                                  offsetof(Slot, epoch));
}

// Every running thread of the process passes a full fence: what a reader
// announced with a plain store is then visible, and what it loads next sees
// what was taken out of reach before.
bool register_fence() noexcept {
#if defined(__linux__) && defined(SYS_membarrier)
  return syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0,
                 0) == 0;
#else
  return false;
#endif
}

// False when the system refused: nothing may then be freed on its strength.
bool fence_every_thread() noexcept {
#if defined(__linux__) && defined(SYS_membarrier)
  return syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) == 0;
#else
  return false;
#endif
}

const bool fenced = register_fence();

// Under the lock: moves into `due` what was retired before the oldest epoch
// a reader still announces. The caller frees it once the lock is gone.
void collect_locked(State& s, std::vector<Retired>& due) noexcept {
  s.sinceCollection = 0;
  s.bytesSinceCollection = 0;
  if (s.retired.empty()) return;
  if (fenced && !fence_every_thread()) return;
  std::uint64_t oldest = kIdle;
  const std::size_t used = s.used.load(std::memory_order_seq_cst);
  for (std::size_t at = 0; at < used; ++at)
    oldest =
        std::min(oldest, s.slots[at].epoch.load(std::memory_order_seq_cst));
  auto kept = s.retired.begin();
  while (kept != s.retired.end() && kept->epoch < oldest) ++kept;
  // Copied before it leaves the list: a copy refused for want of memory
  // leaves everything retired, for the next collection to free.
  try {
    due.assign(s.retired.begin(), kept);
  } catch (const std::bad_alloc&) {
    return;
  }
  s.retired.erase(s.retired.begin(), kept);
}

}  // namespace

const bool TableEpochs::asymmetric = fenced;
constinit thread_local TableEpochs::Seat TableEpochs::seat{};

// Gives the thread's slot back as the thread ends, and marks it as ending: a
// guard taken later, from another thread-local destructor, holds a slot for
// its stay alone.
struct TableEpochs::Release {
  bool held = false;
  ~Release() {
    Seat& s = seat;
    // A guard lasts one call of a table, and thread-local destructors do not
    // nest: none is alive here.
    assert(s.depth == 0);
    s.exiting = true;
    if (held && s.slot != nullptr) giveBack(s);
  }
};
thread_local TableEpochs::Release TableEpochs::release;

std::atomic<std::uint64_t>* TableEpochs::takeSlot() {
  State& s = state();
  for (;;) {
    for (std::size_t at = 0; at < kSlots; ++at) {
      bool taken = false;
      if (!s.slots[at].taken.compare_exchange_strong(taken, true,
                                                     std::memory_order_acq_rel))
        continue;
      // Sequentially consistent, as the scan's read of it: a scan that
      // misses this slot precedes, in that order, the announcement it would
      // have read, and so the loads that follow it.
      std::size_t used = s.used.load(std::memory_order_seq_cst);
      while (used < at + 1 && !s.used.compare_exchange_weak(
                                  used, at + 1, std::memory_order_seq_cst)) {
      }
      // Past its thread-local destructors `release` is gone and would not
      // give a slot back: the guard does, as it leaves.
      if (!seat.exiting) release.held = true;
      return &s.slots[at].epoch;
    }
    std::this_thread::yield();
  }
}

void TableEpochs::giveBack(Seat& s) noexcept {
  slot_of(s.slot).taken.store(false, std::memory_order_release);
  s.slot = nullptr;
}

void TableEpochs::retire(void* object, void (*free)(void*), std::size_t bytes) {
  State& s = state();
  std::vector<Retired> due;
  {
    const std::scoped_lock lock(s.mutex);
    // The epoch moves on: a reader that reads the new one entered after
    // `object` left its reach, and its acquire makes that visible.
    const std::uint64_t now = current.fetch_add(1, std::memory_order_seq_cst);
    s.retired.push_back(Retired{object, free, now});
    s.bytesSinceCollection += bytes;
    if (++s.sinceCollection >= kCollectEvery ||
        s.bytesSinceCollection >= kCollectBytes)
      collect_locked(s, due);
  }
  // Freed outside the lock: a value's destructor may retire in turn.
  for (const Retired& r : due) r.free(r.object);
}

void TableEpochs::collect() noexcept {
  State& s = state();
  std::vector<Retired> due;
  {
    const std::scoped_lock lock(s.mutex);
    // Moved on as a retirement does: what was retired last is older than
    // what any reader announces from now.
    current.fetch_add(1, std::memory_order_seq_cst);
    collect_locked(s, due);
  }
  for (const Retired& r : due) r.free(r.object);
}

void collect_retired_tables() noexcept { TableEpochs::collect(); }

}  // namespace cursiv::detail
