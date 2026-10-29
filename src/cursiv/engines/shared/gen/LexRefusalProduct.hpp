// A refusal no matcher took, spelled by the token graph: the product of its
// word with the continuation it refuses, one level per byte of the word read.
// No match stands on more than one class past the token, of bytes or of
// scalars, so nothing a lexer reads is ever taken back; what the product
// cannot spell is a fault.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LexNfaBuild.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LexWordClass.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/gen/lex_prefix_meets.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

// A word its own turn reaches again; a match ending where the word does,
// which only a class past it would rescue; a match ending short of the word;
// a word running into a relay's content; a rescue read on over a scalar the
// word class leaves out.
enum class LexRefusalFault : std::uint8_t {
  inLoop,
  endsOnWord,
  endsShort,
  intoRelay,
  readsPast
};

// `refuse(fault, guard, bytes)` names what the product cannot spell.
template <class Cfg, class Refuse>
struct LexRefusalProduct {
  LexNfaPool& n;
  Cfg& g;
  Refuse refuse;
  // The refusals a matcher took: the product leaves them be.
  const std::vector<bool>& taken;
  // Per refusal, the one read in a row right in front of it, the only way
  // into it, -1 for none; and the frontier those in front lay, once asked.
  std::vector<int> front{};
  std::vector<int> aheadAt{};
  std::vector<ByteSet> aheadBytes{};
  std::vector<LexCodePoints> aheadCps{};
  // What each refusal lays in front of the ones behind it, asked once.
  struct Laid {
    bool known = false, single = false, scalar = false;
    ByteSet byte{};
    LexCodePoints cps{};
  };
  std::vector<Laid> laid{};

  // Every refusal `taken` leaves, once the graph it points into is complete.
  void run() {
    // The refusal whose hole `to` is, -1 for none.
    const auto owner = [&](int to) {
      return to < 0 ? -1 : n.s[static_cast<std::size_t>(to)].guard;
    };
    front.assign(n.guards.size(), -1);
    for (std::size_t i = 0; i < n.guards.size(); ++i)
      if (const int at = owner(n.guards[i].out); at >= 0) {
        int& f = front[static_cast<std::size_t>(at)];
        f = f != -1 || taken[i] ? -2 : static_cast<int>(i);
      }
    const auto entered = [&](int to) {
      if (const int at = owner(to); at >= 0)
        front[static_cast<std::size_t>(at)] = -2;
    };
    for (const LexNfaState& st : n.s) {
      entered(st.t1);
      entered(st.t2);
    }
    for (const LexRelayBuild& r : n.relays) entered(r.resumeOut);
    aheadAt.assign(n.guards.size(), -1);
    aheadBytes.assign(n.guards.size(), ByteSet{});
    aheadCps.assign(n.guards.size(), LexCodePoints{});
    laid.assign(n.guards.size(), Laid{});
    for (std::size_t k = 0; k < n.guards.size(); ++k) {
      if (taken[k]) continue;
      std::map<std::pair<unsigned, int>, int> memo;
      level(k, memo, 0, n.guards[k].out, n.guards[k].hole);
    }
  }

 private:
  // What the refusals in a row in front of refusal `k` refuse where a match
  // ends there: classes of one byte, and words of one scalar. Where one is
  // such a word, the first refusal read lays the whole frontier, so the
  // others find it covered; false where none is.
  bool ahead(std::size_t k) {
    if (aheadAt[k] >= 0) return aheadAt[k] == 1;
    bool scalars = false;
    LexCodePoints all;
    std::size_t steps = 0;
    for (int j = front[k]; j >= 0 && steps++ < n.guards.size();
         j = front[static_cast<std::size_t>(j)]) {
      Laid& word = laid[static_cast<std::size_t>(j)];
      if (!word.known) {
        word.known = true;
        const LexNode e = g.lex[n.guards[static_cast<std::size_t>(j)].node];
        if (e.a == kNoSet && e.min == 1) {
          word.single = true;
          word.byte = g.sets[g.lexKid[e.b]];
          word.cps = lex_set_cps(g, g.lexKid[e.b]);
        } else if (e.a == kNoSet && one_scalar(e, word.cps)) {
          word.scalar = true;
        }
      }
      if (word.single) aheadBytes[k] |= word.byte;
      scalars = scalars || word.scalar;
      all.ranges.insert(all.ranges.end(), word.cps.ranges.begin(),
                        word.cps.ranges.end());
    }
    aheadCps[k].unite(all);
    aheadAt[k] = scalars ? 1 : 0;
    return scalars;
  }

