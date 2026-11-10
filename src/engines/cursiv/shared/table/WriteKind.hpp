// What one action does to the tree. The action itself is a type in the
// lowering; only its kind is a value.
#pragma once

#include <cstdint>

namespace cursiv::engine {

// A row column that names no chain or no entry.
inline constexpr std::uint16_t kNoRow = 0xFFFFu;

// `create` opens a record (`climb` an infix rule's), the writes fill it, `nest`
// puts the current node under a new one, `convert` turns the span's text into
// a value, and `close` returns the node its body built.
enum class WriteKind : std::uint8_t {
  create,
  close,
  climb,
  assign,
  append,
  flag,
  nest,
  convert,
};

}  // namespace cursiv::engine
