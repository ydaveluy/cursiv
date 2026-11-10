#pragma once

#include <span>
#include <string_view>
#include <unordered_set>

#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

// What the whole workspace knows: the symbols each document exports, filled
// before linking, and the references each document holds, filled after.

namespace cursiv {

struct Document;

class IndexManager {
 public:
  virtual ~IndexManager() noexcept = default;

  // Phase `IndexedContent`: what this document exports.
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

  // Every exported symbol, optionally of one type and from some documents (all
  // when empty), by normalized URI then export order. A description kept past
  // the call is valid until its document's exports next change.
  [[nodiscard]] virtual bool visitElements(
      const AstTypeTag* type, std::span<const DocumentId> documents,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;

  // The first export named `name` that `accepts` (`referenceAccepts`) takes, in
  // `visitElements` order and valid as there. `reflection` is the asking
  // language's, as `accepts` is over its own indices.
  [[nodiscard]] virtual const AstNodeDescription* findByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection) const = 0;

  // Every match of that name, in `visitElements` order, for a reference that
  // names several. False when the visitor stopped early.
  [[nodiscard]] virtual bool visitByName(
      std::string_view name, bool caseSensitive, AcceptSet accepts,
      const AstReflection& reflection,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;

  // Every reference to one symbol, the reverse of the linker. A visitor,
  // as the rows live in a snapshot held open during the walk. False when the
  // visitor stopped.
  [[nodiscard]] virtual bool visitReferences(
      DocumentId targetDocument, NodeId targetSymbol,
      cursiv::function_ref<bool(const ReferenceDescription&)> visitor)
      const = 0;
};

}  // namespace cursiv
