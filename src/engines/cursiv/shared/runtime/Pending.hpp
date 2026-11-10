// The unit of the reduction stack: a CST root and the node built for it, null
// for an effect to apply later. `text` is where the value is, `type` its exact
// type.
#pragma once

#include <cstdint>

#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"

namespace cursiv::engine {

struct Pending {
  NodeId root = kNoNode;
  NodeId text = kNoNode;
  std::uint16_t type = kNoIndex;
  std::uint16_t field = kNoField;
  std::uint16_t conv = kNoIndex;
  ActKind kind = ActKind::none;
  std::uint8_t narrow = kNoNarrow;
  AstNode* value = nullptr;
};
static_assert(sizeof(void*) != 8 || sizeof(Pending) == 24,
              "the record's columns live in padding Pending already had: a "
              "wider pending was measured worse than the tape reads it saves");

// A node and its exact type index. A nest moves both, the wrapper being
// another class.
struct Made {
  AstNode* value = nullptr;
  std::uint16_t type = kNoIndex;
};

}  // namespace cursiv::engine
