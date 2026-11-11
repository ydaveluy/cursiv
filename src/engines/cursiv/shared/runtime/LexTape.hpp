// What `lex_walk` reads of a frozen table: the lookups both engines share. An
// engine's tape derives from it to add how its states pick a terminal.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cursiv/shared/runtime/LexBoundary.hpp"
#include "cursiv/shared/runtime/LexRelayRun.hpp"
#include "cursiv/shared/runtime/LexWalk.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Utf8.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

template <const auto& T>
struct LexTape {
  static constexpr const auto& cfg = []() -> const auto& {
    if constexpr (requires { T.cfg; })
      return T.cfg;
    else
      return T;
  }();
  static constexpr bool kRefuses = [] {
    for (unsigned t = 0; t < cfg.nterms; ++t)
      if (cfg.terms[t].refuses) return true;
    return false;
  }();
  static constexpr bool kLonger = [] {
    if constexpr (requires { T.has_longer(); })
      return T.has_longer();
    else
      return false;
  }();
  static constexpr bool kBacktrack = [] {
    if constexpr (requires { T.has_backtrack(); })
      return T.has_backtrack();
    else
      return false;
  }();
  // The table echoes marks: the adapter offers `relay` and the rest of the
  // relay protocol.
  static constexpr bool kRelays = T.nrelays != 0;

  std::uint16_t start(std::uint16_t mode) const { return T.modeStart[mode]; }
  // The mode that lexes the terminals `mode` takes for the length of its token
  // alone, `kNoMode` where it has none.
  std::uint16_t longer(std::uint16_t mode) const {
    if constexpr (kLonger)
      return T.longer_mode(mode);
    else
      return kNoMode;
  }
  // Whether a walk of `mode` may read far past its token without a refusal
  // to answer for (`LexBacktrack.hpp`).
  bool backtracks(std::uint16_t mode) const {
    if constexpr (kBacktrack)
      return T.modeBacktrack[mode] != 0;
    else
      return false;
  }
  std::uint16_t step(std::uint16_t s, unsigned char b) const {
    return T.next_byte(s, b);
  }
  // The state a walk from `s` over `[from, end)` stands in.
  std::uint16_t state_at(std::uint16_t s, const char* src, TextOffset from,
                         TextOffset end) const {
    for (; from < end && s != kDeadState; ++from)
      s = step(s, static_cast<unsigned char>(src[from]));
    return s;
  }
  std::uint16_t accept(std::uint16_t s) const { return T.dfaAccept[s]; }
  CURSIV_ALWAYS_INLINE bool bound_has(std::uint16_t s, const char* src,
                                      TextOffset i, TextOffset len) const {
    return lex_boundary(cfg).at(T.dfaBound[s], src, i, len);
  }
  std::uint16_t relay(std::uint16_t s) const
    requires kRelays
  {
    return T.dfaRelay[s];
  }
  const LexRelay& relay_of(std::uint16_t k) const { return T.relays[k]; }
  std::size_t relay_count() const { return T.nrelays; }
  TextOffset run(std::uint16_t state, const char* src, TextOffset len,
                 TextOffset at) const
    requires kRelays
  {
    return T.relay_run(state, src, len, at);
  }
  const ByteSet* sets() const { return lex_boundary(cfg).sets; }
  const char* names() const {
    if constexpr (requires { cfg.names.data(); })
      return cfg.names.data();
    else
      return cfg.names;
  }
  // Whether the reading of `t` at `priority` is elected over that of `other`.
  bool prefers(SymbolId t, unsigned priority, SymbolId other,
               unsigned otherPriority) const {
    return other == kInvalidSymbol ||
           lex_term_before(cfg.terms[t], priority, t, cfg.terms[other],
                           otherPriority, other);
  }
  bool refuses(SymbolId t, const char* p, TextOffset rest) const {
    return cfg.terms[t].refuses && T.head_refuses(t, p, rest);
  }
  // Whether a keyword stands at `at` in state `s`, reached from a token begun
  // at `from`, and `s` reserves `t` from it.
  bool reserves(std::uint16_t s, SymbolId t, const char* src, TextOffset from,
                TextOffset at, TextOffset len, TextOffset& far) const;
  // The priority the reading of `t` that state `s` accepts ranks at `at`: a
  // reading whose boundary refuses the byte there is none.
  unsigned priority(std::uint16_t s, SymbolId t, const char* src, TextOffset at,
                    TextOffset len, TextOffset& far) const;
  // The priority at which the reading of `reader` takes a spelling: trivia
  // takes none.
  unsigned taking(SymbolId reader, unsigned priority) const {
    return cfg.terms[reader].trivia ? 0u : priority;
  }
  // Whether a reading behind conditions that a literal's or a keyword's branch
  // read (`priority`, as `taking` answers it) takes the spelling of the walk's
  // reading `w`, which stopped in state `s`...
  bool takes(std::uint16_t s, unsigned priority, SymbolId w) const {
    return priority != 0 && T.dfa_relay_res_set(s).test(w);
  }
  // ...and that of a word read behind conditions that no such branch read.
  static bool takes_relayed(unsigned priority, unsigned other) {
    return priority != 0 && other == 0;
  }
  bool prefers_relayed(SymbolId t, unsigned priority, SymbolId other,
                       unsigned otherPriority) const {
    if (other == kInvalidSymbol ||
        takes_relayed(taking(t, priority), otherPriority))
      return true;
    return !takes_relayed(taking(other, otherPriority), priority) &&
           prefers(t, priority, other, otherPriority);
  }
  TextOffset head_reach(std::uint16_t mode, const char* p,
                        TextOffset rest) const {
    return T.head_reach(mode, p, rest);
  }
};