  // Whether every continuation of this prefix admits a match somewhere, over
  // the NFA subsets, each once: `[^\n]* ![^\n]` is total. A match ends before
  // a byte its bound neither refuses nor decodes at: where the bound decodes,
  // the scalar there may be one it refuses, so the walk reads on.
  bool total(std::vector<int> start) const {
    std::sort(start.begin(), start.end());
    std::vector<std::vector<int>> work{start};
    std::set<std::vector<int>> seen{std::move(start)};
    for (std::size_t i = 0; i < work.size(); ++i) {
      const auto current = work[i];
      if (!lex_accepts(n, current)) return false;
      for (unsigned b = 0; b < 256; ++b) {
        const auto c = static_cast<unsigned char>(b);
        if (std::ranges::any_of(current, [&](int x) {
              const auto& s = n.s[x];
              return s.accept >= 0 &&
                     (s.bound < 0 ||
                      !(g.sets[s.bound].has(c) ||
                        lex_decode_bytes(g, static_cast<std::uint16_t>(s.bound))
                            .has(c)));
            }))
          continue;
        std::vector<int> next = lex_nfa_step(n, g.sets.data(), current, c);
        if (next.empty()) return false;
        if (!seen.insert(next).second) continue;
        work.push_back(std::move(next));
      }
    }
    return true;
  }

  LexCodePoints word_of(std::uint16_t rescue) const {
    LexCodePoints word;
    const auto at = std::ranges::find_if(
        g.sectionWords.v,
        [rescue](const LexWordClass& w) { return w.set == rescue; });
    word.ranges = at != g.sectionWords.v.end() ? at->wide : g.wordRanges.v;
    return word;
  }

  template <class Whole>
  bool scalars_total(const std::vector<int>& cl, Whole&& whole,
                     std::uint16_t rescue) const {
    const LexCodePoints word = word_of(rescue);
    bool dies = false;
    const auto ends =
        lex_scalar_ends(n, g.sets.data(), cl, word.sequences(), dies);
    return !dies && std::ranges::all_of(ends, whole);
  }

  bool reads_outside(const std::vector<int>& cl, const ByteSet& leads,
                     std::uint16_t rescue) const {
    std::vector<LexCodePoints::Seq> seqs;
    const LexCodePoints word = word_of(rescue);
    for (const LexCodePoints::Seq& q :
         LexCodePoints::outside(word.ranges.data(), word.ranges.size())
             .sequences()) {
      ByteSet lead;
      lead.add(q.lo[0], q.hi[0]);
      if (lead.intersects(leads)) seqs.push_back(q);
    }
    bool dies = false;
    return !lex_scalar_ends(n, g.sets.data(), cl, seqs, dies).empty();
  }

  // Whether word `e` is one scalar, a well-formed sequence of more than one
  // byte, and which.
  bool one_scalar(const LexNode& e, LexCodePoints& out) const {
    if (e.min < 2 || e.min > 4) return false;
    const ByteSet* cls[4];
    for (unsigned i = 0; i < e.min; ++i) {
      const std::uint16_t set = g.lexKid[e.b + i];
      if (g.setDecode[set] != kNoSet) return false;
      cls[i] = &g.sets[set];
    }
    return LexCodePoints::spelled(cls, e.min, out);
  }

  // A refusal whose last byte is a nested relay's first content byte: the
  // walk reads that byte off the refused class, and the search starts a byte
  // behind. A longer stretch, a rescue class or a decoded class is not one
  // byte.
  int relay_refusal(std::size_t k, const LexNode& e, unsigned lvl, int r) {
    const bool ends = n.relays[static_cast<std::size_t>(r)].row.nested &&
                      e.a == kNoSet && lvl + 1 == e.min;
    if (!ends || lex_decode_bytes(g, g.lexKid[e.b + lvl]).any()) {
      refuse(LexRefusalFault::intoRelay, k, ByteSet{});
      return -1;
    }
    const ByteSet off = lex_not(g.sets[g.lexKid[e.b + lvl]]);
    if (!off.any()) return -1;
    LexRelayBuild shifted = n.relays[static_cast<std::size_t>(r)];
    shifted.row.back = 1;
    const auto same = std::find_if(
        n.relays.begin(), n.relays.end(), [&](const LexRelayBuild& x) {
          return x.row == shifted.row && x.resumeOut == shifted.resumeOut;
        });
    const int at = same == n.relays.end()
                       ? lex_push_relay(n, shifted, shifted.resumeOut)
                       : static_cast<int>(same - n.relays.begin());
    return n.chars(lex_intern_set(g, off), n.relay(at));
  }

