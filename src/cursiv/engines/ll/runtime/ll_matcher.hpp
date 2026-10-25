// The lexical matcher: one walk over the mode's automaton, the mode a runtime
// index. Longest match, ties as `LexTape::prefers` ranks them, and a word
// boundary tested in the match itself rather than in a second scan.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/runtime/LexTape.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"

namespace cursiv {

template <const auto& T>
inline bool ll_lex_accepts(std::uint16_t state, SymbolId term, const char* src,
                           TextOffset at, TextOffset len,
                           TextOffset& inspected) {
  if constexpr (T.lexChecks == nullptr) return true;
  if (T.lexChecks[state] == 0) {
    const auto bound = T.dfaBound[state];
    if (bound == kNoSet || at >= len) return true;
    inspected = std::max(inspected, lex_boundary(T).reach(bound, src, at, len));
    return !lex_boundary(T).at(bound, src, at, len);
  }
  const auto first = T.lexChoiceAt[std::size_t{state} * 2];
  const auto count = T.lexChoiceAt[std::size_t{state} * 2 + 1];
  for (auto r = first; r < first + count; ++r) {
    const auto& row = T.lexChoices[r];
    if (row.term != term) continue;
    if (row.bound != kNoSet && at < len) {
      inspected =
          std::max(inspected, lex_boundary(T).reach(row.bound, src, at, len));
      if (lex_boundary(T).at(row.bound, src, at, len)) continue;
    }
    return true;
  }
  return false;
}

// The terminals a refused word took off the position.
template <const auto& T>
struct LlTermSkip {
  std::array<std::uint64_t, T.termWords> w{};

  void set(SymbolId t) { w[t >> 6] |= std::uint64_t{1} << (t & 63u); }
  bool test(SymbolId t) const { return ((w[t >> 6] >> (t & 63u)) & 1u) != 0; }
};

// The terminals refusing words behind their head.
template <const auto& T>
inline constexpr auto kLlRefusing = [] {
  constexpr std::size_t n = [] {
    std::size_t k = 0;
    for (std::uint16_t t = 1; t < T.nterms; ++t) k += T.terms[t].refuses;
    return k;
  }();
  std::array<std::uint16_t, n> out{};
  std::size_t k = 0;
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.terms[t].refuses) out[k++] = t;
  return out;
}();

// The tape of the descent: a state's election runs over the rows of its
// acceptances; `Skips`, with the terminals a refused word took off the
// position.
template <const auto& T, bool Skips = false>
struct LlLexTape : LexTape<T> {
  using Skip = LlTermSkip<T>;
  using Skipping = LlLexTape<T, true>;
  const Skip* skip = nullptr;

  bool bounded(std::uint16_t s) const { return T.lex_bounded(s); }
  SymbolId qualified(std::uint16_t s, const char* src, TextOffset at,
                     TextOffset len, TextOffset& far) const {
    if (T.lexChecks == nullptr || T.lexChecks[s] == 0) {
      const auto t = static_cast<SymbolId>(T.dfaAccept[s] - 1u);
      const bool skipped = Skips && skip->test(t);
      if (!skipped && (T.dfaBound[s] == kNoSet || at >= len)) return t;
      if (!skipped) {
        far = std::max(far, lex_boundary(T).reach(T.dfaBound[s], src, at, len));
        if (!lex_boundary(T).at(T.dfaBound[s], src, at, len)) return t;
      }
    }
    return lex_elect<T>(s, src, at, len, far,
                        [&](SymbolId t) { return Skips && skip->test(t); });
  }
  TextOffset bound_reach(std::uint16_t s, const char* src, TextOffset i,
                         TextOffset len) const {
    if (T.dfaBound[s] == kNoSet) return i;
    return lex_boundary(T).reach(T.dfaBound[s], src, i, len);
  }
  Scan unrefused(const char* src, TextOffset len, std::uint16_t st,
                 TextOffset from, Scan out, TextOffset& far) const
    requires(!Skips && LexTape<T>::kRefuses)
  {
    return lex_unrefused(*this, src, len, st, from, out, far);
  }
  bool skips(SymbolId t) const
    requires(Skips)
  {
    return skip->test(t);
  }
};

template <const auto& T>
inline Scan ll_match_text(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset from, TextOffset& halt) {
  return lex_match_halt(LlLexTape<T>{}, src, len, mode, from, halt);
}

template <const auto& T>
inline Scan ll_match_text(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset from) {
  return lex_match(LlLexTape<T>{}, src, len, mode, from);
}

}  // namespace cursiv
