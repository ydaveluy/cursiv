// What a prediction reads of the text: the token a lookahead reaches under an
// alphabet, trivia crossed as the choice's configuration crosses it, and every
// terminal the readings still admit over one spelling.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlLookIndent.hpp"
#include "cursiv/engines/ll/runtime/ll_indent.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"

namespace cursiv {

// What a lookahead may read next, as a mask over the terminals. The text is
// read through it rather than under an alphabet of its own: one tokenisation
// serves every depth, and a spelling two terminals match is read as every
// terminal the readings still admit.
//
// As wide as the grammar's terminals, which the table states: a prediction
// runs once per choice of the text and allocates nothing.
template <std::size_t Words>
class LlMask {
 public:
  void reset() { _w.fill(0); }
  void set(std::uint16_t t) { _w[t >> 6] |= std::uint64_t{1} << (t & 63); }
  void reset(std::uint16_t t) { _w[t >> 6] &= ~(std::uint64_t{1} << (t & 63)); }
  bool test(std::uint16_t t) const {
    const std::uint32_t k = t >> 6;
    return k < Words && ((_w[k] >> (t & 63)) & 1) != 0;
  }
  std::uint64_t word(std::uint32_t k) const { return k < Words ? _w[k] : 0; }
  void assign(const std::uint64_t* row,
              const std::array<std::uint64_t, Words>& mask) {
    for (std::size_t k = 0; k < Words; ++k) _w[k] = row[k] & mask[k];
  }

