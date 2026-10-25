// The candidate collector of the shared DFA walk: one furthest match per
// terminal, and the state's reserve.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "cursiv/engines/lr/runtime/lr_matcher.hpp"

namespace cursiv {

template <const auto& A, const auto& Base, const auto& Terms,
          const auto& Bounds, unsigned Max>
class LrLexCandidates {
 public:
  LrLexCandidates(Scan* out, std::uint32_t& count, std::uint32_t cap,
                  TextOffset start = 0)
      : _out(out), _count(count), _cap(cap), _start(start) {}

  template <class Ad>
  CURSIV_ALWAYS_INLINE void take(const Ad&, std::uint16_t st, const char* src,
                                 TextOffset at, TextOffset len, TextOffset&) {
    const unsigned begin = Base[st];
    const unsigned end = Base[st + 1u];
    if (begin == end) return;
    const auto& res = A.dfa_res_set(st);
    const auto refused = lr_refused_from<A>(src, _start, len);
    TextOffset unseen = at;
    const bool reserved =
        res.any() && lex_reserves<A>(st, src, at, len, unseen,
                                     [&](SymbolId t) { return !refused(t); });
    if (reserved) drop_if([&](const Scan& o) { return res.test(o.term); });
    for (auto k = begin; k < end; ++k)
      if ((!reserved || !res.test(Terms[k])) &&
          (Bounds[k] == kNoSet || at >= len ||
           !lex_boundary(A.cfg).at(Bounds[k], src, at, len)))
        add(Terms[k], at, tier_of(k, src, at, len));
  }

  template <class Relays>
  void finish(const char* src, TextOffset len, TextOffset start,
              std::uint16_t first, Relays& relays) {
    if constexpr (A.relays.size() != 0) {
      if (relays.any()) {
        ScanRoom<Max> held;
        std::uint32_t n = 0;
        TextOffset far = 0;
        LrLexCandidates room(held.data(), n, Max);
        const auto keep = [&](SymbolId term, TextOffset end, unsigned as) {
          room.add(term, end, static_cast<std::uint8_t>(as));
        };
        const LrLexTape<A> tape;
        relays.finish(tape, src, len, far, keep);
        // A reading behind conditions that a literal's or a keyword's branch
        // read takes the spelling of the readings that end with it.
        for (std::uint32_t k = 0; k < n; ++k) {
          if (held[k].term == kInvalidSymbol || held[k].tier == 0) continue;
          const TextOffset end = held[k].end;
          const std::uint16_t walked = walk_to(first, src, start, end);
          drop_if([&](const Scan& o) {
            return o.end == end && walked != kDeadState &&
                   tape.takes(walked, tape.taking(held[k].term, held[k].tier),
                              o.term);
          });
          for (std::uint32_t j = 0; j < n; ++j)
            if (held[j].end == end && held[j].term != kInvalidSymbol &&
                tape.takes_relayed(tape.taking(held[k].term, held[k].tier),
                                   held[j].tier))
              held[j].term = kInvalidSymbol;
        }
        for (std::uint32_t k = 0; k < n; ++k)
          if (held[k].term != kInvalidSymbol &&
              !reserved_at(first, src, len, start, held[k]))
            add(held[k].term, held[k].end, held[k].tier);
      }
    }
    // A terminal refusing a word its token spells stands nowhere here.
    if constexpr (LexTape<A>::kRefuses) {
      drop_if([&](const Scan& o) {
        return A.cfg.terms[o.term].refuses &&
               A.head_refuses(o.term, src + start, len - start);
      });
    }
    // A position has one extent: the shorter matches are not its rivals.
    TextOffset far = 0;
    for (std::uint32_t k = 0; k < _count; ++k) far = std::max(far, _out[k].end);
    drop_if([&](const Scan& o) { return o.end != far; });
  }

 private:
  Scan* _out;
  std::uint32_t& _count;
  std::uint32_t _cap;
  TextOffset _start;

  template <class Gone>
  void drop_if(Gone gone) {
    _count = static_cast<std::uint32_t>(
        std::remove_if(_out, _out + _count, gone) - _out);
  }

  // The tier of the reading candidate `k` is: its own, or the keyword's kept
  // beside it where that stands.
  CURSIV_ALWAYS_INLINE static std::uint8_t tier_of(unsigned k, const char* src,
                                                   TextOffset at,
                                                   TextOffset len) {
    std::uint8_t tier = 0;
    if constexpr (A.cold.accTier.size() != 0) tier = A.cold.accTier[k];
    if constexpr (A.cold.accStand.size() != 0)
      if (A.cold.accStand[k] != kNoSet &&
          (at >= len ||
           !lex_boundary(A.cfg).at(A.cold.accStand[k], src, at, len)))
        tier = kTierKeyword;
    return tier;
  }

  CURSIV_ALWAYS_INLINE void add(SymbolId term, TextOffset end,
                                std::uint8_t tier) {
    for (std::uint32_t k = 0; k < _count; ++k)
      if (_out[k].term == term) {
        if (_out[k].end < end) {
          _out[k].end = end;
          _out[k].tier = tier;
        } else if (_out[k].end == end && tier > _out[k].tier) {
          _out[k].tier = tier;
        }
        return;
      }
    assert(_count < _cap && "a position filled the room its table sized");
    if (_count < _cap) _out[_count++] = Scan{end, term, tier};
  }

  static std::uint16_t walk_to(std::uint16_t st, const char* src,
                               TextOffset from, TextOffset end) {
    for (auto at = from; at < end && st != kDeadState; ++at)
      st = A.next_byte(st, static_cast<unsigned char>(src[at]));
    return st;
  }

  static bool reserved_at(std::uint16_t st, const char* src, TextOffset len,
                          TextOffset from, Scan match) {
    st = walk_to(st, src, from, match.end);
    if (st == kDeadState) return false;
    const auto refused = lr_refused_from<A>(src, from, len);
    TextOffset unseen = match.end;
    return A.dfa_res_set(st).test(match.term) &&
           lex_reserves<A>(st, src, match.end, len, unseen,
                           [&](SymbolId t) { return !refused(t); });
  }
};

}  // namespace cursiv