  // `lvl` bytes of guard `k`'s word spelled and the continuation at `q`; each
  // level re-spells the continuation's edges, `into` the state it fills.
  int level(std::size_t k, std::map<std::pair<unsigned, int>, int>& memo,
            unsigned lvl, int q, int into) {
    if (const auto at = memo.find({lvl, q}); at != memo.end())
      return at->second;
    const LexNode e = g.lex[n.guards[k].node];
    const bool done = lvl == e.min;
    // The word fully spelled and nothing rescuing it: the reading is dead.
    if (done && e.a == kNoSet) return -1;
    const std::vector<int> cl = n.eclose(q);
    const ByteSet scalars = done ? lex_decode_bytes(g, e.a) : ByteSet{};
    // At the word's end, whether the body reads on over every byte and scalar
    // the rescue class holds: then a longer match takes every rescued text,
    // and a match ending here is only the reading the refusal kills.
    bool rescued = done;
    ByteSet stranded;
    std::map<std::vector<int>, bool> totals;
    const auto whole = [&](const std::vector<int>& sub) {
      const auto [at, fresh] = totals.try_emplace(sub, false);
      if (fresh) at->second = total(sub);
      return at->second;
    };
    if (done)
      for (unsigned b = 0; b < 256; ++b) {
        const auto c = static_cast<unsigned char>(b);
        if (g.sets[e.a].has(c) &&
            !whole(lex_nfa_step(n, g.sets.data(), cl, c))) {
          rescued = false;
          stranded.add(c);
        }
      }
    if (rescued && scalars.any() && !scalars_total(cl, whole, e.a)) {
      rescued = false;
      stranded |= scalars;
    }
    const std::uint16_t wantIdx = done ? kNoSet : g.lexKid[e.b + lvl];
    const std::uint16_t pick = done ? e.a : wantIdx;
    // Past the word, a byte the rescue class decodes at goes on too.
    const ByteSet pickSet = g.sets[pick] | scalars;
    int chain = -1;
    bool leads = false;
    const auto add = [&](int x) { chain = chain < 0 ? x : n.split(x, chain); };
    for (int x : cl) {
      const LexNfaState st = n.s[static_cast<std::size_t>(x)];
      if (st.set >= 0) {
        ByteSet on = g.sets[st.set] & pickSet;
        if (st.minus >= 0) on.andnot(g.sets[st.minus]);
        if (on.any()) {
          const int to = done ? st.t1 : level(k, memo, lvl + 1, st.t1, -1);
          // A level that keeps nothing is not an edge.
          if (to >= 0) {
            leads = leads || ByteSet{on}.andnot(g.sets[pick]).any();
            const std::uint16_t idx =
                on == g.sets[pick] ? pick : lex_intern_set(g, on);
            add(n.push({.set = static_cast<int>(idx),
                        .t1 = to,
                        .stray = st.stray,
                        .strayTerm = st.strayTerm}));
          }
        }
        if (done) continue;
        if (st.minus < 0) {
          add(n.push({.set = st.set,
                      .t1 = st.t1,
                      .minus = wantIdx,
                      .stray = st.stray,
                      .strayTerm = st.strayTerm}));
        } else {
          ByteSet off = g.sets[st.set];
          off.andnot(g.sets[wantIdx]).andnot(g.sets[st.minus]);
          if (off.any())
            add(n.push({.set = lex_intern_set(g, off),
                        .t1 = st.t1,
                        .stray = st.stray,
                        .strayTerm = st.strayTerm}));
        }
      } else if (st.accept >= 0 && done) {
        if (!rescued) refuse(LexRefusalFault::endsOnWord, k, stranded);
      } else if (st.accept >= 0) {
        // The token ends inside the word: the last byte left is one class at
        // the frontier, and so is a whole word of one scalar, decoded; more
        // is text past the token.
        LexCodePoints own;
        if (st.bound < 0 ? !g.sets[wantIdx].any() &&
                               !lex_decode_bytes(g, wantIdx).any()
                         : lex_bound_covers(g, st.bound, wantIdx)) {
          add(x);
        } else if (lvl == 0 && e.a == kNoSet &&
                   (e.min == 1 ? ahead(k) : one_scalar(e, own))) {
          ahead(k);
          ByteSet bytes = aheadBytes[k];
          own.unite(aheadCps[k]);
          if (e.min == 1) {
            bytes |= g.sets[wantIdx];
            own.unite(lex_set_cps(g, wantIdx));
          }
          if (st.bound >= 0) {
            bytes |= g.sets[st.bound];
            own.unite(lex_set_cps(g, st.bound));
          }
          const std::uint16_t to = lex_intern_cps_bound(g, bytes, own);
          add(to == st.bound ? x : n.match_bounded(st.accept, to, st.lit));
        } else if (lvl + 1 != e.min || e.a != kNoSet) {
          refuse(LexRefusalFault::endsShort, k, ByteSet{});
        } else if (st.bound < 0) {
          add(n.match_bounded(st.accept, wantIdx, st.lit));
        } else {
          add(n.match_bounded(st.accept, lex_bound_union(g, st.bound, wantIdx),
                              st.lit));
        }
      } else {
        if (st.relay >= 0) {
          const int to = relay_refusal(k, e, lvl, st.relay);
          if (to >= 0) add(to);
        }
        // A refusal not yet resolved stands in the way, its own hole
        // included: its continuation is not there to be narrowed.
        if (st.guard >= static_cast<int>(k) &&
            !taken[static_cast<std::size_t>(st.guard)])
          refuse(LexRefusalFault::inLoop, k, ByteSet{});
      }
    }
    // A byte past the word that only a decoded scalar rescues: what the body
    // reads from it must be rescued, or the edge reads a refused word on.
    if (leads && reads_outside(cl, scalars, e.a))
      refuse(LexRefusalFault::readsPast, k, ByteSet{});
    const int self = into >= 0 ? into : (chain < 0 ? -1 : n.hole());
    if (self >= 0) n.s[static_cast<std::size_t>(self)].t1 = chain;
    return memo[{lvl, q}] = self;
  }
};

