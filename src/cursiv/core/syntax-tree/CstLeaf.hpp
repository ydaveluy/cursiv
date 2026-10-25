#pragma once

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"

// One record that holds no other -- what a linear reader of the tree sees
// without decoding a record it does not read. A childless composite is one
// too, which is what makes this the same set the tape's `childless(id)`
// names.

namespace cursiv {

struct CstLeaf {
  NodeId id = kNoNode;
  TextOffset begin = 0;
  TextOffset end = 0;
  bool hidden = false;
  bool recovered = false;

  [[nodiscard]] constexpr explicit operator bool() const {
    return id != kNoNode;
  }
};

}  // namespace cursiv
