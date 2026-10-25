#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"

namespace cursiv {
struct ValidationCategory;  // identity by address; the storey above owns it
}  // namespace cursiv

namespace cursiv {

// A `bool` would confuse "cancelled" with "will never get there".
enum class WaitOutcome : std::uint8_t {
  Reached,
  Cancelled,
  Unreachable,
  BuildFailed,
};

// Fired on the build's calling thread, once per rung a build publishes, with
// the documents that phase carried. A cancelled phase never drained, so it is
// never announced; a phase with nothing to do still fires with an empty list,
// which is what lets a subscriber settle work an earlier cancelled build
// finished per document.
//
// A subscriber holds the write's exclusivity, so no other write runs beside
// it. An `Immediate` read does: every rung fires past the write's settle,
// which is the door those reads come through.
class BuildPhaseListener {
 public:
  virtual ~BuildPhaseListener() noexcept = default;
  virtual void onBuildPhase(DocumentState reached,
                            std::span<Document* const> built,
                            const CancellationToken&) = 0;
};

// Per-document milestones run on workers before the workspace milestone;
// waiting for that workspace milestone from a callback would deadlock.
// Content notifications may be deferred until old links have been invalidated,
// so the document can already have advanced. Later phases notify immediately.
class DocumentPhaseListener {
 public:
  virtual ~DocumentPhaseListener() noexcept = default;
  virtual void onDocumentPhase(Document& document, DocumentState reached,
                               const CancellationToken&) = 0;
};

// What the workspace just lost or gained, announced before the rebuild that
// answers it. A subscriber holding anything derived from a document -- a
// memo, an inverse table -- learns of a deletion here and nowhere else, a
// deleted document reaching no phase to be announced at.
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
  // list is read inside builds without a lock, which that law makes correct.
  virtual void addBuildPhaseListener(BuildPhaseListener&) = 0;
  virtual void removeBuildPhaseListener(BuildPhaseListener&) = 0;
  virtual void addDocumentPhaseListener(DocumentPhaseListener&) = 0;
  virtual void removeDocumentPhaseListener(DocumentPhaseListener&) = 0;
  virtual void addDocumentUpdateListener(DocumentUpdateListener&) = 0;
  virtual void removeDocumentUpdateListener(DocumentUpdateListener&) = 0;

  // The options are remembered per document: a document keeps what it was
  // built with, so one build can carry a different request per document and
  // a later `update` reuses each document's own. `categories` filters the
  // validating phase by identity; empty runs every one.
  //
  // The split is incremental: the builder remembers per document which
  // categories have run on the current revision, so a fast build followed by a
  // slow one runs the slow checks and appends their diagnostics. A `Validated`
  // rung means "validated for what was asked so far", never "nothing left to
  // owe". A category already run is not re-run and its diagnostics are not
  // doubled; an edit resets the memory with the rest of the document.
  // With validation disabled, the document stops at `IndexedReferences` and
  // no document-level `Validated` notification is emitted. The workspace
  // still publishes the end of each phase, including an empty validation phase.
  virtual void build(std::span<Document* const>, const BuildOptions& = {},
                     const CancellationToken& = {}) const = 0;

  // Changed targets and changed exports invalidate their dependants; unresolved
  // references are retried too. Custom visibility rules extend `isAffected`.
  // Every reset precedes publication, while the workspace excludes readers.
  virtual void update(std::span<const std::string_view> changed,
                      std::span<const std::string_view> deleted,
                      const CancellationToken& = {}) const = 0;

  // What a changed document is rebuilt with: `built_in` and `fast` unless the
  // storey above says otherwise. Every other document keeps its own request.
  [[nodiscard]] virtual BuildOptions& updateBuildOptions() noexcept = 0;
  [[nodiscard]] virtual const BuildOptions& updateBuildOptions()
      const noexcept = 0;

  // The prefix rule read backwards: going back to rung k undoes k+1, k+2, ...
  // up to the top.
  virtual void resetToState(Document&, DocumentState) const = 0;

  // A wait is for a rung, not for a build. Without the liveness answer
  // (`Unreachable`), a request on an excluded document blocks forever.
  [[nodiscard]] virtual WaitOutcome waitUntil(
      DocumentState, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual WaitOutcome waitUntil(
      DocumentState, DocumentId, const CancellationToken& = {}) const = 0;
  [[nodiscard]] virtual DocumentState currentState() const noexcept = 0;
};

}  // namespace cursiv
