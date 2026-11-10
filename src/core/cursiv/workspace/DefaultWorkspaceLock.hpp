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
  void endMutation() override;
  [[nodiscard]] bool mutationEnded() const override;
  void cancelWrite() override;

 private:
  // The durability handoff, mutex held: true when the serving write must now
  // cancel its own token. Called once per write, by `endMutation` or -- for
  // a write that never ends its mutation -- by its end.
  bool endMutationLocked();

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
  // The serving write is past its mutation, so `Immediate` reads may run. Set
  // by `endMutation()` on the writer's thread; false while no write holds.
  bool _mutationEnded = false;
  // Whether `_latest` may still be superseded. Raised when a Durable write
  // becomes the latest; lowered when its mutation ends if nothing newer
  // arrived, or when a newer write replaces it as the latest.
  bool _latestDurable = false;
  // The serving write is durable: its mutation end hands the guard back.
  bool _activeDurable = false;
  // `cancelWrite` reached a durable latest: its mutation end cancels it.
  bool _latestCancelled = false;
  // The thread of the serving write, set with exclusivity and cleared after its
  // action; a default id matches no thread.
  std::thread::id _writerThread{};
  std::stop_source _active{std::nostopstate};
  std::stop_source _latest{std::nostopstate};
};

}  // namespace cursiv
