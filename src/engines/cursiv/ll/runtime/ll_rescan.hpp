// A misspelling is repaired by re-reading, never by rewriting: a cold search
// over the mode's DFA -- a bounded edit distance -- says how far each terminal
// the point admits would have reached, at what price, and the author's own
// span becomes its leaf.
#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_matcher.hpp"
#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/runtime/TypoCandidates.hpp"
#include "cursiv/shared/runtime/TypoTape.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"

namespace cursiv {

template <const auto& T>
inline constexpr std::array<std::uint64_t, T.termWords> kNoReserve{};

// One walk's table, as `typo_each` reads it: every answer is a lookup on the
// frozen table.
template <const auto& T>
struct LlTypoTape : TypoTape<T> {
  // The terminals the point admits.
  std::uint64_t* want;
  const CancellationPoll* stop = nullptr;

  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    return typo_successors(T.typoSuccBase, T.typoSucc, s);
  }
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    if (stop != nullptr) stop->poll();
    ++*this->read;
    return T.next_byte(s, b);
  }
  // A keyword the source as written spells reserves its spelling away from
  // every reading through it, unless its own condition refuses it there.
  bool original(std::uint16_t s, TextOffset p) {
    if (T.dfaAccept[s] == 0) return true;
    const LlTermRow res = reserved(s, p);
    bool any = false;
    for (std::uint32_t k = 0; k < T.termWords; ++k) {
      want[k] &= ~res.w[k];
      any |= want[k] != 0;
    }
    return any;
  }
  // A word a keyword reserved is not a name, whatever the walk paid for it.
  template <class Emit>
  void owed(std::uint16_t s, TextOffset p, Emit emit) const {
    if (T.dfaAccept[s] == 0) return;
    const LlTermRow acc = T.dfa_acc_set(s);
    const LlTermRow res = reserved(s, p);
    for (std::uint32_t k = 0; k < T.termWords; ++k)
      for (std::uint64_t bits = acc.w[k] & ~res.w[k] & want[k]; bits != 0;
           bits &= bits - 1u) {
        const auto t =
            static_cast<SymbolId>(k * 64u + std::countr_zero(bits));
        if (this->closed(s, t, p) && !this->trails(t, p)) emit(t);
      }
  }
  LlTermRow reserved(std::uint16_t s, TextOffset p) const {
    TextOffset unseen = p;
    const LexTape<T> tape{};
    const auto allowed = [&](SymbolId t) {
      return !tape.refuses(t, this->src + this->head,
                           this->docLen - this->head);
    };
    return lex_reserves<T>(s, this->src, p, this->docLen, unseen, allowed)
               ? T.dfa_res_set(s)
               : LlTermRow{kNoReserve<T>.data(), T.termWords};
  }
  // The walk's count of its stops: no statistics read it here, and the stops
  // that leave the candidates incomplete set `cut` through the bounds.
  void refused() const {}
};

// The re-readings of a misspelling from `at`, over the terminals `want` names
// under `mode`, added to `found`; `read` counts the bytes the walks read. The
// caller already read the ordinary token, which ends at `plain`, and bounded
// the section. All paths and alphabets share `most`; hitting a horizon
// reports an incomplete set of candidates through `cut`. `scratch` keeps the
// walk's points, or null.
template <const auto& T>
inline void ll_rescan_within(const LlContext& c, std::uint16_t mode,
                             const std::uint64_t* want, TextOffset at,
                             TextOffset plain, TextOffset len,
                             TextOffset horizon, std::uint64_t& read,
                             TypoCandidates& found, std::uint64_t most,
                             bool& cut, TypoScratch* scratch = nullptr) {
  std::array<std::uint64_t, T.termWords> allowed;
  for (std::uint32_t k = 0; k < T.termWords; ++k) allowed[k] = want[k];
  if constexpr (LexTape<T>::kRefuses)
    for (const std::uint16_t t : kLlRefusing<T>)
      if (((allowed[t >> 6] >> (t & 63u)) & 1u) != 0 &&
          T.head_refuses(t, c.src + at, c.len - at))
        allowed[t >> 6] &= ~(std::uint64_t{1} << (t & 63u));
  const TypoBounds bounds{c.src, horizon, len, &read, &cut, most, scratch};
  typo_each(LlTypoTape<T>{{bounds, at}, allowed.data(), &c.stop},
            T.modeStart[mode], at, plain,
            [&](const Typo& candidate) { found.add(candidate); });
}

}  // namespace cursiv
