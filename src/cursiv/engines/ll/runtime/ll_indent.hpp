#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlLookIndent.hpp"
#include "cursiv/engines/ll/table/LlAtn.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"

namespace cursiv {

// The descent's levels, as a read ahead of it owns them. Only the active ones
// are copied: the arena's closed blocks would cost a long document its square.
inline void ll_indent_copy(const LlContext& c, LlLookIndent& state) {
  state.arena.clear();
  for (auto level = c.indent.head; level != kNoIndent;
       level = c.indentArena[level].prev)
    state.arena.push_back(c.indentArena[level]);
  std::reverse(state.arena.begin(), state.arena.end());
  for (std::size_t i = 0; i < state.arena.size(); ++i)
    state.arena[i].prev =
        i == 0 ? kNoIndent : static_cast<std::uint32_t>(i - 1u);
  state.at = c.indent;
  state.at.head = state.arena.empty()
                      ? kNoIndent
                      : static_cast<std::uint32_t>(state.arena.size() - 1u);
  state.held = c.heldEnd;
}

// Whether the end of input, read where `endStands` says whether it is the
// end, is the dedent that closes a level.
template <const auto& T>
inline bool ll_end_dedents(const std::vector<IndentLevel>& arena,
                           const IndentAt& at, bool endStands) {
  return !endStands && kLaid<T, kLaidDedent> != 0 &&
         indent_owes_dedent(arena, at.head);
}

// Whether a point that asks `admits` of a terminal takes the end of input as
// the end: it admits it and no dedent to close a level first.
template <const auto& T, class Admits>
inline bool ll_end_stands(Admits&& admits) {
  return admits(kEofSymbol) &&
         !(kLaid<T, kLaidDedent> != 0 && admits(kLaid<T, kLaidDedent>));
}

// Whether `mode` takes the end of input as the end.
template <const auto& T>
inline bool ll_mode_ends(std::uint16_t mode) {
  return ll_end_stands<T>([&](auto t) { return T.mode_admits(mode, t); });
}

// Whether one of the readings stands before an edge that reads `term`.
template <const auto& T, class Readings>
inline bool ll_readings_read(const Readings& readings, SymbolId term) {
  for (const auto& config : readings) {
    const auto& state = T.atn.state(config.state);
    for (std::uint32_t k = 0; k < state.nedges; ++k) {
      const auto& edge = T.atn.edge(state.edge + k);
      if (edge.kind == LlEdgeKind::atom && edge.label == term) return true;
    }
  }
  return false;
}

// Whether the readings take the pending dedent over the end of input.
template <const auto& T, class Readings>
inline bool ll_dedent_over_end(const Readings& readings) {
  return ll_readings_read<T>(readings, kLaid<T, kLaidDedent>);
}

// Whether the readings take the end of input as the end: one reads it and none
// reads a dedent, as `ll_mode_ends` asks of a mode.
template <const auto& T, class Readings>
inline bool ll_end_stands_over(const Readings& readings) {
  return ll_end_stands<T>(
      [&](SymbolId t) { return ll_readings_read<T>(readings, t); });
}

// A line's run is read at the head of the blank after the last token only:
// a line a trivia ends on says nothing.
template <const auto& T>
inline bool ll_indent_read(const char* src, TextOffset len,
                           const std::vector<IndentLevel>& arena, IndentAt& at,
                           TextOffset p, bool head, TextOffset held, Token& out,
                           bool admitsEof = false,
                           TextOffset* reached = nullptr) {
  if (at.depth != 0) return false;
  if (p == len && ll_end_dedents<T>(arena, at, admitsEof)) {
    out = Token{p, p, kLaid<T, kLaidDedent>};
    return true;
  }
  return head && indent_virtual_at(src, len, arena, at.head,
                                   kLaid<T, kLaidIndent>, kLaid<T, kLaidDedent>,
                                   p, out, [held] { return held; }, reached);
}

}  // namespace cursiv
