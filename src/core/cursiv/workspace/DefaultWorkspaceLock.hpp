#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"

namespace cursiv {

// Destruction precondition: no thread is about to call it, and no write waits
// for its ticket, as a queued write would still run. A serving write and
// gated reads are cancelled and drained.
class DefaultWorkspaceLock : public WorkspaceLock {
 public:
  DefaultWorkspaceLock() = default;
  DefaultWorkspaceLock(const DefaultWorkspaceLock&) = delete;
  DefaultWorkspaceLock& operator=(const DefaultWorkspaceLock&) = delete;
  ~DefaultWorkspaceLock() noexcept override;

  void write(WriteAction action,
             WriteMode mode = WriteMode::Supersedable) override;
  [[nodiscard]] bool read(ReadAction action, const CancellationToken& = {},
                          ReadPriority = ReadPriority::Normal) override;
  void allowImmediateReads() override;
  [[nodiscard]] bool immediateReadsAllowed() const override;
  void cancelWrite() override;

 private:
  // Ends the serving write's protection, mutex held: true when it must now
  // cancel its own token. Called once per write, by `allowImmediateReads` or
  // -- for a write that never calls it -- by its end.
  bool endProtectionLocked();

  mutable std::mutex _m;
  // `condition_variable_any`: its `stop_token` overload registers the callback
  // and the wait as one operation, so a cancellation racing the last predicate
  // test cannot be lost.
  std::condition_variable_any _cv;

  std::uint64_t _ticketNext = 0;     // the next rank handed out
  std::uint64_t _ticketServing = 0;  // whose turn it is
  std::size_t _writersPending = 0;   // entered, not finished yet
  std::size_t _readers = 0;          // running reads, shared and immediate
  std::size_t _inGate = 0;           // threads that entered the gate
  bool _writeHeld = false;
  bool _stopping = false;
  // `Immediate` reads may run beside the serving write. Set by
  // `allowImmediateReads()` on the writer's thread; false while no write holds.
  bool _immediateReadsAllowed = false;
  // No newer write cancels `_latest`. Raised when a `Protected` write becomes
  // the latest; lowered when its protection ends if nothing newer arrived, or
  // when a newer write replaces it as the latest.
  bool _latestProtected = false;
  // The serving write is still protected.
  bool _activeProtected = false;
  // `cancelWrite` reached a protected latest: the end of protection cancels it.
  bool _latestCancelled = false;
  // The thread of the serving write, set with exclusivity and cleared after its
  // action; a default id matches no thread.
  std::thread::id _writerThread{};
  std::stop_source _active{std::nostopstate};
  std::stop_source _latest{std::nostopstate};
};

}  // namespace cursiv
