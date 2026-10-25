// What one published action states, read once from its row type: the indices
// the AST runs are settled by, and the names the record facet answers with.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct AstRowGen {
  WriteKind kind = WriteKind::create;
  std::string_view feature;
  std::string_view typeName;
  std::string_view rule;
  std::uint16_t declType = detail::kNoIndex;
  std::uint16_t typeSlot = detail::kNoIndex;
  std::uint16_t fieldSlot = kNoField;
  std::uint16_t convSlot = detail::kNoIndex;
  std::uint8_t narrowSlot = kNoNarrow;
  ActKind act = ActKind::none;
  std::string_view convName;
  std::string_view convOver;
  std::size_t convRank = 0;
  bool convJoins = false;
  bool convTextParts = true;
  bool convParts = false;
  bool dataRule = false;
  std::string_view textConv;
  std::string_view textOver;
  bool makes = false;
  bool opens = false;
  bool writes = false;
  bool effects = false;
  bool converts = false;
};

}  // namespace cursiv
