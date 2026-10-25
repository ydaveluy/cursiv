// The unit of the reduction stack: a CST root and the node built for it, null
// for an effect to apply later. `text` is where the value is, not the root when
// a directive wraps its payload; `type` is the value's exact type, so every
// family question is an index test.
#pragma once

#include <cstdint>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

namespace cursiv {

struct Pending {
  NodeId root = kNoNode;
  NodeId text = kNoNode;
  std::uint16_t type = detail::kNoIndex;
  std::uint16_t field = kNoField;
  std::uint16_t conv = detail::kNoIndex;
  ActKind kind = ActKind::none;
  std::uint8_t narrow = kNoNarrow;
  AstNode* value = nullptr;
};
static_assert(sizeof(void*) != 8 || sizeof(Pending) == 24,
              "the record's columns live in padding Pending already had: a "
              "wider pending was measured worse than the tape reads it saves");

}  // namespace cursiv
