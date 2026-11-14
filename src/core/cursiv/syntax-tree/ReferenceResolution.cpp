#include "cursiv/syntax-tree/AstNode.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>
#include <vector>

#include "cursiv/syntax-tree/DefaultDocumentReferences.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::detail {
namespace {

static_assert(public_ref_state(kRefObserved) == RefState::Resolving);
static_assert(static_cast<unsigned>(RefState::ErrorThrown) < 0x80);

// Nested resolutions are bounded by stretches of native stack of this size:
// see `resolve_far`.
constexpr unsigned kStretchBits = 15;

// The lookup of a slow path: one answer goes to `target` and `which`, several
// go to `items`.
CURSIV_ALWAYS_INLINE inline RefState look_up(const RefResolver* resolver,
                                             std::uint32_t handle,
                                             AstNode** target,
                                             std::uint8_t* which,
                                             MultiRefItems* items) {
  if (items != nullptr) return resolver->resolveAll(handle, *items);
  const auto answer = resolver->resolve(handle);
  *target = answer.target;
  *which = answer.which;
  return answer.state;
}

CURSIV_ALWAYS_INLINE inline void forget(AstNode** target, std::uint8_t* which,
                                        MultiRefItems* items) noexcept {
  if (items != nullptr) {
    items->clear();
  } else {
    *target = nullptr;
    *which = 0;
  }
}

// What a slow path was called with, kept to make the same call again.
struct Asked {
  RefState* state;
  const RefResolver* resolver;
  std::uint32_t handle;
  AstNode** target;
  std::uint8_t* which;
  MultiRefItems* items;

  RefState again() const {
    return items != nullptr
               ? resolve_all_slow(*state, resolver, handle, *items)
               : resolve_one_slow(*state, resolver, handle, *target, *which);
  }
};

// A nested resolution given up for want of stack. It leaves as a cancelled one
// does, and the resolution that drives its nest asks it again.
struct PutOff final : OperationCancelled {
  explicit PutOff(const Asked& asked) : asked(asked) {}
  Asked asked;
};

// What a driving resolution was handed back: `waiting` is asked again newest
// first, and no reference in `refs` is put off a second time.
struct PutOffs {
  std::vector<Asked> waiting;
  std::vector<const void*> refs;

  void take(const Asked& asked) {
    refs.push_back(asked.state);
    waiting.push_back(asked);
  }
  void ask() {
    while (!waiting.empty()) {
      (void)waiting.back().again();
      waiting.pop_back();
    }
  }
};

// One resolution in flight, or the root of a phase. Linked by `prev`, a
// thread's frames are what it is computing, newest first; another thread reads
// them only while their owner waits.
struct Frame {
  // The reference's state byte, unique per reference; null for a root.
  const void* ref = nullptr;
  // A root's: the phase's token, found by any wait above it.
  const CancellationToken* cancelToken = nullptr;
  Frame* prev = nullptr;
  // A driving resolution's: what its nest put off.
  const PutOffs* putOffs = nullptr;
  // Set by whichever thread finds the cycle this resolution stands in.
  std::atomic<bool> cycle{false};
};

struct Wait {
  RefState& target;
  Frame* chain;
  const CancellationToken& token;
  std::condition_variable wake;
  Wait* next = nullptr;
  // Scratch for a cycle search, protected by the coordinator mutex.
  Wait* parent = nullptr;
  Wait* todo = nullptr;
  bool seen = false;
};

struct Waits {
  std::mutex mutex;
  Wait* head = nullptr;
};

Waits& waits() {
  static Waits shared;
  return shared;
}

thread_local Frame* newest = nullptr;

class Filed {
 public:
  explicit Filed(Frame& frame) noexcept : _frame(frame) {
    frame.prev = newest;
    newest = &frame;
  }
  ~Filed() {
    assert(newest == &_frame &&
           "a resolution ends on the thread that began it, innermost first");
    newest = _frame.prev;
  }
  Filed(const Filed&) = delete;
  Filed& operator=(const Filed&) = delete;

