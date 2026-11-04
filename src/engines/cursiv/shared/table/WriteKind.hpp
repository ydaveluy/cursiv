// What one action does to the tree, and the sentinel that names none. The
// action itself is a type in the lowering; only its kind reaches a value.
#pragma once

#include <cstdint>

namespace cursiv {

// "Names no chain" / "names no entry", one value for every column below.
inline constexpr std::uint16_t kNoRow = 0xFFFFu;

// A creation opens the record; the three writes (`assign`, `append`,
// `flag`) fill one; a nest buries the current node under a new one; a
// conversion turns the span's text into the value its rule returns. A climb
// creates too, but from the record's own children. A close opens nothing and
// still hands a node back, adopting what its body built.
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

}  // namespace cursiv
