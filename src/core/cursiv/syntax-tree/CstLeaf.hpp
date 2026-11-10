#pragma once

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/TextOffset.hpp"

// A record with no children, an empty composite included: what
// `Cst::childless(id)` tests.

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