 private:
  Frame& _frame;
};

const Frame* resolving_frame(const Frame* chain, const void* ref) noexcept {
  for (const Frame* f = chain; f != nullptr; f = f->prev)
    if (f->ref == ref) return f;
  return nullptr;
}

void mark_cycle(Frame* chain, const Frame* end) noexcept {
  for (auto* frame = chain; frame != nullptr; frame = frame->prev) {
    frame->cycle.store(true, std::memory_order_relaxed);
    if (frame == end) break;
  }
}

bool closes_cycle(Waits& shared, Wait& request) noexcept {
  for (auto* w = shared.head; w != nullptr; w = w->next) w->seen = false;
  Wait* todo = &request;
  while (todo != nullptr) {
    Wait* const current = todo;
    todo = current->todo;
    if (current->token.stop_requested() ||
        !ref_running(std::atomic_ref<RefState>(current->target)
                         .load(std::memory_order_acquire)))
      continue;
    if (const auto* end = resolving_frame(request.chain, &current->target)) {
      mark_cycle(request.chain, end);
      for (auto* w = current; w != &request; w = w->parent)
        mark_cycle(w->chain, resolving_frame(w->chain, &w->parent->target));
      return true;
    }
    for (auto* w = shared.head; w != nullptr; w = w->next) {
      if (w->seen || resolving_frame(w->chain, &current->target) == nullptr)
        continue;
      w->seen = true;
      w->parent = current;
      w->todo = todo;
      todo = w;
    }
  }
  return false;
}

const CancellationToken& cancellation(const Frame* chain) noexcept {
  for (auto* frame = chain; frame != nullptr; frame = frame->prev)
    if (frame->cancelToken != nullptr) return *frame->cancelToken;
  return kNoCancel;
}

RefState wait_for_reference(RefState& state) {
  Frame* const chain = newest;
  if (const auto* mine = resolving_frame(chain, &state)) {
    mark_cycle(chain, mine);
    return RefState::ErrorCycle;
  }

  auto& shared = waits();
  Wait request{state, chain, cancellation(chain), {}};
  // Construct before locking: an already cancelled token invokes this inline.
  // Destruction joins any callback after the unique_lock below has unlocked,
  // and before the condition variable is destroyed.
  std::stop_callback cancelled(request.token, [&] {
    const std::scoped_lock lock(shared.mutex);
    request.wake.notify_one();
  });
  std::unique_lock lock(shared.mutex);
  throw_if_cancelled(request.token);
  const std::atomic_ref st(state);
  if (RefState expected = RefState::Resolving;
      !st.compare_exchange_strong(expected, kRefObserved,
                                  std::memory_order_acq_rel,
                                  std::memory_order_acquire) &&
      expected != kRefObserved)
    return expected;

  if (closes_cycle(shared, request)) return RefState::ErrorCycle;
  request.next = shared.head;
  shared.head = &request;
  struct Unlink {
    Waits& shared;
    Wait& request;
    ~Unlink() {
      auto** at = &shared.head;
      while (*at != &request) at = &(*at)->next;
      *at = request.next;
    }
  } unlink{shared, request};
  // Cancellation can publish Unresolved and another reader can immediately
  // claim the next computation. Running without the observed bit must leave
  // this wait too, so its next pass registers itself with that new producer.
  request.wake.wait(lock, [&] {
    return request.token.stop_requested() ||
           st.load(std::memory_order_acquire) != kRefObserved;
  });
  throw_if_cancelled(request.token);
  return st.load(std::memory_order_acquire);
}

void publish(RefState& state, RefState result) noexcept {
  const std::atomic_ref st(state);
  if (RefState expected = RefState::Resolving;
      st.compare_exchange_strong(expected, result, std::memory_order_release,
                                 std::memory_order_relaxed))
    return;
  // A reader registered itself first. Publishing under the same lock makes
  // both the state check before sleep and the cycle search observe one order.
  auto& shared = waits();
  const std::scoped_lock lock(shared.mutex);
  st.store(result, std::memory_order_release);
  for (auto* w = shared.head; w != nullptr; w = w->next)
    if (&w->target == &state) w->wake.notify_one();
}

// Computes under `frame`: the answer published, or what leaves cleared and
// published.
template <class Compute, class Clear>
CURSIV_ALWAYS_INLINE inline RefState settle(RefState& state, const Frame& frame,
                                            Compute compute, Clear clear) {
  try {
    RefState result = compute();
    if (frame.cycle.load(std::memory_order_relaxed) && has_error(result))
      result = RefState::ErrorCycle;
    publish(state, result);
    return result;
  } catch (const OperationCancelled&) {
    clear();
    publish(state, RefState::Unresolved);
    throw;
  } catch (...) {
    clear();
    publish(state, frame.cycle.load(std::memory_order_relaxed)
                       ? RefState::ErrorCycle
                       : RefState::ErrorThrown);
    throw;
  }
}

// A nested resolution that starts in another stretch of native stack than the
// one before it. The first of a nest drives: later ones are put off to it and
// asked again from there, so a nest stays within about two stretches.
CURSIV_COLD CURSIV_NOINLINE RefState resolve_far(Frame& frame,
                                                 const Asked& asked) {
  RefState& state = *asked.state;
  const auto compute = [&] {
    return look_up(asked.resolver, asked.handle, asked.target, asked.which,
                   asked.items);
  };
  const auto clear = [&]() noexcept {
    forget(asked.target, asked.which, asked.items);
  };
  for (const Frame* f = frame.prev; f != nullptr && f->ref != nullptr;
       f = f->prev) {
    if (f->putOffs == nullptr) continue;
    const std::vector<const void*>& refs = f->putOffs->refs;
    if (std::ranges::find(refs, frame.ref) != refs.end())
      return settle(state, frame, compute, clear);
    publish(state, RefState::Unresolved);
    throw PutOff(asked);
  }
  PutOffs putOffs;
  frame.putOffs = &putOffs;
  return settle(
      state, frame,
      [&] {
        for (;;) {
          try {
            putOffs.ask();
            return compute();
          } catch (const PutOff& putOff) {
            clear();
            putOffs.take(putOff.asked);
          }
        }
      },
      clear);
}

// `compute` and `clear` are the lookup as the usual path inlines it; the
// values after them are the same lookup as a far resolution keeps it.
template <class Compute, class Clear>
RefState resolve_guarded(RefState& state, Compute compute, Clear clear,
                         const RefResolver* resolver, std::uint32_t handle,
                         AstNode** target, std::uint8_t* which,
                         MultiRefItems* items) {
  const std::atomic_ref st(state);
  RefState current = st.load(std::memory_order_acquire);
  for (;;) {
    if (ref_running(current)) {
      current = wait_for_reference(state);
      continue;
    }
    if (current != RefState::Unresolved) return current;
    if (st.compare_exchange_strong(current, RefState::Resolving,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire))
      break;
  }
  Frame frame{.ref = &state};
  const Filed filed(frame);
  if (((reinterpret_cast<std::uintptr_t>(&frame) ^
        reinterpret_cast<std::uintptr_t>(frame.prev)) >>
       kStretchBits) != 0 &&
      frame.prev != nullptr && frame.prev->ref != nullptr) [[unlikely]]
    return resolve_far(frame, {&state, resolver, handle, target, which, items});
  return settle(state, frame, compute, clear);
}

}  // namespace

