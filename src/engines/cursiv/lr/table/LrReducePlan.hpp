// A production's fixed work, shared by the emitted drive and the recovery.
#pragma once

#include <cstdint>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/table/LrReduceForm.hpp"

namespace cursiv::engine {

// The record a reduction hands its landing to close: never one, always one,
// or one known only at run time.
enum class LrReduceRow : std::uint8_t { none, node, any };

struct LrReducePlan {
  std::uint32_t names = 0;
  std::uint16_t count = 0;
  LrReduceForm form = LrReduceForm::discard;
  // Empty, and its nonterminal stands first in some production: the trivia
  // before the next token is laid ahead of it.
  bool flushes = false;
  // A nest turn's row where its record reaches back over the record the frame
  // below laid; its own node where that frame laid nothing.
  std::uint16_t turnRow = 0xFFFFu;
  // A completed region hands an untouched gap to its caller, even where its
  // last optional item was absent. No token is needed to take that exit.
  bool regionExit = false;

  constexpr bool carries() const {
    return form == LrReduceForm::carry || form == LrReduceForm::foldClose;
  }
  constexpr bool closes() const {
    return form == LrReduceForm::close || form == LrReduceForm::foldClose;
  }
  constexpr LrReduceRow row(std::uint32_t len) const {
    switch (form) {
      case LrReduceForm::through:
        return LrReduceRow::none;
      case LrReduceForm::close:
        return LrReduceRow::node;
      case LrReduceForm::foldClose:
        return LrReduceRow::any;
      default:
        return turnRow != kNoElement && len != 0 ? LrReduceRow::any
                                                 : LrReduceRow::none;
    }
  }
};

}  // namespace cursiv::engine
