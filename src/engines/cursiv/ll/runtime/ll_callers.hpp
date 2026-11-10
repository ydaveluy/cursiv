// What the callers of the rule in hand read once it ends, at their exact call
// sites. Frames are read from the nearest down to the first that must read
// something, so a deep descent costs only the frames that may be empty.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"
#include "cursiv/ll/table/LlTable.hpp"

namespace cursiv::engine {

// Each row the rule's end may return through, the nearest frame first, until
// `read` answers true or a frame must read something. True where `read` did.
template <const auto& T, class Read>
inline bool ll_caller_rows(const LlContext& c, Read&& read) {
  static_assert(T.callerReadAt != nullptr);
  // The document reads its end once the entry returns.
  constexpr std::uint32_t document =
      T.atn.edge(T.atn.state(T.atnStart).edge).follow;
  for (std::size_t i = c.stack.size() + 1; i-- > 0;) {
    const std::uint32_t at = T.callerReadAt[i != 0 ? c.stack[i - 1] : document];
    if (read(LlTermRow{T.callerReads + (at >> 1), T.termWords})) return true;
    if ((at & 1u) == 0) return false;
  }
  return false;
}

// Whether a caller goes on from `sym` once the rule in hand ends.
template <const auto& T>
inline bool ll_callers_take(const LlContext& c, std::uint16_t sym) {
  return ll_ends<T>(c, sym) ||
         ll_caller_rows<T>(c, [&](LlTermRow row) { return row.test(sym); });
}

}  // namespace cursiv::engine
