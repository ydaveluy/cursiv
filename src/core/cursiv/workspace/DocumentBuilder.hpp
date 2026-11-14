#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"

namespace cursiv {
struct ValidationCategory;  // identity by address
}  // namespace cursiv

namespace cursiv {

// A `bool` would confuse "cancelled" with "will never get there".
enum class WaitOutcome : std::uint8_t {
  Reached,
  Cancelled,
  Unreachable,
  BuildFailed,
};

// Called on the build's thread once per workspace state a build publishes,
// with the documents the phase carried, possibly none. A cancelled phase is
// never told. It runs after the state is published, beside its waiters and
// any `Immediate` read.
class BuildPhaseListener {
 public:
  virtual ~BuildPhaseListener() noexcept = default;
  virtual void onBuildPhase(DocumentState reached,
                            std::span<Document* const> built,
                            const CancellationToken&) = 0;
};

// Called on a worker after a document's state is published, beside its
// waiters; content states may be told after the document moved on. Waiting for
// the workspace state here deadlocks. A listener that throws fails the build;
// the others still receive.
class DocumentPhaseListener {
 public:
  virtual ~DocumentPhaseListener() noexcept = default;
  virtual void onDocumentPhase(Document& document, DocumentState reached,
                               const CancellationToken&) = 0;
};

// What the workspace lost or gained, told before the rebuild: the only news of
// a deletion. A listener may send another document back down with
// `resetToState`, and the rebuild builds it too.
class DocumentUpdateListener {
 public:
  virtual ~DocumentUpdateListener() noexcept = default;
  virtual void onUpdate(std::span<const std::string_view> changed,
                        std::span<const std::string_view> deleted) = 0;
};

class DocumentBuilder {
 public:
  virtual ~DocumentBuilder() noexcept = default;

  // Listeners are configuration: registered and removed outside any build. The
  // list is read inside builds without a lock, which this rule makes safe.
  virtual void addBuildPhaseListener(BuildPhaseListener&) = 0;
  virtual void removeBuildPhaseListener(BuildPhaseListener&) = 0;
  virtual void addDocumentPhaseListener(DocumentPhaseListener&) = 0;
  virtual void removeDocumentPhaseListener(DocumentPhaseListener&) = 0;
  virtual void addDocumentUpdateListener(DocumentUpdateListener&) = 0;
  virtual void removeDocumentUpdateListener(DocumentUpdateListener&) = 0;

  // Options are remembered per document, and replace what an earlier build
  // asked of a document not yet validated. `Validated` means validated for
  // the categories asked so far: a wider request runs only the missing ones.
  virtual void build(std::span<Document* const>, const BuildOptions& = {},
                     const CancellationToken& = {}) = 0;

  // Applies the changes, then builds: the token cuts the build, never the
  // changes. A changed target or export resets its dependants and unresolved
  // references are retried (`isAffected`), all before readers are let in.
  virtual void update(std::span<const std::string_view> changed,
                      std::span<const std::string_view> deleted,
                      const CancellationToken& = {}) = 0;

  // What an update asks of every document it builds: `built_in` and `fast`
  // unless set otherwise here. A request a cancelled build left unfinished is
  // finished first.
  [[nodiscard]] virtual BuildOptions& updateBuildOptions() noexcept = 0;
  [[nodiscard]] virtual const BuildOptions& updateBuildOptions()
      const noexcept = 0;

  // Undoes every state above the one given. Back to `Changed`, the tree goes,
  // and every document with a reference into it goes back to `ComputedScopes`
  // first. Throws `std::logic_error` once its write has built.
  virtual void resetToState(Document&, DocumentState) = 0;

  // Waits for a state, not a build; `Unreachable` when the document cannot get
  // there. A cancelled build answers nothing, so pass a token when no further
  // build may come.
  [[nodiscard]] virtual WaitOutcome waitUntil(
      DocumentState, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual WaitOutcome waitUntil(
      DocumentState, DocumentId, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual DocumentState currentState() const noexcept = 0;
};

}  // namespace cursiv
