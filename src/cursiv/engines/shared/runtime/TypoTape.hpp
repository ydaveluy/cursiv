// What a re-reading walk asks of a frozen table, for both engines: where a
// terminal's token may close, and whether a word may be read as a spelled one.
#pragma once

#include <cstdint>

#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/runtime/LexTape.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"

namespace cursiv {

template <const auto& T>
struct TypoTape : TypoBounds {
  static constexpr const auto& cfg = LexTape<T>::cfg;

  // Where the token begins: a keyword's own refusal is asked of its head.
  TextOffset head = 0;

  // A word the terminal refuses where its token ends, spelled behind the
  // re-reading: its head may be the one misspelt.
  bool trails(SymbolId t, TextOffset p) const {
    if (!cfg.terms[t].refuses) return false;
    if constexpr (requires { T.tailRefuses; })
      return T.tailRefuses != nullptr && T.tailRefuses(t, src + p, docLen - p);
    else
      return T.tail_refuses(t, src + p, docLen - p);
  }
  // The boundary is the one the ACCEPTANCE declares: a state may accept several
  // terminals, each with a frontier of its own, and a repair may not read what
  // the lexer would refuse. An unbounded terminal still ends at a word edge: a
  // repair cutting a word in two is a cascade, not a reading.
  bool closed(std::uint16_t s, SymbolId t, TextOffset p) const {
    if (p >= docLen) return true;
    if (inside_scalar(src, p, docLen)) return false;
    std::uint16_t bound = T.dfaBound[s];
    T.each_accept(s, [&](SymbolId term, std::uint16_t own, unsigned) {
      if (term != t) return false;
      bound = own;
      return true;
    });
    return bound == kNoSet ? !lex_boundary(cfg).word_at(src, p, docLen)
                           : !lex_boundary(cfg).at(bound, src, p, docLen);
  }
  // Whether the word up to `p` may be read as spelled terminal `t`: its
  // spelling, read from `start`, accepts it, and closes where the lexer would
  // let it.
  bool replaces(std::uint16_t start, SymbolId t, TextOffset p) const {
    std::uint16_t s = start;
    for (const char ch : cfg.term_text(t)) {
      s = T.next_byte(s, static_cast<unsigned char>(ch));
      if (s == kDeadState) return false;
    }
    if (T.dfaAccept[s] == 0 || !T.dfa_acc_set(s).test(t)) return false;
    if constexpr (LexTape<T>::kRefuses)
      if (cfg.terms[t].refuses && T.head_refuses(t, src + head, docLen - head))
        return false;
    return closed(s, t, p) && !trails(t, p);
  }
};

}  // namespace cursiv