RefState resolve_one_slow(RefState& state, const RefResolver* resolver,
                          std::uint32_t handle, AstNode*& target,
                          std::uint8_t& which) {
  return resolve_guarded(
      state,
      [&] { return look_up(resolver, handle, &target, &which, nullptr); },
      [&]() noexcept { forget(&target, &which, nullptr); }, resolver, handle,
      &target, &which, nullptr);
}

RefState resolve_all_slow(RefState& state, const RefResolver* resolver,
                          std::uint32_t handle, MultiRefItems& out) {
  return resolve_guarded(
      state, [&] { return look_up(resolver, handle, nullptr, nullptr, &out); },
      [&]() noexcept { forget(nullptr, nullptr, &out); }, resolver, handle,
      nullptr, nullptr, &out);
}

}  // namespace cursiv::detail

namespace cursiv {

// Here, beside the frames: resolving a batch is a phase, and its root frame is
// where a wait under it finds `cancelToken`.
void DefaultDocumentReferences::force(std::span<const detail::RefHandle> which,
                                      const CancellationToken& cancelToken) {
  if (which.empty()) return;
  detail::Frame root{.cancelToken = &cancelToken};
  const detail::Filed filed(root);
  for (const detail::RefHandle& h : which) {
    throw_if_cancelled(cancelToken);
    if (h.owner == nullptr) continue;
    try {
      _f.refOps[h.field].force(*h.owner, h.index);
    } catch (const OperationCancelled&) {
      throw;
    } catch (...) {
      // A provider that threw failed one reference -- published as
      // `ErrorThrown` by the resolution guard -- not the whole list.
    }
  }
}

}  // namespace cursiv
