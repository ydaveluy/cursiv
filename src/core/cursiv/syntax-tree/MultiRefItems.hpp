// The targets a name resolving to several has, filled by a read.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

struct AstNode;

struct MultiRefItem {
  AstNode* target = nullptr;
  std::uint8_t which = 0;
};

using MultiRefItems = std::vector<MultiRefItem>;

}  // namespace cursiv
