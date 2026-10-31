// The sets of the pools, interned once each: the lowering's classes and those
// a lexer construction invents when resolving a refusal spells classes the
// grammar never named.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/SetIndex.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

inline constexpr ByteSet kAllBytes{{~0ull, ~0ull, ~0ull, ~0ull}};

inline ByteSet lex_not(const ByteSet& s) {
  return ByteSet{kAllBytes}.andnot(s);
}

// Where a decoded set lists the scalars it tests: `at << 16 | count` into
// `scalarRanges`, zero for the word class's.
template <class Cfg>
std::uint32_t lex_set_scalars(const Cfg& g, std::uint16_t s) {
  return s < g.setScalars.size() ? g.setScalars[s] : 0;
}

// The set with these bytes, decoding and scalars, interned once: the pool's
// index answers, kept in step with the pool, which a trial may have cut back.
template <class Cfg>
std::uint16_t lex_intern_set(Cfg& c, const ByteSet& s,
                             std::uint16_t decode = kNoSet,
                             std::uint32_t scalars = 0) {
  detail::SetIndex& x = c.setIndex;
  x.sync(c);
  const detail::SetKey key{s, decode, scalars};
  if (const std::uint16_t* const at = x.find(key)) return *at;
  // Never pass the sentinel on as an NFA edge's set index.
  if (c.nsets == kNoSet) throw LowerRefused(LowerFail::tokenGraphTooLarge);
  c.sets[c.nsets] = s;
  c.setDecode[c.nsets] = decode;
  if (scalars != 0 || c.nsets < c.setScalars.size())
    c.setScalars[c.nsets] = scalars;
  x.add(key);
  return c.nsets++;
}

// The lead bytes a set decodes at: none for a plain class.
template <class Cfg>
ByteSet lex_decode_bytes(const Cfg& g, std::uint16_t s) {
  return g.setDecode[s] == kNoSet ? ByteSet{} : g.sets[g.setDecode[s]];
}

// Whether a set is a word class: the generated grammar's -- its bytes, and the
// lead bytes of its scalars -- or a section's, or a keyword's own.
template <class Cfg>
bool lex_word_set(const Cfg& g, std::uint16_t s) {
  return (g.sets[s] == g.wordBytes && lex_decode_bytes(g, s) == g.wordLeads &&
          lex_set_scalars(g, s) == 0) ||
         std::ranges::any_of(g.sectionWords.v,
                             [s](const auto& w) { return w.set == s; }) ||
         std::ranges::find(g.keywordBounds.v, s) != g.keywordBounds.v.end();
}

// The sets that are word classes: the sets are interned, so one stands for
// each.
template <class Cfg>
std::vector<std::uint16_t> lex_word_bounds(const Cfg& g) {
  std::vector<std::uint16_t> words;
  for (std::uint16_t s = 0; s < g.nsets; ++s)
    if (lex_word_set(g, s)) words.push_back(s);
  return words;
}

template <class Cfg>
std::vector<std::uint8_t> lex_word_flags(const Cfg& g) {
  std::vector<std::uint8_t> flags(g.nsets, 0);
  for (std::uint16_t s = 0; s < g.nsets; ++s)
    flags[s] = lex_word_set(g, s) ? 1 : 0;
  return flags;
}

// Whether a bound refuses every byte: its acceptance stands only where no byte
// follows.
template <class Cfg>
bool lex_bound_ends(const Cfg& g, std::uint16_t s) {
  return s != kNoSet && g.sets[s].count() == 256;
}

// The scalars a set refuses past its bytes: those of its list whose lead
// byte it decodes at.
template <class Cfg>
LexCodePoints lex_set_cps(const Cfg& g, std::uint16_t s) {
  if (g.setDecode[s] == kNoSet) return {};
  const std::uint32_t at = lex_set_scalars(g, s);
  const unicode::Range* const r =
      at == 0 ? g.wordRanges.data() : g.scalarRanges.data() + (at >> 16);
  LexCodePoints all;
  all.ranges.assign(r, r + (at == 0 ? g.wordRanges.size() : at & 0xFFFFu));
  return all.led(g.sets[g.setDecode[s]]);
}

// A bound refusing `bytes` outright and the scalars `cps` past them, its
// list shared with any set listing the same.
template <class Cfg>
std::uint16_t lex_intern_cps_bound(Cfg& g, const ByteSet& bytes,
                                   const LexCodePoints& cps) {
  const LexCodePoints kept = cps.led(lex_not(bytes));
  if (kept.ranges.empty()) return lex_intern_set(g, bytes);
  const std::size_t n = kept.ranges.size();
  std::uint32_t at = 0;
  for (std::uint16_t i = 0; i < g.nsets && at == 0; ++i) {
    const std::uint32_t k = lex_set_scalars(g, i);
    if ((k & 0xFFFFu) == n &&
        std::equal(kept.ranges.begin(), kept.ranges.end(),
                   g.scalarRanges.data() + (k >> 16)))
      at = k;
  }
  if (at == 0) {
    const std::size_t base = g.scalarRanges.size();
    if (n > 0xFFFFu || base > 0xFFFFu)
      throw LowerRefused(LowerFail::tokenGraphTooLarge);
    for (const unicode::Range& r : kept.ranges)
      g.scalarRanges[g.scalarRanges.size()] = r;
    at = static_cast<std::uint32_t>(base << 16 | n);
  }
  return lex_intern_set(g, bytes, lex_intern_set(g, kept.leads()), at);
}

template <class Cfg>
std::uint16_t lex_bound_union(Cfg& g, std::uint16_t a, std::uint16_t b) {
  if (lex_set_scalars(g, a) == 0 && lex_set_scalars(g, b) == 0) {
    const ByteSet bytes = g.sets[a] | g.sets[b];
    // A byte refused outright needs no decoding.
    ByteSet decode = lex_decode_bytes(g, a) | lex_decode_bytes(g, b);
    decode.andnot(bytes);
    return lex_intern_set(g, bytes,
                          decode.any() ? lex_intern_set(g, decode) : kNoSet);
  }
  LexCodePoints cps = lex_set_cps(g, a);
  cps.unite(lex_set_cps(g, b));
  return lex_intern_cps_bound(g, g.sets[a] | g.sets[b], cps);
}

template <class Cfg>
bool lex_bound_covers(const Cfg& g, std::uint16_t a, std::uint16_t b) {
  if (!g.sets[a].covers(g.sets[b])) return false;
  if (lex_set_scalars(g, a) == 0 && lex_set_scalars(g, b) == 0)
    return (g.sets[a] | lex_decode_bytes(g, a)).covers(lex_decode_bytes(g, b));
  return lex_set_cps(g, a).covers(lex_set_cps(g, b).led(lex_not(g.sets[a])));
}

}  // namespace cursiv
