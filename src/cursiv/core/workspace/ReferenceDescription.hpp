#pragma once

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"

// One cross-reference, as the index stores it: where it is written, and what
// it points at. Encoded as ids rather than URIs and paths, so an index
// outlives the rebuild of what it describes.

namespace cursiv {

struct ReferenceDescription {
  DocumentId sourceDocumentId = kNoDocument;
  // The span of the reference's text, which is what an editor underlines.
  TextOffset sourceBegin = 0;
  TextOffset sourceEnd = 0;

  // The target's declaration site, as `AstNodeDescription` names it. Two
  // nodes built on one CST record share it: "who points here" is asked of the
  // text they are both declared by, so a reference to either is listed for
  // both, and a reference holding both draws one row.
  DocumentId targetDocumentId = kNoDocument;
  NodeId targetSymbolId = kNoNode;

  // A cross-reference inside one document, marked because a local
  // change need not re-link the world.
  bool local = false;
  // Whether the source names several targets: a consumer walking incoming
  // edges skips the others in O(1) rather than going back to the source
  // document to ask.
  bool multiReference = false;

  [[nodiscard]] bool isResolved() const noexcept {
    return targetDocumentId != kNoDocument && targetSymbolId != kNoNode;
  }

  bool operator==(const ReferenceDescription&) const = default;
};

}  // namespace cursiv
