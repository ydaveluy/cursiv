#pragma once

#include <string_view>

#include "cursiv/core/syntax-tree/Cst.hpp"

namespace cursiv {

// A position plus the tree that explains it, for the one reader with no tree
// in hand: a data-rule converter is handed text, so its parts carry their
// own. Nothing else stores a tree pointer this way -- nodes and diagnostics
// keep a bare `NodeId`, and a tree states structure alone.
struct CstNodeView {
  const Cst* cst = nullptr;
  NodeId id = kNoNode;
  const CstNode& node() const { return (*cst)[id]; }
  std::string_view text() const { return cst->text(id); }
  TextOffset begin() const { return node().begin; }
  TextOffset end() const { return node().end; }
  // Bounded: a view built with an id from a tape that has since been rewound
  // must refuse, not dereference a slot that no longer exists.
  explicit operator bool() const {
    return cst != nullptr && id != kNoNode && id < cst->size();
  }
};

}  // namespace cursiv
