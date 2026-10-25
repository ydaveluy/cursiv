// A node and its exact type index. A nest moves both, the wrapper being
// another class.
#pragma once

#include <cstdint>

#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace cursiv {

struct Made {
  AstNode* value = nullptr;
  std::uint16_t type = detail::kNoIndex;
};

}  // namespace cursiv
