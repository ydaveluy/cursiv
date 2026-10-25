// The hot loop of one grammar, as entry points: what its generated parser
// hands over, and what the recovery re-enters through the context.
#pragma once

#include <span>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrExit.hpp"
#include "cursiv/engines/lr/runtime/LrGapMemo.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"

namespace cursiv {

struct LrDrive {
  LrExit (*enter)(LrContext&) = nullptr;  // the stack's top state, lexing first
  // The token one alphabet reads at a position, by the drive's own walk.
  Scan (*walk)(const char*, TextOffset, std::uint16_t, TextOffset) = nullptr;
  // The candidates of one position under one alphabet, ranked, or null where
  // no alphabet of this grammar reads several.
  std::uint32_t (*scanText)(const char*, TextOffset, std::uint16_t,
                            TextOffset, Scan*, std::uint32_t) = nullptr;
  // The one row that states a creation and the site naming it.
  ElementId (*folds)(ElementId, ElementId) = nullptr;
  // Whether gap condition `k` holds for the token `la`, its gap from `from`.
  bool (*cond)(const LrContext&, unsigned, TextOffset, const Token&) = nullptr;
  // A named completion has no spelling yet: only definite refusals count.
  bool (*condText)(std::string_view, unsigned, TextOffset, const Token&, bool,
                   LrGapMemo*) = nullptr;
  std::span<const CondReach> condReach{};
  // Past the last byte condition `k`'s phrases read, the gap from `from` on.
  TextOffset (*condSeen)(std::string_view, unsigned, TextOffset,
                         const Token&) = nullptr;
};

// Re-enter the drive from the stack after a graph or repair wrote its path.
inline LrExit lr_resume(LrContext& c) {
  c.ncand = 0;
  return c.drive->enter(c);
}

}  // namespace cursiv
