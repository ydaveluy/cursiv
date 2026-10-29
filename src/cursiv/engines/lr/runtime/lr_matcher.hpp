// The lexical matcher: one walk over the mode's DFA row, the mode a runtime
// index. Read outside the hot loop, so no code is generated per mode.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/table/LrLexElse.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/runtime/LexTape.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_skips(std::uint16_t mode, unsigned char b) {
  return ((A.derived.skipSet[std::size_t{mode} * 4 + (b >> 6)] >> (b & 63u)) &
          1) != 0;
}

// Whether a fallback row of the table asks the scalar a lead byte starts.
template <const auto& A>
inline constexpr bool kElseAsksScalar = [] {
  for (const LrLexElse& r : A.derived.lexElseRows)
    if (r.scalar != 0) return true;
  return false;
}();

template <const auto& A>
inline constexpr bool kElseElects = [] {
  for (const LrLexElse& r : A.derived.lexElseRows)
    if (r.scalar == 3) return true;
  return false;
}();

// The acceptance the state gives up to where its own boundary refused the byte
// behind it: a word byte after a keyword hands the name it reserved back.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline SymbolId lr_accept_else(
    std::uint16_t s, const char* src, TextOffset i,
    [[maybe_unused]] TextOffset len) {
  // Asked only where a boundary refused the byte behind the match.
  assert(i < len);
  const auto b = static_cast<unsigned char>(src[i]);
  for (auto k = A.derived.lexElseBase[s]; k < A.derived.lexElseBase[s + 1u];
       ++k) {
    const LrLexElse& r = A.derived.lexElseRows[k];
    if (!r.bytes.has(b)) continue;
    if constexpr (kElseElects<A>)
      if (r.scalar == 3) {
        TextOffset far = i;
        return lex_elect<A>(s, src, i, len, far, kLexUnskipped);
      }
    if constexpr (kElseAsksScalar<A>)
      if (r.scalar != 0 &&
          (r.scalar == 1) != lex_boundary(A.cfg).word_at(src, i, len))
        continue;
    return r.term;
  }
  return kInvalidSymbol;
}

// How far the fallback reads at `i`: a row asking the scalar reads it whole.
template <const auto& A>
inline TextOffset lr_else_reach(std::uint16_t s, const char* src, TextOffset i,
                                TextOffset len) {
  const auto b = static_cast<unsigned char>(src[i]);
  for (auto k = A.derived.lexElseBase[s]; k < A.derived.lexElseBase[s + 1u];
       ++k)
    if (A.derived.lexElseRows[k].scalar != 0 &&
        A.derived.lexElseRows[k].bytes.has(b)) {
      const auto n = static_cast<TextOffset>(utf8_width(src[i]));
      return i + n < len ? i + n : len;
    }
  return i + 1;
}

template <const auto& A>
constexpr auto lr_refused_from(const char* src, TextOffset from,
                               TextOffset len) {
  return [=](unsigned t) {
    return LexTape<A>{}.refuses(static_cast<SymbolId>(t), src + from,
                                len - from);
  };
}

// The tape of the table: a state's election was spelled as fallback rows;
// `Skips`, with the terminals a refused word took off the position.
template <const auto& A, bool Skips = false>
struct LrLexTape : LexTape<A> {
  using Skip = typename std::remove_cvref_t<decltype(A)>::TermSet;
  using Skipping = LrLexTape<A, true>;
  // Only a walk that takes terminals off the position elects with `qualified`;
  // the rest read the fallback rows.
  static constexpr bool kQualified = Skips;
  struct NoSkip {};
  [[no_unique_address]] std::conditional_t<Skips, const Skip*, NoSkip> skip{};

