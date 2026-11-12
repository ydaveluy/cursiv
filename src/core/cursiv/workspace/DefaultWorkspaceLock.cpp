#include "cursiv/workspace/DefaultWorkspaceLock.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <mutex>
#include <new>
#include <stop_token>
#include <thread>

#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {

DefaultWorkspaceLock::~DefaultWorkspaceLock() noexcept {
  std::stop_source active{std::nostopstate};
  std::stop_source latest{std::nostopstate};
  {
    std::scoped_lock lock(_m);
    // A write still waiting for its ticket here would be served into a
    // workspace being destroyed: one entered write is allowed, a second is a
    // caller bug.
    assert(_ticketNext - _ticketServing <= 1 &&
           "a write was still waiting for its ticket at destruction");
    _stopping = true;
    active = _active;
    latest = _latest;
  }
  // Unblock first, cancel second, and never under the mutex: `request_stop`
  // invokes user callbacks synchronously and one may want in here.
  _cv.notify_all();
  if (active.stop_possible()) active.request_stop();
  if (latest.stop_possible()) latest.request_stop();
  {
    std::unique_lock lock(_m);
    // Everyone out: gated reads, immediate reads running beside a write, and
    // every entered write.
    _cv.wait(lock, [this]() {
      return _inGate == 0 && _readers == 0 && _writersPending == 0 &&
             !_writeHeld;
    });
  }
}

void DefaultWorkspaceLock::write(WriteAction action, WriteMode mode) {
  // With no memory for a stop source, the write runs uncancellable: its action
  // always runs.
  std::stop_source mine{std::nostopstate};
  try {
    mine = std::stop_source();
  } catch (const std::bad_alloc&) {  // NOLINT(bugprone-empty-catch)
  }
  std::stop_source superseded{std::nostopstate};
  std::uint64_t ticket = 0;
  {
    std::scoped_lock lock(_m);
    ticket = _ticketNext++;
    ++_writersPending;
    // A protected latest is not cancelled here but at the end of its
    // protection. Everything else keeps the low-latency model: the newest
    // write wins.
    if (!_latestProtected) superseded = _latest;
    _latest = mine;
    _latestProtected = mode == WriteMode::Protected;
    _latestCancelled = false;
  }
  // Outside the lock: see the destructor.
  if (superseded.stop_possible()) superseded.request_stop();

  {
    std::unique_lock lock(_m);
    _cv.wait(lock, [this, ticket]() {
      return _ticketServing == ticket && !_writeHeld && _readers == 0;
    });
    _writeHeld = true;
    _immediateReadsAllowed = false;
    _activeProtected = mode == WriteMode::Protected;
    _active = mine;
    // From here to the end of the action this thread's reads are the writer's
    // own and run inline in `read()`.
    _writerThread = std::this_thread::get_id();
  }

  std::exception_ptr failure;
  try {
    // The action always runs, even while shutting down: skipping it would
    // install no text while reporting success.
    action(mine.get_token());
  } catch (const OperationCancelled&) {
    // Absorbed: a cancellation is not a failure.
  } catch (...) {
    failure = std::current_exception();
  }

  // Wakeups happen under the mutex: the destructor destroys it once everyone is
  // out. Only `request_stop` runs outside it, as it calls user callbacks.
  {
    std::scoped_lock lock(_m);
    // A write that never allowed immediate reads ends its protection here,
    // so no mode outlives its action.
    if (!_immediateReadsAllowed) (void)endProtectionLocked();
    _writeHeld = false;
    _immediateReadsAllowed = false;
    _activeProtected = false;
    --_writersPending;
    ++_ticketServing;
    _active = std::stop_source{std::nostopstate};
    _writerThread = {};
    _cv.notify_all();
  }
  if (failure != nullptr) std::rethrow_exception(failure);
}

bool DefaultWorkspaceLock::read(ReadAction action,
                                const CancellationToken& token,
                                ReadPriority priority) {
  {
    std::unique_lock lock(_m);
    // The writer's own read runs inline: it already holds exclusivity, and
    // gating it would wait on this same thread.
    if (_writerThread == std::this_thread::get_id()) {
      lock.unlock();
      action();
      return true;
    }
    ++_inGate;
    // Writers first: `_writersPending` counts waiting writes and the serving
    // one. An `Immediate` read waits only for the serving write to allow it,
    // and counts as a reader, so the next write drains it before any reset.
    const bool proceed =
        priority == ReadPriority::Immediate
            ? _cv.wait(lock, token,
                       [this]() {
                         return _stopping || !_writeHeld ||
                                _immediateReadsAllowed;
                       })
            : _cv.wait(lock, token, [this]() {
                return _stopping || (!_writeHeld && _writersPending == 0);
              });
    if (!proceed || _stopping) {
      if (--_inGate == 0) _cv.notify_all();
      return false;
    }
    ++_readers;
  }

  std::exception_ptr failure;
  try {
    action();
  } catch (...) {
    failure = std::current_exception();
  }

  {
    std::scoped_lock lock(_m);
    // Only the last reader out changes what a waiter tests: a write waits for
    // no reader, the destructor for no thread in the gate, and a gated read
    // never waits on another read.
    --_inGate;
    if (--_readers == 0) [[likely]]
      _cv.notify_all();
  }
  if (failure != nullptr) std::rethrow_exception(failure);
  return true;
}

// Ends the protection, and returns whether the write cancels itself at once:
// a newer write is waiting, or a cancel came while it was protected.
bool DefaultWorkspaceLock::endProtectionLocked() {
  if (!_activeProtected) return false;
  _activeProtected = false;
  if (_writersPending > 1) return true;
  _latestProtected = false;
  if (_latestCancelled) {
    _latestCancelled = false;
    return true;
  }
  return false;
}

void DefaultWorkspaceLock::allowImmediateReads() {
  bool cancelSelf = false;
  {
    std::scoped_lock lock(_m);
    // Only the write itself may allow them: a caller from anywhere else would
    // let reads in beside a write it does not own.
    if (_writerThread != std::this_thread::get_id() || !_writeHeld) return;
    if (_immediateReadsAllowed) return;
    _immediateReadsAllowed = true;
    cancelSelf = endProtectionLocked();
    _cv.notify_all();
  }
  // Outside the mutex, as every `request_stop` here is.
  if (cancelSelf && _active.stop_possible()) _active.request_stop();
}

bool DefaultWorkspaceLock::immediateReadsAllowed() const {
  std::scoped_lock lock(_m);
  return _writeHeld && _immediateReadsAllowed;
}

void DefaultWorkspaceLock::cancelWrite() {
  std::stop_source latest{std::nostopstate};
  {
    std::scoped_lock lock(_m);
    if (_latestProtected) {
      _latestCancelled = true;
      return;
    }
    latest = _latest;
  }
  if (latest.stop_possible()) latest.request_stop();
}

}  // namespace cursiv