inline constexpr auto kLexUnskipped = [](SymbolId) { return false; };

// Whether a bound is the word class of some grammar the table reads.
template <const auto& T>
constexpr bool lex_word_bound(std::uint16_t bound) {
  return std::ranges::find(T.wordBounds, bound) != T.wordBounds.end();
}

// Whether `bound` refuses the byte at `at`, `far` covering what it decoded.
template <const auto& T>
bool lex_bound_blocks(std::uint16_t bound, const char* src, TextOffset at,
                      TextOffset len, TextOffset& far) {
  if (bound == kNoSet || at >= len) return false;
  far = std::max(far, lex_boundary(LexTape<T>::cfg).reach(bound, src, at, len));
  return lex_boundary(LexTape<T>::cfg).at(bound, src, at, len);
}

// Whether a reading that reserves stands at `at`: an acceptance of `s` that
// `allowed` lets stand, a literal's where its boundary lets it, a keyword's
// where no byte of its word class follows.
template <const auto& T, class Allowed>
inline bool lex_reserves(std::uint16_t s, const char* src, TextOffset at,
                         TextOffset len, TextOffset& inspected,
                         Allowed&& allowed) {
  if constexpr (requires { T.reserves; })
    if constexpr (!T.reserves) return false;
  const auto reads = [&](SymbolId term) {
    return !LexTape<T>::cfg.terms[term].trivia && allowed(term);
  };
  const auto stands = [&](SymbolId term, std::uint16_t bound) {
    return reads(term) && !lex_bound_blocks<T>(bound, src, at, len, inspected);
  };
  if constexpr (T.wordBounds.empty()) {
    if (T.each_accept(
            s, [&](SymbolId term, std::uint16_t bound, unsigned priority) {
              return priority == kLiteralPriority && stands(term, bound);
            }))
      return true;
    if constexpr (!T.standsWithoutWordClass) {
      return false;
    } else {
      if (!T.each_accept(s,
                         [&](SymbolId term, std::uint16_t, unsigned priority) {
                           return priority == kKeywordPriority && reads(term);
                         }))
        return false;
      if (at >= len) return true;
      const auto& cfg = LexTape<T>::cfg;
      const auto w = cfg.wordRanges.size() != 0
                         ? static_cast<TextOffset>(utf8_width(src[at]))
                         : TextOffset{1};
      inspected = std::max(inspected, w < len - at ? at + w : len);
      return !lex_boundary(cfg).word_at(src, at, len);
    }
  } else {
    return T.each_accept(
        s, [&](SymbolId term, std::uint16_t bound, unsigned priority) {
          return (priority == kLiteralPriority || lex_word_bound<T>(bound)) &&
                 stands(term, bound);
        });
  }
}

