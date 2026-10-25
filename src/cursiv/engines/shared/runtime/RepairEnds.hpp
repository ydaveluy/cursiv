// The ends a refused token's re-readings may stop at, found one at a time as a
// search asks for them: `scanned` is where the text was read to for them.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/runtime/CancellationPoll.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"

namespace cursiv {

struct RepairEnds {
  enum Regime : std::uint8_t { none, lines, line, last };

  std::vector<TextOffset> found;
  TextOffset scanned = 0;
  Regime regime = none;
  bool done = false;

  // The next end the token's reading `r` from `p` may stop at, past the ones
  // found, the bytes scanned for it added to `work`; false where none is
  // left. `cuts(x)` says an end at `x` cuts a word.
  template <class Cuts>
  bool more(const char* src, TextOffset len, TextOffset p, const LexReading& r,
            std::uint64_t& work, Cuts&& cuts,
            TextOffset upto = ~TextOffset{0}, bool* shortOf = nullptr,
            const CancellationPoll* stop = nullptr,
            std::uint64_t maxWork = ~std::uint64_t{0}) {
    if (done) return false;
    const auto eol = [&](TextOffset i) { return lex_line_end_at(src, i); };
    const auto pay = [&](TextOffset from, TextOffset to) {
      work += (to - p) / kRepairLexBytes - (from - p) / kRepairLexBytes;
    };
    const auto advance = [&](TextOffset& i, const auto& keep) {
      if (i >= r.end) return true;
      const std::uint64_t left = maxWork > work ? maxWork - work : 0;
      const std::uint64_t bytes = left > ~std::uint64_t{0} / kRepairLexBytes
                                      ? ~std::uint64_t{0}
                                      : left * kRepairLexBytes;
      const TextOffset cap = std::min(
          upto, r.end - i > bytes ? static_cast<TextOffset>(i + bytes) : r.end);
      while (i < r.end && i < cap && keep(i)) {
        if (stop != nullptr) stop->poll();
        ++i;
      }
      if (i < cap || cap >= r.end) return true;
      if (shortOf != nullptr) *shortOf = true;
      return false;
    };
    Regime nextRegime = regime;
    TextOffset nextScanned = scanned;
    if (nextRegime == none) {
      TextOffset i = p;
      const bool complete = advance(i, [&](TextOffset at) { return !eol(at); });
      pay(p, i);
      if (!complete) return false;
      nextRegime = i < r.end ? lines : r.end < len && eol(r.end) ? line : last;
      nextScanned = nextRegime == lines ? i : p + 1;
    }
    TextOffset x = nextScanned;
    bool complete = true;
    if (nextRegime == lines) {
      complete = advance(x, [&](TextOffset at) { return !eol(at); });
    } else if (nextRegime == line) {
      complete = advance(x, cuts);
    } else {
      x = r.end;
    }
    // `last` jumps to an end the initial scan already inspected. The bounded
    // caller pays only the walks it performs; legacy callers keep their count.
    if (nextRegime != last || maxWork == ~std::uint64_t{0})
      pay(nextScanned, x);
    if (!complete) return false;
    found.push_back(x);
    regime = nextRegime;
    scanned = x + 1;
    done = x >= r.end;
    return true;
  }
};

}  // namespace cursiv
