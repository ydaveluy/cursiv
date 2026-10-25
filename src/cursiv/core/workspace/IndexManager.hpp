#pragma once

#include <span>
#include <string_view>
#include <unordered_set>

#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

// What the whole workspace knows: the symbols every document exports, and the
// references every document holds. Two halves, updated by two phases, because a
// document's exports are known before anything is linked and its references
// only after.

namespace cursiv {

struct Document;

class IndexManager {
 public:
  virtual ~IndexManager() noexcept = default;

  // Phase `IndexedContent`: what this document shows the world.
  virtual void updateContent(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) = 0;
  // Phase `IndexedReferences`: what it points at, once linked.
  virtual void updateReferences(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // False when there was nothing to remove.
  virtual bool removeContent(DocumentId document) = 0;
  virtual bool removeReferences(DocumentId document) = 0;
  virtual bool remove(DocumentId document) = 0;

  // Changed targets or newly indexed exports may affect the document's links.
  // Pending export changes remain visible with an empty `changed` set until
  // the document's references have been rebuilt.
  [[nodiscard]] virtual bool isAffected(
      const Document& document,
      const std::unordered_set<DocumentId>& changed) const = 0;

  // Every exported symbol, optionally of one type and from one set of
  // documents. The empty set means all of them. The default index visits
  // normalized URIs in lexical order, retaining each document's export order.
  [[nodiscard]] virtual bool visitElements(
      const AstTypeTag* type, std::span<const DocumentId> documents,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;

  // What the workspace declares under one name: the global scope a reference
  // resolves to when no local scope answers it, without scanning every
  // exported symbol per lookup. `accepts` is the reference's own bitset
  // (`AstReflection::referenceAccepts`); the first match wins in the same
  // order as `visitElements`.
  //
  // The reflection is the asking language's, and `caseSensitive` is too: a
  // description carries a tag, the process's identity for a class, but the
  // bitset is over the asker's own indices -- without the conversion a
  // `.tst` could never resolve a symbol a `.req` exported.
  [[nodiscard]] virtual const AstNodeDescription* findByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection) const = 0;

  // Every match of that name, in `visitElements` order, for a reference that
  // names several. False when the visitor stopped early.
  [[nodiscard]] virtual bool visitByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;

  // Every reference pointing at one symbol -- the inverse of the linker's edge.
  // A visitor, like `visitElements` above and for the same reason: the rows
  // live in a snapshot the guard holds open, and nothing may leave under it.
  // False when the visitor stopped the walk.
  [[nodiscard]] virtual bool visitReferences(
      DocumentId targetDocument, NodeId targetSymbol,
      cursiv::function_ref<bool(const ReferenceDescription&)> visitor)
      const = 0;
};

}  // namespace cursiv