 private:
  std::array<std::uint64_t, Words> _w{};
};

template <const auto& T>
inline constexpr auto kLlVisibleTerms = [] {
  std::array<std::uint64_t, T.termWords> mask{};
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (!T.terms[t].trivia) mask[t >> 6] |= std::uint64_t{1} << (t & 63);
  return mask;
}();

// The scratch a prediction reads and writes, kept between calls: it is the hot
// path of every choice the construction left open.
template <std::size_t Words>
struct LlScratch {
  LlMask<Words> viable;
  std::vector<std::uint16_t> read;
};

template <std::size_t Words>
inline LlScratch<Words>& ll_scratch() {
  thread_local LlScratch<Words> one;
  return one;
}

template <const auto& T>
inline bool ll_reserves_in(std::uint16_t state,
                           const LlMask<T.termWords>& viable, const char* src,
                           TextOffset at, TextOffset len,
                           TextOffset& inspected) {
  return lex_reserves<T>(state, src, at, len, inspected,
                            [&](SymbolId term) { return viable.test(term); });
}

struct LlLexReading {
  std::uint16_t term;
  TextOffset end;
  std::uint8_t tier;
};

// The terminals a position admits, without those refusing a word the text
// spells there; `room` holds the copy where one is taken off.
template <const auto& T>
inline const LlMask<T.termWords>& ll_unrefused_mask(
    const LlMask<T.termWords>& viable, const char* src, TextOffset from,
    TextOffset len, LlMask<T.termWords>& room) {
  if constexpr (!LexTape<T>::kRefuses) {
    (void)src, (void)from, (void)len, (void)room;
    return viable;
  } else {
    bool trimmed = false;
    for (const std::uint16_t t : kLlRefusing<T>)
      if (viable.test(t) && T.head_refuses(t, src + from, len - from)) {
        if (!trimmed) room = viable;
        trimmed = true;
        room.reset(t);
      }
    return trimmed ? room : viable;
  }
}

// Whether a walk in `st` can still accept a terminal of `wanted`.
template <const auto& T>
inline bool ll_wants_ahead(std::uint16_t st,
                           const LlMask<T.termWords>& wanted) {
  if (T.lexLive == nullptr) return true;
  const std::uint64_t* const live = T.dfa_live_set(st).w;
  for (std::uint32_t k = 0; k < T.termWords; ++k)
    if ((live[k] & wanted.word(k)) != 0) return true;
  return false;
}

// Longest match, restricted, and ALL of it: at the longest accepting state
// every terminal the readings still admit is a reading of this spelling.
// Naming one of them would kill the arms that wanted another -- a guarded `=`
// and a bare one are two terminals over one byte. `lengths`, where asked,
// takes the readings at that longest extent.
template <const auto& T, bool Collect>
inline TextOffset ll_match_in_impl(
    const char* src, TextOffset len, TextOffset from,
    const LlMask<T.termWords>& wanted, std::vector<std::uint16_t>& out,
    TextOffset* inspected, bool* ended, bool* different,
    std::vector<LlLexReading>* lengths, std::uint16_t mode = T.errMode,
    std::size_t* top = nullptr) {
  LlMask<T.termWords> room;
  const LlMask<T.termWords>& viable =
      ll_unrefused_mask<T>(wanted, src, from, len, room);
  TextOffset i = from;
  TextOffset end = from;
  TextOffset far = from;
  std::uint16_t best = kDeadState;
  bool shorter = false;
  std::array<std::uint64_t, T.termWords> previous;
  std::conditional_t<Collect, std::array<TextOffset, T.nterms>,
                     std::array<TextOffset, 0>>
      terminalEnd;
  std::conditional_t<Collect, std::array<std::uint8_t, T.nterms>,
                     std::array<std::uint8_t, 0>>
      terminalTier;
  if constexpr (Collect) {
    terminalEnd.fill(from);
    terminalTier.fill(0);
  }
  if (different != nullptr) {
    *different = false;
    previous.fill(0);
  }
  // Keywords can cut trivia paths, so only visible readings use a narrower
  // alphabet. It must still cover every requested terminal.
  if (mode >= T.nmodes) mode = T.errMode;
  if (mode != T.errMode)
    for (std::uint32_t k = 0; k < T.termWords; ++k)
      if ((viable.word(k) & ~(T.modeTerms[std::size_t{mode} * T.termWords + k] &
                              kLlVisibleTerms<T>[k])) != 0) {
        mode = T.errMode;
        break;
      }
  std::uint16_t st = T.modeStart[mode];
  LlLexTape<T> tape;
  [[maybe_unused]] std::conditional_t<T.nrelays != 0, LexRelayRun<LlLexTape<T>>,
                                      bool> relays{};
  for (;;) {
    if (T.dfaAccept[st] != 0 && i < len && T.lex_bounded(st))
      far = std::max(far, tape.bound_reach(st, src, i, len));
    const bool checked = T.lexChecks != nullptr && T.lexChecks[st] != 0;
    if (T.dfaAccept[st] != 0 &&
        (checked || !lex_bound_refuses(LlLexTape<T>{}, st, src, i, len))) {
      // Word by word, not terminal by terminal: the match asks this at every
      // accepting state it passes, and a grammar has ninety terminals where
      // it has two words of them.
      const std::uint64_t* const acc = T.dfa_acc_set(st).w;
      const bool reserve = ll_reserves_in<T>(st, viable, src, i, len, far);
      const std::uint64_t* const res = reserve ? T.dfa_res_set(st).w : nullptr;
      std::uint64_t any = 0;
      for (std::uint32_t k = 0; k < T.termWords; ++k) {
        if constexpr (Collect) {
          if (reserve) {
            // A complete reserved word excludes its identifier reading, rather
            // than leaving an identifier over a shorter prefix of that word.
            for (auto bits = acc[k] & res[k] & viable.word(k); bits != 0;
                 bits &= bits - 1u) {
              const auto term =
                  static_cast<SymbolId>(k * 64u + std::countr_zero(bits));
              if (ll_lex_accepts<T>(st, term, src, i, len, far)) {
                terminalEnd[term] = from;
                terminalTier[term] = 0;
              }
            }
          }
        }
        auto bits =
            acc[k] & (reserve ? ~res[k] : ~std::uint64_t{0}) & viable.word(k);
        if (checked) {
          auto accepted = std::uint64_t{0};
          for (; bits != 0; bits &= bits - 1u) {
            const auto term =
                static_cast<SymbolId>(k * 64u + std::countr_zero(bits));
            if (ll_lex_accepts<T>(st, term, src, i, len, far))
              accepted |= std::uint64_t{1} << (term & 63u);
          }
          bits = accepted;
        }
        any |= bits;
        if constexpr (Collect)
          for (auto accepted = bits; accepted != 0; accepted &= accepted - 1u) {
            const auto term =
                static_cast<SymbolId>(k * 64u + std::countr_zero(accepted));
            terminalEnd[term] = i;
            terminalTier[term] = static_cast<std::uint8_t>(
                tape.tier(st, term, src, i, len, far));
          }
        if (different != nullptr) {
          if ((previous[k] & ~bits) != 0) shorter = true;
          previous[k] = bits;
        }
      }
      if (any != 0) {
        end = i;
        best = st;
      }
    }
    if constexpr (T.nrelays != 0)
      for (auto k = tape.relay(st); k != kNoRelay; k = tape.relay_of(k).next)
        if (viable.test(tape.relay_of(k).term))
          relays.launch(tape, k, src, len, from, i);
    if (i >= len) break;
    // Past the last state that leads to a terminal the readings want, the
    // walk can only die: a run of text no wanted token reads ends it here.
    if (!ll_wants_ahead<T>(st, viable)) break;
    far = std::max(far, i + 1u);
    const std::uint16_t to =
        T.next_byte(st, static_cast<unsigned char>(src[i]));
    if (to == kDeadState) break;
    ++i;
    if (to == st && lex_run_blind(tape, st)) {
      // One unbounded accepting state has the same terminal mask throughout
      // its run; only the final position can improve the longest match.
      while (i < len &&
             T.next_byte(st, static_cast<unsigned char>(src[i])) == st)
        ++i;
      far = std::max(far, i < len ? i + 1u : i);
    }
    st = to;
  }
  out.clear();
  if (best != kDeadState) {
    const std::uint64_t* const acc = T.dfa_acc_set(best).w;
    const bool reserve = ll_reserves_in<T>(best, viable, src, end, len, far);
    const std::uint64_t* const res = reserve ? T.dfa_res_set(best).w : nullptr;
    for (std::uint32_t k = 0; k < T.termWords; ++k) {
      std::uint64_t bits =
          acc[k] & (reserve ? ~res[k] : ~std::uint64_t{0}) & viable.word(k);
      while (bits != 0) {
        const auto term = static_cast<std::uint16_t>(
            k * 64 + static_cast<unsigned>(std::countr_zero(bits)));
        if (ll_lex_accepts<T>(best, term, src, end, len, far))
          out.push_back(term);
        bits &= bits - 1;
      }
    }
  }
  const TextOffset walkEnd = end;
  // The readings behind conditions that a literal's or keyword's branch read.
  [[maybe_unused]] std::conditional_t<(T.nrelays != 0), LlMask<T.termWords>,
                                      bool> fixed{};
  const auto relayWeight = [&](std::uint16_t term) -> unsigned {
    if constexpr (T.nrelays != 0)
      return fixed.test(term) ? 1u : 0u;
    else
      return 0u;
  };
  if constexpr (T.nrelays != 0) {
    if (relays.any()) {
      LlMask<T.termWords> behind;
      auto strongest = 0u;
      relays.finish(
          tape, src, len, far,
          [&](SymbolId term, TextOffset reach, unsigned as) {
            if (different != nullptr && viable.test(term) && reach != end &&
                reach != from && end != from)
              *different = true;
            if (!viable.test(term)) return;
            if (best != kDeadState && reach == walkEnd &&
                ll_reserves_in<T>(best, viable, src, walkEnd, len, far) &&
                T.dfa_res_set(best).test(term))
              return;
            if constexpr (Collect) {
              if (reach > terminalEnd[term]) {
                terminalEnd[term] = reach;
                terminalTier[term] = static_cast<std::uint8_t>(as);
              } else if (reach == terminalEnd[term] &&
                         as > terminalTier[term]) {
                terminalTier[term] = static_cast<std::uint8_t>(as);
              }
            }
            if (reach < end) return;
            if (reach > end) {
              end = reach;
              out.clear();
              behind.reset();
              fixed.reset();
              strongest = 0;
            }
            behind.set(term);
            if (as != 0) fixed.set(term);
            strongest = std::max(strongest, tape.taking(term, as));
            if (std::find(out.begin(), out.end(), term) == out.end())
              out.push_back(term);
          });
      // A reading behind conditions that a literal's or keyword's branch read
      // takes the spelling of the readings that end with it.
      if (strongest != 0) {
        const std::uint16_t walked = end == walkEnd ? best : kDeadState;
        std::erase_if(out, [&](std::uint16_t word) {
          const bool takes =
              behind.test(word)
                  ? tape.takes_relayed(strongest, relayWeight(word))
                  : walked != kDeadState && tape.takes(walked, strongest, word);
          if constexpr (Collect)
            if (takes) {
              terminalEnd[word] = from;
              terminalTier[word] = 0;
            }
          return takes;
        });
      }
    }
    // Relay rests report their last position, which may be a failing byte.
    if (far < len) ++far;
    std::sort(out.begin(), out.end());
  }
  if (top != nullptr) {
    *top = 0;
    if (out.size() > 1) {
      const auto tierOf = [&](std::uint16_t term) {
        const auto walked = best != kDeadState && end == walkEnd
                                ? tape.tier(best, term, src, end, len, far)
                                : 0u;
        return std::max(lex_weight(walked), relayWeight(term));
      };
      auto strongest = 0u;
      for (const std::uint16_t term : out)
        strongest = std::max(strongest, tierOf(term));
      const auto strong = [&](std::uint16_t term) {
        return tierOf(term) == strongest;
      };
      const auto cut = std::stable_partition(out.begin(), out.end(), strong);
      if (cut != out.end()) *top = static_cast<std::size_t>(cut - out.begin());
    }
  }
  if (different != nullptr && shorter) *different = true;
  // What a refused word the matcher asked read counts, whichever terminal
  // was elected: its reach is a halt, the byte there read.
  if constexpr (T.termReach != nullptr)
    if (from < len)
      for (const std::uint16_t t : kLlRefusing<T>)
        if (wanted.test(t)) {
          const TextOffset halt = from + T.termReach(t, src + from, len - from);
          far = std::max(far, halt < len ? halt + 1u : halt);
        }
  if (inspected != nullptr) *inspected = std::max(far, i);
  if (ended != nullptr) *ended = i == len || far >= len;
  if constexpr (Collect) {
    lengths->clear();
    for (std::uint16_t term = 1; term < T.nterms; ++term)
      if (terminalEnd[term] != from && terminalEnd[term] == end)
        lengths->push_back({term, terminalEnd[term], terminalTier[term]});
  }
  return end;
}

template <const auto& T>
inline TextOffset ll_match_in(const char* src, TextOffset len, TextOffset from,
                              const LlMask<T.termWords>& viable,
                              std::vector<std::uint16_t>& out,
                              TextOffset* inspected = nullptr,
                              bool* ended = nullptr, bool* different = nullptr,
                              std::uint16_t mode = T.errMode,
                              std::size_t* top = nullptr) {
  return ll_match_in_impl<T, false>(src, len, from, viable, out, inspected,
                                    ended, different, nullptr, mode, top);
}

template <const auto& T>
inline TextOffset ll_match_in(const char* src, TextOffset len, TextOffset from,
                              const LlMask<T.termWords>& viable,
                              std::vector<std::uint16_t>& out,
                              TextOffset* inspected, bool* ended,
                              bool* different,
                              std::vector<LlLexReading>* lengths,
                              std::uint16_t mode = T.errMode) {
  return ll_match_in_impl<T, true>(src, len, from, viable, out, inspected,
                                   ended, different, lengths, mode);
}

struct LlLookRead {
  Token token{};
  TextOffset inspected = 0;
  bool ended = false;
  bool different = false;
  // The readings of the highest tier lead the read, and this many are they;
  // zero where the read holds one tier.
  std::size_t top = 0;
};

// A head condition consumes within the candidate token, but a word guard
// can inspect beyond it. Keep that dependency with the prediction too.
template <const auto& T>
inline bool ll_predict_condition(const LlContext& c, std::uint16_t condition,
                                 const Token& token, TextOffset gapFrom,
                                 LlLookRead& dependency) {
  TextOffset seen = 0;
  const bool value =
      ll_condition_text<T>(std::string_view(c.src, c.len), condition, token,
                           gapFrom, ll_ends<T>(c, token.sym), &seen);
  dependency.inspected = std::max(dependency.inspected, seen);
  dependency.ended = dependency.ended || seen >= c.len;
  return value;
}

// Whether the token in hand may be several terminals: anywhere in a mode
// marked 1, and only where the text ends on it in one marked 2.
template <const auto& T>
inline bool ll_reads_rivals(const LlContext& c, std::uint16_t mode) {
  const std::uint8_t multi = T.modeMulti[mode];
  return multi != 0 && (multi == 1 || c.la.end == c.len);
}

template <const auto& T>
inline void ll_first_read(const LlContext& c, const LlTable::Decision& dec,
                          LlMask<T.termWords>& viable,
                          std::vector<std::uint16_t>& read,
                          LlLookRead* details = nullptr) {
  read.assign(1, c.la.sym);
  if (details != nullptr) *details = {c.la, c.la.end, c.la.sym == kEofSymbol};
  if (!ll_reads_rivals<T>(c, dec.mode) || c.la.sym == kEofSymbol ||
      c.la.sym == kInvalidSymbol || T.terms[c.la.sym].laid != 0)
    return;
  // The mode is already a bit row. Filter its trivia in words, rather than
  // testing every terminal again on every adaptive decision.
  viable.assign(T.modeTerms + std::size_t{dec.mode} * T.termWords,
                kLlVisibleTerms<T>);
  TextOffset inspected = c.la.end;
  bool ended = false;
  bool different = false;
  std::size_t top = 0;
  ll_match_in<T>(c.src, c.len, c.la.begin, viable, read, &inspected, &ended,
                 &different, dec.mode, &top);
  if (details != nullptr) {
    details->inspected = inspected;
    details->ended = ended;
    details->different = different;
    details->top = top;
  }
  if (read.empty()) read.assign(1, c.la.sym);
}

// Conditions and document EOF retain their own skip configuration.
template <const auto& T>
inline std::uint16_t ll_read_mode(std::uint32_t state) {
  if constexpr (T.lookMode != nullptr) {
    const LlAtnState& s = T.atn.state(state);
    for (std::uint32_t i = 0; i < s.nedges; ++i) {
      const auto& edge = T.atn.edge(s.edge + i);
      if (edge.kind != LlEdgeKind::atom && edge.kind != LlEdgeKind::condition)
        continue;
      const std::uint16_t hop = T.edgeNode[s.edge + i];
      if (hop != kNoRow) return T.nodes[hop].mode;
      if (edge.kind == LlEdgeKind::atom)
        return edge.label == kEofSymbol ? T.tailMode : kNoRow;
    }
  }
  (void)state;
  return kNoRow;
}

// What a mode skips: the blanks it crosses and those it crosses joined.
template <const auto& T>
constexpr std::uint32_t ll_skips_of(std::uint16_t mode) {
  return (std::uint32_t{T.modeTrivia[mode]} << 16) | T.modeJoinedTrivia[mode];
}

template <const auto& T>
inline LlLookIndent& ll_look_indent(const LlContext& c) {
  // Predictions have no user callbacks and finish before the descent can
  // ask another one: the room is retained between calls.
  thread_local LlLookIndent state;
  if constexpr (kIndented<T>) {
    ll_indent_copy(c, state);
    indent_shift<T>(state.arena, state.at, c.la);
    if (c.la.end != c.la.begin) state.held = c.la.end;
  }
  return state;
}

// A visible spelling from another configuration must not hide the trivia
// this point crosses. Elect only the active blanks, including joined lines
// while prediction stands inside brackets.
template <const auto& T>
inline Scan ll_look_blank(const LlContext& c, TextOffset p, std::uint16_t look,
                          bool joined, std::vector<std::uint16_t>& out,
                          TextOffset& inspected, bool& ended,
                          const LlMask<T.termWords>* viable = nullptr,
                          std::uint16_t scanMode = T.errMode) {
  const auto mode = joined ? T.modeJoinedTrivia[look] : T.modeTrivia[look];
  TextOffset halt = p;
  const Scan result = ll_match_text<T>(c.src, c.len, mode, p, halt);
  inspected = std::max(inspected, halt < c.len ? halt + 1u : halt);
  ended = ended || halt == c.len;
  if (result.term == kInvalidSymbol || result.end == p || viable == nullptr)
    return result;
  // A visible token at least as long owns its prefix, blanks included.
  LlMask<T.termWords> visible;
  bool active = false;
  for (std::size_t w = 0; w < T.termWords; ++w) {
    auto bits = viable->word(w) & kLlVisibleTerms<T>[w];
    while (bits != 0) {
      const auto t =
          static_cast<std::uint16_t>(w * 64u + std::countr_zero(bits));
      bits &= bits - 1u;
      if (joined && T.terms[t].joins) continue;
      visible.set(t);
      active = true;
    }
  }
  if (active) {
    TextOffset far = p;
    bool atEnd = false;
    const auto end = ll_match_in<T>(c.src, c.len, p, visible, out, &far, &atEnd,
                                    nullptr, scanMode);
    inspected = std::max(inspected, far);
    ended = ended || atEnd;
    if (end >= result.end) return {p, kInvalidSymbol};
  }
  return result;
}

// The token `k` ahead of the one in hand, trivia crossed as the choice's
// configuration crosses it. `from` walks with the caller: a lookahead reads
// forward and never rewinds.
template <const auto& T>
inline bool ll_look(const LlContext& c, TextOffset& from,
                    const LlMask<T.termWords>& viable,
                    std::vector<std::uint16_t>& out, std::uint16_t look,
                    LlLookRead* details = nullptr,
                    LlLookIndent* indent = nullptr, bool shift = true,
                    std::uint16_t scanMode = T.errMode) {
  TextOffset p = from;
  TextOffset inspected = p;
  bool ended = false;
  bool head = true;
  for (;;) {
    if constexpr (kIndented<T>) {
      if (indent != nullptr) {
        Token token;
        if (ll_indent_read<T>(
                c.src, c.len, indent->arena, indent->at, p, head, indent->held,
                token,
                ll_end_stands<T>([&](auto t) { return viable.test(t); }))) {
          out.clear();
          if (!viable.test(token.sym)) return false;
          out.push_back(token.sym);
          from = token.end;
          if (shift) {
            indent_shift<T>(indent->arena, indent->at, token);
            if (token.end != token.begin) indent->held = token.end;
          }
          if (details != nullptr) *details = {token, c.len, true};
          return true;
        }
      }
    }
    if (p >= c.len) break;
    const bool joined =
        kIndented<T> && indent != nullptr && indent->at.depth != 0;
    const Scan sc = ll_look_blank<T>(c, p, look, joined, out, inspected, ended,
                                     &viable, scanMode);
    if (sc.term == kInvalidSymbol || sc.end == p) break;
    assert(T.terms[sc.term].trivia || (joined && T.terms[sc.term].joins));
    p = sc.end;
    head = false;
  }
  if (p >= c.len) {
    from = p;
    out.assign(1, kEofSymbol);
    if (details != nullptr) *details = {{p, p, kEofSymbol}, inspected, true};
    return true;
  }
  TextOffset matched = p;
  bool matchEnded = false;
  bool different = false;
  std::size_t top = 0;
  const TextOffset end =
      ll_match_in<T>(c.src, c.len, p, viable, out, &matched, &matchEnded,
                     &different, scanMode, &top);
  from = end;
  if constexpr (kIndented<T>) {
    if (shift && indent != nullptr && !out.empty()) {
      indent_shift<T>(indent->arena, indent->at, {p, end, out.front()});
      if (end != p) indent->held = end;
    }
  }
  if (details != nullptr)
    *details = {{p, end, out.empty() ? kInvalidSymbol : out.front()},
                std::max(inspected, matched),
                ended || matchEnded,
                different,
                top};
  return end != p && !out.empty();
}

}  // namespace cursiv
