#pragma once

#include <cstdint>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

namespace cursiv {

// One writer, shared readers. Every pointer and view taken under a read stays
// valid while it is held, unless the read is `Immediate` -- the door `settle`
// opens. A read inside a write runs inline; a write under a read, or a wait
// from a read action, both deadlock instead, silently. An escaping
// `OperationCancelled` is absorbed; any other exception propagates after
// exclusivity is released.
class WorkspaceLock {
 public:
  virtual ~WorkspaceLock() noexcept = default;

  // Whether a later write may cancel this one while it still holds
  // exclusivity.
  enum class WriteMode : std::uint8_t {
    // The default, and the low-latency model: a newer write cancels this
    // one's token the moment it enters.
    Supersedable,
    // The mutation is protected: a causally-announced edit -- one the client
    // has already committed on its side -- must not lose the install of its
    // text to a keystroke that arrived behind it. Durability ends at `settle`:
    // once the mutation is read-safe, the rest is superseded normally.
    Durable,
  };

  // `cursiv::function_ref`, not an owning callable: `write`/`read` run the
  // action on the calling thread, so it lives on that stack for the whole call.
  // It has no empty state, which is the point -- a null action is not a case.
  using WriteAction = cursiv::function_ref<void(const CancellationToken&)>;
  using ReadAction = cursiv::function_ref<void()>;

  // Which writes a read may run beside. An `Immediate` read is only correct
  // where the state it needs has already been awaited by other means -- here,
  // `DocumentBuilder::waitUntil`.
  enum class ReadPriority : std::uint8_t {
    // Queued behind every write, as a read has always been.
    Normal,
    // Runs beside a settled write instead of waiting for it. What it buys is
    // the build's tail: a request needing only `Parsed` no longer waits for
    // the scopes and the linking of a build already past its mutation.
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
  // The write's mutation is over, said from inside it on its own thread. It is
  // the one boundary a write has: before it an `Immediate` read would hold
  // pointers a reset is about to invalidate -- dropping a parse invalidates
  // every pointer into a document; after it, the documents this write did not
  // reset only climb, and a `Durable` write stops being protected. A no-op
  // anywhere else, so a caller with no write in hand cannot open the door.
  virtual void settle() = 0;
  // Whether the serving write has said it -- what a mutation asserts against.
  // Past a settle, an `Immediate` read may be holding a tree, so nothing may
  // free one. False when no write holds, which is what lets a build outside
  // any lock assert the same line.
  [[nodiscard]] virtual bool settled() const = 0;
  // Cancels the last submitted write; it still runs, and a durable one keeps
  // its mutation: the cancellation lands at its settle.
  virtual void cancelWrite() = 0;
};

}  // namespace cursiv
