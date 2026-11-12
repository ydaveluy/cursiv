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
    // Not cancelled by a newer write or by `cancelWrite` until it calls
    // `allowImmediateReads`; from there it is cancelled like any other write.
    Protected,
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
    // Runs beside a write that has called `allowImmediateReads`, instead of
    // waiting for its end: a request needing only `Parsed` does not wait for
    // that build's scopes and linking.
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
  // Called from inside a write: `Immediate` reads may now run beside it, and
  // a `Protected` write may now be cancelled. The caller resets no document
  // and frees no tree after the call. A no-op anywhere else.
  virtual void allowImmediateReads() = 0;
  // Whether the serving write has called `allowImmediateReads`: nothing may
  // reset a document or free a tree then. False when no write holds.
  [[nodiscard]] virtual bool immediateReadsAllowed() const = 0;
  // Cancels the last submitted write; it still runs. A `Protected` one is
  // cancelled only when it calls `allowImmediateReads`.
  virtual void cancelWrite() = 0;
};

}  // namespace cursiv