template <const auto& T>
bool LexTape<T>::reserves(std::uint16_t s, SymbolId t, const char* src,
                          TextOffset from, TextOffset at, TextOffset len,
                          TextOffset& far) const {
  return T.dfa_res_set(s).test(t) &&
         lex_reserves<T>(s, src, at, len, far, [&](SymbolId k) {
           return !refuses(k, src + from, len - from);
         });
}

template <const auto& T>
unsigned LexTape<T>::priority(std::uint16_t s, SymbolId t, const char* src,
                              TextOffset at, TextOffset len,
                              TextOffset& far) const {
  auto out = 0u;
  T.each_accept(s, [&](SymbolId term, std::uint16_t bound, unsigned priority) {
    if (term == t && priority > out &&
        !lex_bound_blocks<T>(bound, src, at, len, far))
      out = priority;
    return false;
  });
  return out;
}

// The priority at which `term` reads `[from, end)` in the walk of `mode`: the
// state the walk was in at `end`, and the relays it launched that end there.
template <class Tape>
unsigned lex_reading_priority(const Tape& tape, const char* src, TextOffset len,
                              std::uint16_t mode, TextOffset from,
                              TextOffset end, SymbolId term) {
  auto st = tape.start(mode);
  TextOffset far = from, i = from;
  auto ended = kDeadState;
  [[maybe_unused]] std::conditional_t<Tape::kRelays, LexRelayRun<Tape>, bool>
      run{};
  for (;;) {
    if constexpr (Tape::kRelays)
      for (auto k = tape.relay(st); k != kNoRelay; k = tape.relay_of(k).next)
        run.launch(tape, k, src, len, from, i);
    if (i == end) {
      ended = st;
      break;
    }
    if (i >= len) break;
    st = tape.step(st, static_cast<unsigned char>(src[i]));
    if (st == kDeadState) break;
    ++i;
  }
  auto priority =
      ended == kDeadState ? 0u : tape.priority(ended, term, src, end, len, far);
  if constexpr (Tape::kRelays)
    if (run.any())
      run.finish(tape, src, len, far,
                 [&](SymbolId t, TextOffset e, unsigned as) {
                   if (t == term && e == end && as > priority) priority = as;
                 });
  return priority;
}

// What state `s` elects at `at` with the terminals `skipped` names taken off
// the position: the best acceptance a keyword does not reserve away and its
// own boundary lets stand.
template <const auto& T, class Skipped>
CURSIV_NOINLINE SymbolId lex_elect(std::uint16_t s, const char* src,
                                   TextOffset at, TextOffset len,
                                   TextOffset& far, Skipped&& skipped) {
  const bool reserved = lex_reserves<T>(
      s, src, at, len, far, [&](SymbolId t) { return !skipped(t); });
  SymbolId best = kInvalidSymbol;
  auto bestPriority = 0u;
  T.each_accept(s, [&](SymbolId term, std::uint16_t bound, unsigned priority) {
    if ((reserved && T.dfa_res_set(s).test(term)) || skipped(term) ||
        lex_bound_blocks<T>(bound, src, at, len, far))
      return false;
    if (LexTape<T>{}.prefers(term, priority, best, bestPriority)) {
      best = term;
      bestPriority = priority;
    }
    return false;
  });
  return best;
}

