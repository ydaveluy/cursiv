#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"

namespace cursiv {

// Destruction precondition: no thread may be about to call
// `write`/`read`/`settle`/`cancelWrite`, and no write may be waiting for its
// ticket -- a write action always runs, so a queued writer would install its
// text into a dying workspace. What is already in flight -- one serving
// write, gated reads -- is not a violation: the destructor cancels and
// drains it.
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
  void settle() override;
  [[nodiscard]] bool settled() const override;
  void cancelWrite() override;

 private:
  // The durability handoff, mutex held: true when the serving write must now
  // cancel its own token. Called once per write, by `settle` or -- for a write
  // that never settles -- by its end.
  bool settleLocked();

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
  // The serving write has passed its mutation phase, so an `Immediate` read
  // may run beside it. Raised by `settle()` on the writer's own thread, and
  // false the whole time no write holds -- where there is nothing to run
  // beside, `_writeHeld` alone admits.
  bool _settled = false;
  // Whether `_latest` may still be superseded. Raised when a Durable write
  // becomes the latest; lowered at its settle if nothing newer arrived, or
  // when a newer write replaces it as the latest.
  bool _latestDurable = false;
  // The serving write asked for durability, so its settle owes the handoff.
  bool _activeDurable = false;
  // `cancelWrite` reached a durable latest: its settle cancels it.
  bool _latestCancelled = false;
  // The thread of the write being served -- set with exclusivity, cleared when
  // the action is done. Writes run on the calling thread here, so this is what
  // "the writer's own thread" means; a default-constructed id matches no
  // running thread.
  std::thread::id _writerThread{};
  std::stop_source _active{std::nostopstate};
  std::stop_source _latest{std::nostopstate};
};

}  // namespace cursiv
