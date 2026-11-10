#pragma once

#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/workspace/DocumentId.hpp"

// A symbol's name, type and identity, as an index stores it. The target is
// `(documentId, symbolId)`, which survives a rebuild; the pointer beside it is
// valid only while the build that filled it stands.

namespace cursiv {

struct AstNodeDescription {
  // A view into the parse result: the document's text, or a node's `string`.
  // Every route that frees a parse purges the index rows first.
  std::string_view name;
  // The node's type as the process knows it: the tag's address, resolved once,
  // so filtering a scope is one bit test per candidate.
  const AstTypeTag* type = nullptr;

  DocumentId documentId = kNoDocument;
  // The CST node the AST node was built from, found again after a rebuild. Two
  // nodes built on one record share it; `node` tells them apart.
  NodeId symbolId = kNoNode;

  // Set only while the description's own build stands. Not const: the linker
  // binds references to it, and a description without one resolves nothing.
  AstNode* node = nullptr;

  [[nodiscard]] bool valid() const noexcept {
    return documentId != kNoDocument && symbolId != kNoNode;
  }
};

}  // namespace cursiv
