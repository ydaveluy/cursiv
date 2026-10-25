// What the tree says of the last token before a caret: the records that hold
// it.
#pragma once

#include <span>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/engines/ll/runtime/ll_spine.hpp"

namespace cursiv {

class LlSpine {
 public:
  LlSpine(const Cst& cst, const CstLeaf& leaf) : _cst(cst), _leaf(leaf) {}

  // Looked for when first asked: the walk down to a token costs a hop for each
  // later sibling at every level.
  std::span<const NodeId> records() const {
    if (!_read) {
      _read = true;
      if (_leaf) ll_ancestors(_cst, _leaf.id, _records);
    }
    return _records;
  }

 private:
  const Cst& _cst;
  CstLeaf _leaf;
  mutable bool _read = false;
  mutable std::vector<NodeId> _records;
};

}  // namespace cursiv