  // A state whose election a refusal took off hands the position to the
  // acceptances left, which a byte may admit inside its run and refuse at its
  // end: such a run is not read blind.
  bool bounded(std::uint16_t s) const {
    if constexpr (Skips) {
      if (A.dfa_bounded(s)) return true;
      if (A.dfaAccept[s] == 0 || !skip->test(A.dfaAccept[s] - 1u)) return false;
      auto open = A.dfa_acc_set(s);
      for (unsigned t = open.take(); t < A.cfg.nterms; t = open.take())
        if (!skip->test(t) &&
            (A.dfa_acc_bound(s, t) != kNoSet || A.dfa_res_set(s).test(t)))
          return true;
      return false;
    } else {
      return A.dfa_bounded(s);
    }
  }
  CURSIV_ALWAYS_INLINE TextOffset bound_reach(std::uint16_t s,
                                              const char* src, TextOffset i,
                                              TextOffset len) const {
    const TextOffset own =
        lex_boundary(A.cfg).reach(A.dfaBound[s], src, i, len);
    if constexpr (kElseAsksScalar<A>) {
      const TextOffset rows = lr_else_reach<A>(s, src, i, len);
      return own > rows ? own : rows;
    }
    return own;
  }
  CURSIV_ALWAYS_INLINE SymbolId refused(std::uint16_t s, const char* src,
                                        TextOffset i, TextOffset len) const {
    return lr_accept_else<A>(s, src, i, len);
  }
  Scan unrefused(const char* src, TextOffset len, std::uint16_t st,
                 TextOffset from, Scan out, TextOffset& far) const
    requires(!Skips && LexTape<A>::kRefuses)
  {
    return lex_unrefused(*this, src, len, st, from, out, far);
  }
  bool skips(SymbolId t) const
    requires(Skips)
  {
    return skip->test(t);
  }
  SymbolId qualified(std::uint16_t s, const char* src, TextOffset i,
                     TextOffset len, TextOffset& far) const
    requires(Skips)
  {
    auto t = static_cast<SymbolId>(A.dfaAccept[s] - 1u);
    if (i < len && A.dfa_bounded(s)) {
      far = std::max(far, bound_reach(s, src, i, len));
      if (this->bound_has(s, src, i, len)) t = refused(s, src, i, len);
    }
    if (t == kInvalidSymbol || !skip->test(t)) return t;
    return lex_elect<A>(s, src, i, len, far,
                        [&](SymbolId u) { return skip->test(u); });
  }
};

// The same for a walk whose caller reads no horizon.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline Scan lr_unrefused_hot(const char* src,
                                                  TextOffset len,
                                                  std::uint16_t st,
                                                  TextOffset from, Scan out,
                                                  std::uint16_t ended) {
  if (A.head_refuses(out.term, src + from, len - from)) {
    TextOffset far = from;
    return lex_unrefused_walk<true>(LrLexTape<A>{}, src, len, st, from, out,
                                    far, ended);
  }
  return out;
}

template <const auto& A>
inline Scan lr_match_halt(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset from, TextOffset& halt) {
  return lex_match_halt(LrLexTape<A>{}, src, len, mode, from, halt);
}

// The same with the terminals of `skip` off the position: a flagged winner
// the stack refused, read again.
template <const auto& A>
inline Scan lr_match_without(const char* src, TextOffset len,
                             std::uint16_t mode, TextOffset from,
                             std::span<const SymbolId> skip, TextOffset& halt) {
  typename LrLexTape<A>::Skip off{};
  for (const SymbolId t : skip) off.set(t);
  const typename LrLexTape<A>::Skipping tape{.skip = &off};
  return lex_match_halt(tape, src, len, mode, from, halt);
}

// The token the table's own walk reads, no generated walk asked: what a test
// holds the generated walks to.
template <const auto& A>
inline Scan lr_match_text(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset from) {
  return lex_match(LrLexTape<A>{}, src, len, mode, from);
}

// The mode a state reading `mode` lexes a fault with: every terminal, or in a
// section its grammar's and the token that closes it.
template <const auto& A>
constexpr std::uint16_t lr_err_mode([[maybe_unused]] std::uint16_t mode) {
  if constexpr (A.errModeOf.size() == 0) {
    return A.errMode;
  } else {
    return A.errModeOf[mode];
  }
}

// The token that ends the text state `st` reads: in a section, the one that
// closes it, which a repair there never invents, drops or reads as another,
// as a grammar alone treats the end of its text.
template <const auto& A>
constexpr SymbolId lr_text_close([[maybe_unused]] std::uint32_t st) {
  if constexpr (A.modeEnd.size() == 0) {
    return kEofSymbol;
  } else {
    return A.modeEnd[A.mode[st]];
  }
}

}  // namespace cursiv
