// What the callers of the rule in hand read once it ends, at their exact call
// sites: two calls made by the same rule need not have the same followers.
// Each frame the descent stands in names what its rule reads once the call
// returns, and whether that may be nothing: the frames are read from the
// nearest down to the first that must read something, so a deep descent costs
// only the frames that may be empty.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// Each row the rule's end may return through, the nearest frame first, until
// `read` answers true or a frame must read something. True where `read` did,
// or where the frames ran out with no caller known: any token may follow.
template <const auto& T, class Read>
inline bool ll_caller_rows(const LlContext& c, Read&& read) {
  static_assert(T.callerReadAt != nullptr);
  // The document reads its end once the entry returns.
  constexpr std::uint32_t document =
      T.atn.edge(T.atn.state(T.atnStart).edge).follow;
  for (std::size_t i = c.stack.size() + 1; i-- > 0;) {
    if (i == 0 && !c.document) return true;
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

}  // namespace cursiv