// The spine refusals a matcher asks: those at a terminal's head, and behind
// it those the product cannot spell without text past the token -- tried on a
// copy until what is taken leaves no other such. `offsets` are the spine's.
// The last trial is the product: `n` and the set pools keep it, and what it
// cannot spell is named on the verdicts.
template <class Cfg>
std::vector<bool> lex_matcher_takes(LexNfaPool& n, Cfg& g,
                                    const std::vector<int>& offsets) {
  std::vector<bool> taken(n.guards.size(), false);
  for (std::size_t k = 0; k < n.guards.size(); ++k) taken[k] = offsets[k] == 0;
  // A trial only appends to the set pools: `lex_intern_set` and
  // `lex_intern_cps_bound` write behind `nsets` and the end of
  // `scalarRanges`, and never change an entry already there. Cutting each pool
  // back to its size and `nsets` to its count therefore undoes a trial.
  const auto nsets = g.nsets;
  const auto setsSize = g.sets.size();
  const auto decodeSize = g.setDecode.size();
  const auto scalarsSize = g.setScalars.size();
  const auto rangesSize = g.scalarRanges.size();
  struct Fault {
    LexRefusalFault why;
    std::size_t k;
    ByteSet bytes;
  };
  std::vector<Fault> faults;
  for (LexNfaPool trial;;) {
    trial = n;
    for (std::size_t k = 0; k < n.guards.size(); ++k)
      if (taken[k])
        trial.s[static_cast<std::size_t>(trial.guards[k].hole)].t1 =
            trial.guards[k].out;
    faults.clear();
    const auto mark = [&](LexRefusalFault why, std::size_t k,
                          const ByteSet& bytes) {
      faults.push_back({why, k, bytes});
    };
    LexRefusalProduct<Cfg, decltype(mark)>{trial, g, mark, taken}.run();
    bool grew = false;
    for (const Fault& f : faults)
      if (offsets[f.k] > 0 && !taken[f.k]) {
        taken[f.k] = true;
        grew = true;
      }
    if (!grew) {
      n = std::move(trial);
      break;
    }
    g.nsets = nsets;
    g.sets.truncate(setsSize);
    g.setDecode.truncate(decodeSize);
    g.setScalars.truncate(scalarsSize);
    g.scalarRanges.truncate(rangesSize);
  }
  for (const auto& [why, k, bytes] : faults) {
    const LexNfaGuard& gd = n.guards[k];
    switch (why) {
      case LexRefusalFault::inLoop:
        lower_refuse(g, LowerFail::wordRefusedInLoop, gd.term);
        break;
      case LexRefusalFault::endsOnWord:
        lower_refuse(g, LowerFail::rescuedRefusalPastMatch, gd.term, bytes);
        break;
      case LexRefusalFault::endsShort:
        if (!g.lowerFail()) g.lowerWord = gd.node;
        lower_refuse(g, LowerFail::wordPastMatch, gd.term);
        break;
      case LexRefusalFault::intoRelay:
        lower_refuse(g, LowerFail::refusalIntoRelay, gd.term);
        break;
      case LexRefusalFault::readsPast:
        lower_refuse(g, LowerFail::rescueReadsPast, gd.term);
        break;
    }
  }
  return taken;
}

}  // namespace cursiv
