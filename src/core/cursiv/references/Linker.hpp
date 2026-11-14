#pragma once

#include <cstdint>
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

// How a reference failed, as its diagnostic tells it.
enum class LinkingFailure : std::uint8_t {
  NotFound,
  Cycle,
  // The lookup itself failed: a provider threw.
  Threw,
};

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

  // The error for a failed reference: `why` says how it failed,
  // `targetDescription` what the name did reach, when the caller has one.
  // Every message comes from here: override it for a custom one.
  [[nodiscard]] virtual LinkingError createLinkingError(
      const ReferenceInfo& reference,
      LinkingFailure why = LinkingFailure::NotFound,
      const AstNodeDescription* targetDescription = nullptr) const = 0;
};

}  // namespace cursiv
