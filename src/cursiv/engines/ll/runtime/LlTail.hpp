// The last steps of a replay whose conditions read as far as the caret, and
// the readings before the first of them: what a candidate asks again on the
// text it would splice in.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LlTail {
  // A token taken, or where `node` names none a token the repair inserted;
  // with the conditions asked of it on the document and what they answered.
  struct Step {
    Token token;
    TextOffset gapFrom = 0;
    std::uint16_t node = kNoRow;
    SymbolId inserted = kInvalidSymbol;
    std::vector<std::pair<std::uint16_t, bool>> asked;
  };

  std::vector<Step> steps;
  // The readings the first step starts from.
  std::vector<LlConfig> before;
  // The last token, read again where the replay ends: once as each terminal
  // it can be.
  std::vector<Step> stood;
  // Where the text a candidate replaces begins, and how many tokens stand
  // between the replay's last and the caret.
  TextOffset edge = 0;
  unsigned onward = 0;

  // Whether a condition that read the text up to `seen` read the text the
  // candidate replaces: a phrase that read to the end goes on with it.
  bool reaches(TextOffset seen, std::size_t size) const {
    return seen > edge || (seen == edge && seen == size);
  }

  // Whether a condition asked of `leaf`, `between` tokens before the caret
  // counting it, can read the text the candidate replaces.
  bool covers(const CondReach& reach, const CstLeaf& leaf,
              unsigned between) const {
    const std::uint64_t width = leaf.end - leaf.begin;
    const std::uint64_t look =
        std::min<std::uint64_t>(reach.look, width + reach.tail);
    return leaf.begin + look > edge || (reach.phrase && reach.tokens > between);
  }
};

}  // namespace cursiv
