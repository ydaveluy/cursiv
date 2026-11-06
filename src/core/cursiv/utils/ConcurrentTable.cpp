#include "cursiv/utils/ConcurrentTable.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <new>

#if defined(__linux__)
#include <linux/membarrier.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace cursiv::detail {

namespace {

// Enough for every thread a process runs at once; past it a thread reads
// without one, and holds back every collection while it does.
constexpr std::size_t kSlots = 1024;

// Retirements between two collections: a collection fences every thread,
// which costs microseconds, so a few retirements share one. Fewer when they
// hold a lot: a few large tables do not wait for dozens of small ones.
constexpr std::size_t kCollectEvery = 32;
constexpr std::size_t kCollectBytes = std::size_t{8} << 20;

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

}  // namespace

struct TableEpochs::State {
  struct alignas(64) Slot {
    std::atomic<std::uint64_t> epoch{kIdle};
    std::atomic<bool> taken{false};
  };

  std::array<Slot, kSlots> slots;
  // Past the last slot ever taken: a scan stops there.
  std::atomic<std::size_t> used{0};
  // Slots taken now: a thread that would find none looks no further.
  std::atomic<std::size_t> held{0};
  // Readers without a slot, reading now: a collection frees nothing while
  // there is one.
  std::atomic<std::size_t> unslotted{0};
  std::mutex mutex;
  // Oldest first: each is retired under the lock with a later epoch, so
  // what may be freed is always a prefix, and a reader that stays long
  // costs a collection a look at the front, not a pass over the whole list.
  Retired* oldest = nullptr;
  Retired* newest = nullptr;
  // Retirements since the last collection, and what they hold: what spaces
  // collections out, not the length of the list, which a long reader keeps
  // long.
  std::size_t sinceCollection = 0;
  std::size_t bytesSinceCollection = 0;

  static Slot& slot_of(std::atomic<std::uint64_t>* epoch) noexcept {
    return *reinterpret_cast<Slot*>(reinterpret_cast<char*>(epoch) -
                                    offsetof(Slot, epoch));
  }

  // Under the lock: takes off the list what was retired before the oldest
  // epoch a reader still announces, and returns it, oldest first. The
  // caller frees it once the lock is gone.
  Retired* collectLocked() noexcept {
    sinceCollection = 0;
    bytesSinceCollection = 0;
    if (oldest == nullptr) return nullptr;
    if (fenced && !fence_every_thread()) return nullptr;
    std::uint64_t floor = kIdle;
    const std::size_t end = used.load(std::memory_order_seq_cst);
    for (std::size_t at = 0; at < end; ++at)
      floor = std::min(floor, slots[at].epoch.load(std::memory_order_seq_cst));
    // A reader without a slot is counted here, or loads a table after this
    // load, in that order, and so finds none of what is retired.
    if (unslotted.load(std::memory_order_seq_cst) != 0) return nullptr;
    if (oldest->epoch >= floor) return nullptr;
    Retired* const due = oldest;
    Retired* last = due;
    while (last->next != nullptr && last->next->epoch < floor)
      last = last->next;
    oldest = last->next;
    if (oldest == nullptr) newest = nullptr;
    last->next = nullptr;
    return due;
  }

  // Each record lives in what it frees: the next one is read first.
  static void freeAll(Retired* due) noexcept {
    while (due != nullptr) {
      Retired* const next = due->next;
      due->free(due->object);
      due = next;
    }
  }
};

// Built in place and never destroyed: the first reader asks for no memory,
// and a thread that ends after static destruction still gives its slot back
// to live memory.
TableEpochs::State& TableEpochs::state() noexcept {
  alignas(State) static unsigned char storage[sizeof(State)];
  static State* const s = new (storage) State;
  return *s;
}

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

std::atomic<std::uint64_t>* TableEpochs::takeSlot() noexcept {
  State& s = state();
  if (s.held.load(std::memory_order_relaxed) >= kSlots) return nullptr;
  for (std::size_t at = 0; at < kSlots; ++at) {
    bool taken = false;
    if (!s.slots[at].taken.compare_exchange_strong(taken, true,
                                                   std::memory_order_acq_rel))
      continue;
    s.held.fetch_add(1, std::memory_order_relaxed);
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
  return nullptr;
}

void TableEpochs::giveBack(Seat& s) noexcept {
  State::slot_of(s.slot).taken.store(false, std::memory_order_release);
  state().held.fetch_sub(1, std::memory_order_relaxed);
  s.slot = nullptr;
}

// Counted before the reader loads a table, and sequentially consistent as
// those loads are: a collection that does not count this reader comes
// before them in that order. The count falls with release, so what the
// reader loaded happens before a free that counts no reader.
void TableEpochs::enterWithoutSlot() noexcept {
  state().unslotted.fetch_add(1, std::memory_order_seq_cst);
}

void TableEpochs::leaveWithoutSlot() noexcept {
  state().unslotted.fetch_sub(1, std::memory_order_release);
}

void TableEpochs::retire(Retired& record, void* object, void (*free)(void*),
                         std::size_t bytes) noexcept {
  State& s = state();
  Retired* due = nullptr;
  {
    const std::scoped_lock lock(s.mutex);
    // The epoch moves on: a reader that reads the new one entered after
    // `object` left its reach, and its acquire makes that visible.
    const std::uint64_t now = current.fetch_add(1, std::memory_order_seq_cst);
    record = Retired{nullptr, object, free, now};
    if (s.newest != nullptr)
      s.newest->next = &record;
    else
      s.oldest = &record;
    s.newest = &record;
    s.bytesSinceCollection += bytes;
    if (++s.sinceCollection >= kCollectEvery ||
        s.bytesSinceCollection >= kCollectBytes)
      due = s.collectLocked();
  }
  // Freed outside the lock: a value's destructor may retire in turn.
  State::freeAll(due);
}

void TableEpochs::collect() noexcept {
  State& s = state();
  Retired* due = nullptr;
  {
    const std::scoped_lock lock(s.mutex);
    // Moved on as a retirement does: what was retired last is older than
    // what any reader announces from now.
    current.fetch_add(1, std::memory_order_seq_cst);
    due = s.collectLocked();
  }
  State::freeAll(due);
}

void collect_retired_tables() noexcept { TableEpochs::collect(); }

}  // namespace cursiv::detail
