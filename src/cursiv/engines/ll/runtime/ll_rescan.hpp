// A misspelling is repaired by re-reading, never by rewriting: a cold walk over
// the mode's DFA -- k faults, no edit distance -- says how far each terminal
// the point admits would have reached, and the author's own span becomes its
// leaf.
#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/TypoCandidates.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"

namespace cursiv {

template <const auto& T>
inline constexpr std::array<std::uint64_t, T.termWords> kNoReserve{};

// One walk's table, as `typo_each` reads it: every answer is a lookup on the
// frozen table.
template <const auto& T>
struct LlTypoTape : TypoBounds {
  // The terminals the point admits.
  std::uint64_t* want;
  // Where the token begins: a keyword's own refusal is asked of its head.
  TextOffset head = 0;
  const CancellationPoll* stop = nullptr;

  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    return typo_successors(T.typoSuccBase, T.typoSucc, s);
  }
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    if (stop != nullptr) stop->poll();
    ++*read;
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
        if (closed(s, t, p) && !trails(t, p)) emit(t);
      }
  }
  LlTermRow reserved(std::uint16_t s, TextOffset p) const {
    TextOffset unseen = p;
    const LexTape<T> tape{};
    const auto allowed = [&](SymbolId t) {
      return !tape.refuses(t, src + head, docLen - head);
    };
    return lex_reserves<T>(s, src, p, docLen, unseen, allowed)
               ? T.dfa_res_set(s)
               : LlTermRow{kNoReserve<T>.data(), T.termWords};
  }
  // A word the terminal refuses where its token ends, spelled behind the
  // re-reading: its head may be the one misspelt.
  bool trails(SymbolId t, TextOffset p) const {
    return T.tailRefuses != nullptr && T.terms[t].refuses &&
           T.tailRefuses(t, src + p, docLen - p);
  }
  // The boundary is the one the ACCEPTANCE declares: a state may accept
  // several terminals, each with a frontier of its own. An unbounded terminal
  // ends at a word edge to avoid splitting words during repair.
  bool closed(std::uint16_t s, SymbolId t, TextOffset p) const {
    if (p >= docLen) return true;
    if (inside_scalar(src, p, docLen)) return false;
    std::uint16_t bound = T.dfaBound[s];
    T.each_accept(s, [&](SymbolId term, std::uint16_t own, unsigned) {
      if (term != t) return false;
      bound = own;
      return true;
    });
    return bound == kNoSet ? !lex_boundary(T).word_at(src, p, docLen)
                           : !lex_boundary(T).at(bound, src, p, docLen);
  }
  // The typo walk's refusal hook has no statistics sink here.
  void refused() const {}
};

// The re-readings of a misspelling from `at`, over the terminals `want` names
// under `mode`, added to `found`; `read` counts the bytes the walks read. The
// caller already read the ordinary token, which ends at `plain`, and bounded
// the section. All paths and alphabets share `most`; hitting a horizon
// reports an incomplete set of candidates through `cut`.
template <const auto& T>
inline void ll_rescan_within(const LlContext& c, std::uint16_t mode,
                             const std::uint64_t* want, TextOffset at,
                             TextOffset plain, TextOffset len,
                             TextOffset horizon, std::uint64_t& read,
                             TypoCandidates& found, std::uint64_t most,
                             bool& cut) {
  std::array<std::uint64_t, T.termWords> allowed;
  for (std::uint32_t k = 0; k < T.termWords; ++k) allowed[k] = want[k];
  if constexpr (LexTape<T>::kRefuses)
    for (const std::uint16_t t : kLlRefusing<T>)
      if (((allowed[t >> 6] >> (t & 63u)) & 1u) != 0 &&
          T.head_refuses(t, c.src + at, c.len - at))
        allowed[t >> 6] &= ~(std::uint64_t{1} << (t & 63u));
  typo_each(LlTypoTape<T>{{c.src, horizon, len, &read, &cut, most},
                          allowed.data(), at, &c.stop},
            T.modeStart[mode], at, plain,
            [&](const Typo& candidate) { found.add(candidate); });
}

}  // namespace cursiv
