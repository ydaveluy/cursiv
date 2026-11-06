#pragma once

#include "cursiv/references/LinkingResult.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/Cancellation.hpp"

// What a name points at: `link` and `unlink` walk a document's work list,
// `getCandidate` and `getCandidates` answer one site -- the second for a
// reference that names several targets at once -- and `createLinkingError`
// says why a site failed.

namespace cursiv {

struct Document;

class Linker {
 public:
  virtual ~Linker() noexcept = default;

  // Resolves every reference the document owns. Cancellation is polled inside
  // the loop: a document long enough to be worth cancelling is long in its
  // reference count.
  virtual void link(Document& document,
                    const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Drops every answer, errors included -- the one place that loop is written.
  virtual void unlink(Document& document) const = 0;

  // The scope-lookup primitive: what is resolved is what the site spells. An
  // empty `referenceText` is an error here, not an empty scope -- a reference
  // that spells nothing names nothing.
  [[nodiscard]] virtual AstNodeDescriptionOrError getCandidate(
      const ReferenceInfo& reference) const = 0;

  // Every visible candidate, for a reference that names several.
  [[nodiscard]] virtual AstNodeDescriptionsOrError getCandidates(
      const ReferenceInfo& reference) const = 0;

  // What a reference that failed is told to be: `why` says how, a name found
  // nowhere or a cycle. The one place the sentence is written, so a language
  // rewords it once: the errors above and the diagnostic a validator
  // publishes both read it. `description` is what the name did reach, when the
  // caller has one.
  [[nodiscard]] virtual LinkingError createLinkingError(
      const ReferenceInfo& reference, RefState why = RefState::ErrorNotFound,
      const AstNodeDescription* description = nullptr) const = 0;
};

}  // namespace cursiv
