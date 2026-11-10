#pragma once

#include <cstdint>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv {

// One writer, shared readers; a read inside a write runs inline on the
// writer's thread. A write inside a read, or a wait or `Normal` read from a
// read or a build worker, can deadlock. `OperationCancelled` is absorbed.
class WorkspaceLock {
 public:
  virtual ~WorkspaceLock() noexcept = default;

  // Whether a later write may cancel this one while it still holds
  // exclusivity.
  enum class WriteMode : std::uint8_t {
    // The default, and the low-latency model: a newer write cancels this
    // one's token the moment it enters.
    Supersedable,
    // Protects the mutation of an edit the client already committed from a
    // later keystroke; past `endMutation`, the write is superseded normally.
    Durable,
  };

  // `write`/`read` run the action on the calling thread, so it lives on that
  // stack for the whole call; it cannot be null.
  using WriteAction = cursiv::function_ref<void(const CancellationToken&)>;
  using ReadAction = cursiv::function_ref<void()>;

  // Which writes a read may run beside. An `Immediate` read is only correct
  // where the state it needs has already been awaited by other means -- here,
  // `DocumentBuilder::waitUntil`.
  enum class ReadPriority : std::uint8_t {
    // Queued behind every write.
    Normal,
    // Runs beside a write whose mutation ended, instead of waiting for it: a
    // request needing only `Parsed` does not wait for the scopes and the
    // linking of a build already past its mutation.
    Immediate,
  };

  // Writes are served in entry order, and a new write cancels the previous one
  // without skipping it: `action` always runs, token already cancelled or not,
  // because that is where the text is installed.
  virtual void write(WriteAction action,
                     WriteMode mode = WriteMode::Supersedable) = 0;
  // False if the wait was cancelled; `action` was then not called.
  [[nodiscard]] virtual bool read(ReadAction action,
                                  const CancellationToken& = {},
                                  ReadPriority = ReadPriority::Normal) = 0;
  // Says the write's mutation is over, from inside the write: documents it did
  // not reset only advance from here, and `Immediate` reads may run. A no-op
  // anywhere else.
  virtual void endMutation() = 0;
  // Whether the serving write's mutation has ended: nothing may free a tree
  // then. False when no write holds.
  [[nodiscard]] virtual bool mutationEnded() const = 0;
  // Cancels the last submitted write; it still runs, and a durable one keeps
  // its mutation: the cancellation takes effect when its mutation ends.
  virtual void cancelWrite() = 0;
};

}  // namespace cursiv
