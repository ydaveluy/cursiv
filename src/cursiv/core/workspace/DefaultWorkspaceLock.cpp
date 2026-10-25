#include "cursiv/core/workspace/DefaultWorkspaceLock.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>

#include "cursiv/core/utils/Cancellation.hpp"

namespace cursiv {

DefaultWorkspaceLock::~DefaultWorkspaceLock() noexcept {
  std::stop_source active{std::nostopstate};
  std::stop_source latest{std::nostopstate};
  {
    std::scoped_lock lock(_m);
    // The declared precondition, checked where it can be: a write still
    // waiting for its ticket here would be served into a workspace
    // mid-demolition. One entered write is the drain the destructor owes; a
    // second is the caller's bug.
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
    // Everyone out: gated reads, immediate reads running beside a settled
    // write, and every entered write.
    _cv.wait(lock, [this]() {
      return _inGate == 0 && _readers == 0 && _writersPending == 0 &&
             !_writeHeld;
    });
  }
}

void DefaultWorkspaceLock::write(WriteAction action, WriteMode mode) {
  std::stop_source mine;
  std::stop_source superseded{std::nostopstate};
  std::uint64_t ticket = 0;
  {
    std::scoped_lock lock(_m);
    ticket = _ticketNext++;
    ++_writersPending;
    // A durable latest is not superseded: its mutation is protected until its
    // settle hands the guard back. Everything else keeps the low-latency
    // model: the newest write wins.
    if (!_latestDurable) superseded = _latest;
    _latest = mine;
    _latestDurable = mode == WriteMode::Durable;
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
    _settled = false;
    _activeDurable = mode == WriteMode::Durable;
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

  // Wakeups happen under the mutex, here and everywhere else: the destructor
  // waits for everyone to be out and then destroys the mutex and the condition
  // variable, so a `notify_all` made after unlocking would touch a dead
  // object. `request_stop` is the one call made outside it: it runs user
  // callbacks synchronously and one may want in here.
  {
    std::scoped_lock lock(_m);
    // A write that never settled -- one with no build under it -- hands the
    // durability guard back here instead, so no mode leaks past its action.
    // Its verdict is dropped: what it would ask for is this write's own token,
    // and this write is over.
    if (!_settled) (void)settleLocked();
    _writeHeld = false;
    _settled = false;
    _activeDurable = false;
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
    // The writer's own read runs inline: the write action already holds
    // exclusivity, which is access compatible with a read, and gating it here
    // would wait on a condition only this same, now-blocked thread could make
    // true.
    if (_writerThread == std::this_thread::get_id()) {
      lock.unlock();
      action();
      return true;
    }
    ++_inGate;
    // Writer priority: `_writersPending` counts waiting writes and the serving
    // one.
    //
    // An `Immediate` read does not yield to a waiting writer -- yielding is
    // what it exists to stop doing -- and waits only for the serving write to
    // settle. It still counts as a reader below, so the next write drains it
    // before taking exclusivity: what it may not observe is a reset, and a
    // reset happens under exclusivity it is holding out.
    const bool proceed =
        priority == ReadPriority::Immediate
            ? _cv.wait(
                  lock, token,
                  [this]() { return _stopping || !_writeHeld || _settled; })
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
    --_readers;
    --_inGate;
    _cv.notify_all();
  }
  if (failure != nullptr) std::rethrow_exception(failure);
  return true;
}

// Durability ends one way or the other. Nothing newer queued: future writes
// may supersede the rest normally. A newer write already waiting: it could not
// cancel the mutation, so it supersedes what follows now that the mutation is
// read-safe -- tickets are FIFO and this write holds, so every other pending
// write is newer.
bool DefaultWorkspaceLock::settleLocked() {
  if (!_activeDurable) return false;
  _activeDurable = false;
  if (_writersPending > 1) return true;
  _latestDurable = false;
  if (_latestCancelled) {
    _latestCancelled = false;
    return true;
  }
  return false;
}

void DefaultWorkspaceLock::settle() {
  bool cancelSelf = false;
  {
    std::scoped_lock lock(_m);
    // Only the write itself may say it: from anywhere else this would be a
    // caller opening the door on a mutation it does not own.
    if (_writerThread != std::this_thread::get_id() || !_writeHeld) return;
    if (_settled) return;
    _settled = true;
    cancelSelf = settleLocked();
    _cv.notify_all();
  }
  // Outside the mutex, as every `request_stop` here is.
  if (cancelSelf && _active.stop_possible()) _active.request_stop();
}

bool DefaultWorkspaceLock::settled() const {
  std::scoped_lock lock(_m);
  return _writeHeld && _settled;
}

void DefaultWorkspaceLock::cancelWrite() {
  std::stop_source latest{std::nostopstate};
  {
    std::scoped_lock lock(_m);
    if (_latestDurable) {
      _latestCancelled = true;
      return;
    }
    latest = _latest;
  }
  if (latest.stop_possible()) latest.request_stop();
}

}  // namespace cursiv
