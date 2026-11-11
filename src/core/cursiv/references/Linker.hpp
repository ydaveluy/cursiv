#pragma once

#include <vector>

#include "cursiv/references/LinkingError.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

// What a name points at: `link` and `unlink` walk a document's references,
// `getCandidate` and `getCandidates` answer one site, and `createLinkingError`
// says why a site failed.

namespace cursiv {

struct Document;

class Linker {
 public:
  virtual ~Linker() noexcept = default;

  // Resolves every reference of the document, checking cancellation inside the
  // loop.
  virtual void link(Document& document,
                    const CancellationToken& cancelToken = kNoCancel) const = 0;

  // Clears every resolution, errors included, of a tree that stays: a tree
  // that is dropped takes its resolutions with it.
  virtual void unlink(Document& document) const = 0;

  // What the site spells resolves to, or null; an empty `referenceText`
  // resolves to nothing.
  [[nodiscard]] virtual const AstNodeDescription* getCandidate(
      const ReferenceInfo& reference) const = 0;

  // Every visible candidate, for a reference that names several; empty when
  // none.
  [[nodiscard]] virtual std::vector<const AstNodeDescription*> getCandidates(
      const ReferenceInfo& reference) const = 0;

  // The error for a failed reference, `why` saying how: a name found nowhere or
  // a cycle. Every message comes from here: override it for a custom one.
  // `description` is what the name did reach, when the caller has one.
  [[nodiscard]] virtual LinkingError createLinkingError(
      const ReferenceInfo& reference, RefState why = RefState::ErrorNotFound,
      const AstNodeDescription* description = nullptr) const = 0;
};

}  // namespace cursiv
