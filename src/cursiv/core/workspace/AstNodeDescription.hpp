#pragma once

#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"

// What an index trades in: a symbol's name, its type, and the identity that
// finds it again.
//
// It names its target by `(documentId, symbolId)` and not by pointer: an index
// outlives the rebuild of the document it describes, so an address is no
// identity. The pointer is carried beside it as a shortcut, valid only while
// the build that filled it stands.

namespace cursiv {

struct AstNodeDescription {
  // A view, not a copy: the name is already written where the parse put it --
  // in the document's text, or, when a converter materialised it, in the pool
  // that text and tree share.
  //
  // Its lifetime is the parse result's, the same one `node` below already
  // depends on. The invariant that makes it sound is the reset's own order: a
  // document dropped to `Changed` has its index rows purged and its pool freed
  // by one call, so no row ever names bytes that are gone. Anything that frees
  // a pool by another route -- attaching a new text, destroying a document --
  // purges the rows first.
  std::string_view name;
  // The node's type, as the process knows it: the tag's address, not a name
  // string and not a grammar's own index -- an index means different classes
  // in two grammars, and one document resolving a symbol another exported is
  // exactly the case that proves it. Resolved once, when the description is
  // made, so filtering a scope is one bit test per candidate.
  const AstTypeTag* type = nullptr;

  DocumentId documentId = kNoDocument;
  // The CST node the AST node was cut from, which is what a document can find
  // again after the node itself has been rebuilt. It names a declaration
  // site, not a node: two nodes built on one record share it, so telling them
  // apart takes `node` below.
  NodeId symbolId = kNoNode;

  // Present only while the description's own build stands -- for a local
  // reference, resolving costs nothing. Not const: what follows this address is
  // the linker binding a reference to it, and a const pointer would only push
  // the cast one storey down.
  AstNode* node = nullptr;

  [[nodiscard]] bool valid() const noexcept {
    return documentId != kNoDocument && symbolId != kNoNode;
  }
};

}  // namespace cursiv