// A terminal refusing a word its token spells from `from` stands nowhere: the
// position is read again without it, until a reading stands. With `Hot`, the
// state `ended` of the refused match is asked first; others pass `kDeadState`.
template <bool Hot, class Tape>
CURSIV_NOINLINE Scan lex_unrefused_walk(const Tape& tape, const char* src,
                                        TextOffset len, std::uint16_t st,
                                        TextOffset from, Scan out,
                                        TextOffset& far, std::uint16_t ended) {
  typename Tape::Skip skip{};
  skip.set(out.term);
  const typename Tape::Skipping skipping{.skip = &skip};
  const auto refuses = [&](SymbolId t) {
    return t != kInvalidSymbol && tape.refuses(t, src + from, len - from);
  };
  if constexpr (Hot && !Tape::kRelays) {
    for (TextOffset seen = far;;) {
      const SymbolId t = skipping.qualified(ended, src, out.end, len, seen);
      if (t == kInvalidSymbol) break;
      if (!refuses(t)) return Scan{out.end, t};
      skip.set(t);
    }
  }
  for (;;) {
    TextOffset halt = from;
    out = lex_walk_from(skipping, src, len, st, from, halt);
    far = std::max(far, halt);
    if (!refuses(out.term)) return out;
    skip.set(out.term);
  }
}

template <class Tape>
CURSIV_ALWAYS_INLINE inline Scan lex_unrefused(const Tape& tape,
                                               const char* src, TextOffset len,
                                               std::uint16_t st,
                                               TextOffset from, Scan out,
                                               TextOffset& far) {
  if (out.term != kInvalidSymbol &&
      tape.refuses(out.term, src + from, len - from))
    return lex_unrefused_walk<false>(tape, src, len, st, from, out, far,
                                     kDeadState);
  return out;
}

// A mode's token is the longer of its own and the one its `longer` mode reads,
// its own at equal length; it never gives way to trivia.
template <class Tape, class Match>
CURSIV_ALWAYS_INLINE inline Scan lex_longest(const Tape& tape,
                                             std::uint16_t mode, Scan own,
                                             Match&& match) {
  if constexpr (Tape::kLonger) {
    const std::uint16_t more = tape.longer(mode);
    if (more == kNoMode ||
        (own.term != kInvalidSymbol && Tape::cfg.terms[own.term].trivia))
      return own;
    const Scan far = match(more);
    return far.term != kInvalidSymbol && far.end > own.end ? far : own;
  } else {
    (void)tape, (void)mode, (void)match;
    return own;
  }
}

template <class Tape>
CURSIV_ALWAYS_INLINE inline Scan lex_match_halt(const Tape& tape,
                                                const char* src, TextOffset len,
                                                std::uint16_t mode,
                                                TextOffset from,
                                                TextOffset& halt) {
  const Scan out = lex_walk(tape, src, len, mode, from, halt);
  // A refused word the token from `from` spells counts as read, the byte
  // that would lift the refusal included, whichever terminal the walk elected.
  if constexpr (Tape::kRefuses)
    if (from < len)
      halt =
          std::max(halt, from + tape.head_reach(mode, src + from, len - from));
  return lex_longest(tape, mode, out, [&](std::uint16_t more) {
    TextOffset reach = from;
    const Scan scan = lex_match_halt(tape, src, len, more, from, reach);
    halt = std::max(halt, reach);
    return scan;
  });
}

template <class Tape>
CURSIV_ALWAYS_INLINE inline Scan lex_match(const Tape& tape, const char* src,
                                           TextOffset len, std::uint16_t mode,
                                           TextOffset from) {
  return lex_longest(
      tape, mode, lex_walk(tape, src, len, mode, from),
      [&](std::uint16_t more) { return lex_walk(tape, src, len, more, from); });
}

}  // namespace cursiv::engine
