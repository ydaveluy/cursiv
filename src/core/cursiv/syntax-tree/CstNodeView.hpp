#pragma once

#include <string_view>

#include "cursiv/syntax-tree/Cst.hpp"

namespace cursiv {

// A position plus its tree, for a data-rule converter, which is handed text
// and no tree. Everything else keeps a bare `NodeId`.
struct CstNodeView {
  const Cst* cst = nullptr;
  NodeId id = kNoNode;
  const CstNode& node() const { return (*cst)[id]; }
  std::string_view text() const { return cst->text(id); }
  TextOffset begin() const { return node().begin; }
  TextOffset end() const { return node().end; }
  // Bounded: a view built with an id from a tree that has since been reset
  // must refuse, not read a record that is gone.
  explicit operator bool() const {
    return cst != nullptr && id != kNoNode && id < cst->size();
  }
};

}  // namespace cursiv
