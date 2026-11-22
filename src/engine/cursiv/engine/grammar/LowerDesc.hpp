// What a lowering reads of a grammar besides its image: the actions it
// publishes, in the order that numbers them, and its entry rule. All of it
// constant, made of the grammar's types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace cursiv::engine {

namespace gen {
struct ImageRuleDesc;
}

// Actions up to nest build or fill the tree. The remaining kinds describe
// what the record read, including the suffix of a nest.
enum class WriteKind : std::uint8_t {
  create,
  close,
  climb,
  assign,
  append,
  flag,
  nest,
  nestBody,
  convert,
  doc,
  fold,
};

// Whether the record of an action runs it on the tree.
constexpr bool runs(WriteKind kind) {
  return kind <= WriteKind::nest;
}

// A member a grammar writes, as data: one constant per member, told by its
// address.
struct MemberDesc {
  std::string_view name;
  detail::FieldContent content = detail::FieldContent::value;
  detail::FieldStorage storage = detail::FieldStorage::direct;
  const AstTypeTag* owner = nullptr;
  // The classes it points at: a node member's, a reference's candidates, a
  // variant's node alternatives.
  std::span<const AstTypeTag* const> targets;
  // Where it stands in its class, which orders the members of one class.
  std::ptrdiff_t (*offset)() = nullptr;
};

// An action as data. `name` is the class built or closed, the rule
// converted, or a `doc`'s text, and `type` that class; `feature` names the member written, which
// `member` describes; `narrow` is the class a reference site narrows to. A
// conversion says whether its rule is a terminal or a data rule, and what
// its converter gives.
enum class Converts : std::uint8_t { nothing, text, value, pass };

struct RowDesc {
  WriteKind kind = WriteKind::create;
  std::string_view name;
  const AstTypeTag* type = nullptr;
  const MemberDesc* member = nullptr;
  const AstTypeTag* narrow = nullptr;
  bool terminal = false;
  bool dataRule = false;
  Converts converts = Converts::nothing;
  bool node = false;
  std::uint16_t fold = 0;
};

struct LowerDesc {
  std::span<const RowDesc* const> rows;
  const gen::ImageRuleDesc* entry = nullptr;
};

}  // namespace cursiv::engine
