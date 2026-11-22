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

// Every running thread of the process passes a full fence: what a reader
// published with a plain store is then visible, and what it loads next sees
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
  // Oldest first: each is released under the lock with a later epoch, so
  // what may be freed is always a prefix, and a reader that stays long
  // costs a collection a look at the front, not a pass over the whole list.
  PendingFree* oldest = nullptr;
  PendingFree* newest = nullptr;
  // What was released below this epoch came before a finished fence: any
  // later scan judges it without another.
  std::uint64_t fencedBelow = 0;

  static Slot& slot_of(std::atomic<std::uint64_t>* epoch) noexcept {
    return *reinterpret_cast<Slot*>(reinterpret_cast<char*>(epoch) -
                                    offsetof(Slot, epoch));
  }

  // The oldest epoch a reader publishes; none while a reader without a slot
  // reads.
  [[nodiscard]] std::uint64_t floor() const noexcept {
    std::uint64_t floor = kIdle;
    const std::size_t end = used.load(std::memory_order_seq_cst);
    for (std::size_t at = 0; at < end; ++at)
      floor = std::min(floor, slots[at].epoch.load(std::memory_order_seq_cst));
    // A reader without a slot is counted here, or loads a table after this
    // load, in that order, and so finds none of what is released.
    return unslotted.load(std::memory_order_seq_cst) != 0 ? 0 : floor;
  }

  // Under the lock: takes off the list what was released before the oldest
  // epoch a reader still publishes, and returns it, oldest first. The
  // caller frees it once the lock is gone. It fences only when a release no
  // fence has covered may be free.
  PendingFree* collectLocked() noexcept {
    if (oldest == nullptr) return nullptr;
    std::uint64_t below = floor();
    if (fenced) {
      if (below > fencedBelow && newest->epoch >= fencedBelow) {
        const std::uint64_t now = current.load(std::memory_order_relaxed);
        if (fence_every_thread()) {
          fencedBelow = now;
          below = floor();
        }
      }
      below = std::min(below, fencedBelow);
    }
    if (oldest->epoch >= below) return nullptr;
    PendingFree* const due = oldest;
    PendingFree* last = due;
    while (last->next != nullptr && last->next->epoch < below)
      last = last->next;
    oldest = last->next;
    if (oldest == nullptr) newest = nullptr;
    last->next = nullptr;
    return due;
  }

  // Each record lives in what it frees: the next one is read first.
  static void freeAll(PendingFree* due) noexcept {
    while (due != nullptr) {
      PendingFree* const next = due->next;
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
  static auto* const s = new (storage) State;
  return *s;
}

const bool TableEpochs::asymmetric = fenced;
constinit thread_local TableEpochs::Reader TableEpochs::reader{};

// Gives the thread's slot back as the thread ends, and marks it as ending: a
// guard taken later, from another thread-local destructor, holds a slot for
// its stay alone.
struct TableEpochs::Release {
  bool held = false;
  ~Release() {
    Reader& s = reader;
    // A guard lasts one call of a table, and thread-local destructors do not
    // nest: none is alive here.
    assert(s.depth == 0);
    s.onLeave |= kGiveBack;
    if (held && s.slot != nullptr) giveBack(s);
  }
};
thread_local TableEpochs::Release TableEpochs::release;

std::atomic<std::uint64_t>* TableEpochs::takeSlot() noexcept {
  State& s = state();
  if (s.held.load(std::memory_order_relaxed) >= kSlots) return nullptr;
  for (std::size_t at = 0; at < kSlots; ++at) {
    if (bool taken = false; !s.slots[at].taken.compare_exchange_strong(
            taken, true, std::memory_order_acq_rel))
      continue;
    s.held.fetch_add(1, std::memory_order_relaxed);
    // Sequentially consistent, as the scan's read of it: a scan that
    // misses this slot precedes, in that order, the epoch store it would have
    // read, and so the loads that follow it.
    std::size_t used = s.used.load(std::memory_order_seq_cst);
    while (used < at + 1 && !s.used.compare_exchange_weak(
                                used, at + 1, std::memory_order_seq_cst)) {
    }
    // Past its thread-local destructors `release` is gone and would not
    // give a slot back: the guard does, as it leaves.
    if ((reader.onLeave & kGiveBack) == 0) release.held = true;
    return &s.slots[at].epoch;
  }
  return nullptr;
}

void TableEpochs::giveBack(Reader& s) noexcept {
  State::slot_of(s.slot).taken.store(false, std::memory_order_release);
  state().held.fetch_sub(1, std::memory_order_relaxed);
  s.slot = nullptr;
}

// The collection is taken off first: a value it frees may read and release
// in turn.
void TableEpochs::leave(Reader& s) noexcept {
  if ((s.onLeave & kGiveBack) != 0 && s.slot != nullptr) giveBack(s);
  if ((s.onLeave & kCollect) != 0) {
    s.onLeave &= static_cast<std::uint8_t>(~kCollect);
    collect();
  }
}

// Counted before the reader loads a table, sequentially consistent as
// those loads are. The count falls with release, so the reader's loads happen
// before any free that counts no reader.
void TableEpochs::enterWithoutSlot() noexcept {
  state().unslotted.fetch_add(1, std::memory_order_seq_cst);
}

void TableEpochs::leaveWithoutSlot() noexcept {
  state().unslotted.fetch_sub(1, std::memory_order_release);
  if (Reader& s = reader; s.onLeave != 0) leave(s);
}

void TableEpochs::freeLater(PendingFree& record, void* object,
                            void (*free)(void*)) noexcept {
  State& s = state();
  // This thread's own guard may hold `object`: its outermost one collects.
  Reader& r = reader;
  assert(r.depth != 0);
  r.onLeave |= kCollect;
  const std::scoped_lock lock(s.mutex);
  // The epoch moves on: a reader that reads the new one entered after
  // `object` left its reach, and its acquire makes that visible.
  const std::uint64_t now = current.fetch_add(1, std::memory_order_seq_cst);
  record = PendingFree{nullptr, object, free, now};
  if (s.newest != nullptr)
    s.newest->next = &record;
  else
    s.oldest = &record;
  s.newest = &record;
}

void TableEpochs::collect() noexcept {
  State& s = state();
  PendingFree* due = nullptr;
  {
    const std::scoped_lock lock(s.mutex);
    due = s.collectLocked();
  }
  // Freed outside the lock: a value's destructor may release in turn.
  State::freeAll(due);
}

}  // namespace cursiv::detail
