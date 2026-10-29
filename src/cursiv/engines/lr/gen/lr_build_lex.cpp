// One NFA per terminal, the byte classes its edges are written in, and the
// subsets that make a DFA of each mode -- a state's alphabet.
#include "cursiv/engines/lr/gen/lr_build_lex.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrLexView.hpp"
#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/lr_intern_name.hpp"
#include "cursiv/engines/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/engines/shared/gen/LexBacktrack.hpp"
#include "cursiv/engines/shared/gen/LexDfaBuild.hpp"
#include "cursiv/engines/shared/gen/LexKeywordCut.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LexSuccessors.hpp"
#include "cursiv/engines/shared/gen/PredicateModes.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/gen/lex_reserve.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {
// A state reads under one configuration, the one every item's boundary is
// read under. An item at its own start owes its boundary to the call, whose
// item states it -- save one that closes a region, which stands past it.
std::vector<std::uint8_t> lr_state_cfgs(LrBuildTable& A,
                                        const LrPoolRows<LrItem>& clos,
                                        const std::vector<unsigned>& pick,
                                        const std::vector<DynSet>& first) {
  std::vector<std::uint8_t> cfg(A.nstates, 0);
  std::vector<std::uint32_t> calledAt(A.cfg.nnts, 0);
  std::uint32_t gen = 0;
  for (unsigned f = 0; f < A.nstates; ++f) {
    ++gen;
    for (const LrItem& it : clos[pick[f]])
      if (const auto b = A.cfg.nt_after(it.prod, it.dot); b != kNoRow)
        calledAt[b] = gen;
    bool seen = false;
    for (const LrItem& it : clos[pick[f]]) {
      const auto& p = A.cfg.prods[it.prod];
      const auto& build = A.cfg.prodBuild[it.prod];
      if (const auto b = A.cfg.nt_after(it.prod, it.dot);
          b != kNoRow && !A.nullable[b] && first[b].test(kEofSymbol) &&
          !first[b].more_than_one())
        continue;
      if (it.dot == 0 && calledAt[p.lhs] == gen && !build.closesRegion &&
          !(p.len == 0 && build.endOnly))
        continue;
      const std::uint8_t c = A.cfg.cfg_at(it.prod, it.dot);
      if (seen && c != cfg[f])
        lower_refuse(A.cfg, A.cfg.home_at(c) != A.cfg.home_at(cfg[f])
                                ? LowerFail::sectionPointShared
                                : LowerFail::regionBoundaryShared);
      cfg[f] = c;
      seen = true;
    }
  }
  return cfg;
}

std::vector<DynSet> lr_state_guard_firsts(const LrBuildTable& A,
                                          const LrPoolRows<LrItem>& clos,
                                          const std::vector<unsigned>& pick,
                                          const std::vector<DynSet>& firsts) {
  std::vector<DynSet> out(A.nstates);
  for (unsigned f = 0; f < A.nstates; ++f)
    for (const LrItem& it : clos[pick[f]])
      if (const auto k = A.cfg.guard_at(A.cfg.prods[it.prod], it.dot); k != 0)
        out[f] |= firsts[k - 1u];
  return out;
}

// What each section's grammar reads, by the section's own configuration: the
// terminals of its productions.
std::vector<DynSet> lr_section_reads(const LrBuildCfg& g) {
  std::vector<DynSet> own(g.cfgHome.size());
  for (std::uint16_t k = 0; k < g.nprods; ++k) {
    const LrLowProd& q = g.prods[k];
    for (unsigned i = 0; i < q.len; ++i)
      if (const std::uint16_t y = g.sym_at(q, i); is_term(y))
        own[g.prodBuild[k].home].set(y);
  }
  return own;
}

// The NFAs first: resolving a refusal spells classes the grammar never
// named, and the partition must see them.
LexAlphabet lr_lex_alphabet(LrBuildTable& A) {
  auto& g = A.cfg;
  // One bit per implied token laid at a position.
  constexpr unsigned kImpliedBits =
      std::numeric_limits<decltype(LrVirtualAt::inserted)>::digits;
  unsigned implied = 0;
  for (unsigned t = 1; t < g.nterms; ++t) {
    if (g.terms[t].laid != kLaidImplied) continue;
    ++implied;
    if (implied > kImpliedBits)
      lower_refuse(g, LowerFail::gapTooMany, static_cast<std::uint16_t>(t));
  }
  LexAlphabet lex;
  lex.terminals(A, lr_intern_name);
  return lex;
}

LexModes lr_lex_mode_sets(LrBuildTable& A,
                          const std::vector<std::uint8_t>& cfgState,
                          const std::vector<DynSet>& guardFirsts,
                          const std::vector<DynSet>& impliedFirsts,
                          const std::vector<DynSet>& flagged) {
  auto& g = A.cfg;
  LexModes r(g);
  const unsigned nT = g.nterms;
  // An implied token answers a refused token in hand: where it may stand,
  // the text its condition reads must lex, or a shorter token is taken and
  // kept.
  std::vector<std::pair<unsigned, DynSet>> implied;
  for (unsigned t = 1; t < nT; ++t)
    if (g.terms[t].laid == kLaidImplied)
      implied.emplace_back(t, impliedFirsts[g.terms[t].cond]);
  // A mode is also keyed by the section its states read, and the token that
  // ends their text: a state of a section never lends its mode to a state
  // outside it, nor one that reads the closing token to one that reduces on it.
  const bool sections = g.sections();
  const auto tag = [&](unsigned f, std::uint8_t k) -> std::uint32_t {
    const std::uint8_t h = g.home_at(k);
    if (h == 0) return 0;
    std::uint16_t end = g.cfgEnd[h];
    for (const LrAction& action : A.action_row(f))
      if (action.term == end && action.kind == LrActionKind::shift) end = 0;
    return (std::uint32_t{h} << 16) | end;
  };

  // A mode is tight where its configuration reads as a token some terminal
  // the grammar skips.
  std::vector<std::uint8_t> tightMode;
  std::vector<bool> tightSet;
  {
    const std::uint8_t* const cs = cfgState.data();
    const DynSet* const fol = A.follow.row(0, A.nstates);
    std::uint16_t* const mod = A.mode.row(0, A.nstates);
    for (unsigned f = 0; f < A.nstates; ++f) {
      DynSet want = fol[f];
      DynSet beyond = guardFirsts[f];
      for (const auto& [u, heads] : implied)
        if (fol[f].test(u)) beyond |= heads;
      want |= r.skips[cs[f]];
      want |= r.joins;
      want.reset(kEofSymbol);
      mod[f] = r.intern(want, sections ? tag(f, cs[f]) : 0,
                        longer_firsts(g, want, beyond));
      if (tightSet.size() <= mod[f]) {
        tightSet.resize(mod[f] + 1u, false);
        tightMode.resize(mod[f] + 1u, 0);
      }
      if (!tightSet[mod[f]]) {
        tightSet[mod[f]] = true;
        tightMode[mod[f]] = cs[f] < g.cfgShort.size() ? g.cfgShort[cs[f]] : 0;
      }
    }
  }
  A.errMode = r.error_mode(g);
  predicate_modes(g, r);
  if (sections) {
    const auto errOf = r.section_modes(g, lr_section_reads(g), A.errMode);
    A.sectionErr = errOf;
    const auto n = r.set.size();
    std::uint16_t* const err = A.errModeOf.row(0, n);
    std::uint16_t* const end = A.modeEnd.row(0, n);
    for (std::size_t m = 0; m < n; ++m) {
      err[m] = errOf[r.tag[m] >> 16];
      end[m] = static_cast<std::uint16_t>(r.tag[m]);
    }
  }
  A.nmodes = static_cast<std::uint16_t>(r.set.size());
  if (std::ranges::any_of(flagged, [](const DynSet& s) { return s.any(); })) {
    // Per mode, the flagged terminals of the states that read in it.
    DynSet* const out = A.modeFlag.row(0, A.nmodes);
    for (unsigned f = 0; f < A.nstates; ++f) out[A.mode[f]] |= flagged[f];
    for (unsigned m = 0; m < A.nmodes; ++m) out[m] &= r.set[m];
  }
  std::uint16_t* const longer = A.modeLonger.row(0, A.nmodes);
  for (unsigned m = 0; m < A.nmodes; ++m) longer[m] = r.longerMode[m];
  A.modeHeads = r.heads(g);
  std::uint8_t* const mt = A.modeTight.row(0, A.nmodes);
  for (unsigned m = 0; m < A.nmodes; ++m)
    mt[m] = m < tightMode.size() ? tightMode[m] : 0;
  return r;
}

namespace {

// The shortest text a mode reads from `from` to stand in `to`, each byte the
// lowest printable one of its class where the class holds one.
std::string lr_dfa_text(const LrBuildTable& A, const LexAlphabet& alpha,
                        unsigned from, unsigned to) {
  const unsigned ncls = alpha.ncls;
  std::vector<unsigned> shown(alpha.repByte);
  for (unsigned b = 0x7e; b > 0x20; --b) shown[A.cls[b]] = b;
  constexpr auto none = ~0u;
  std::vector<unsigned> back(A.ndfa, none), via(A.ndfa, 0), queue{from};
  back[from] = from;
  const std::uint16_t* const next = A.dfaNext.data();
  for (std::size_t i = 0; i < queue.size() && back[to] == none; ++i) {
    const unsigned s = queue[i];
    for (unsigned c = 0; c < ncls; ++c) {
      const unsigned r = next[std::size_t{s} * ncls + c];
      if (r == kDeadState || back[r] != none) continue;
      back[r] = s;
      via[r] = c;
      queue.push_back(r);
    }
  }
  std::string text;
  for (unsigned s = to; s != from && back[s] != none; s = back[s])
    text.push_back(static_cast<char>(shown[via[s]]));
  std::ranges::reverse(text);
  return text;
}

// The DFA of every mode over the alphabet's classes, minimised.
void lr_lex_dfa(LrBuildTable& A, const LexAlphabet& alpha,
                const LexModes& modes) {
  auto& g = A.cfg;
  LexKeywordCut cut = alpha.cut;
  const auto push_relay = [&A](const LexRelayGen& row) {
    A.relays[A.nrelays] = row;
    return std::size_t{A.nrelays++};
  };
  const auto write = [&](std::uint16_t self, const LexDfaReading& r) {
    A.dfaAccept[self] = r.best < 0 ? 0 : static_cast<std::uint16_t>(r.best + 1);
    A.dfaAcc[self] = r.acc;
    A.accBoundBase[self] = static_cast<std::uint32_t>(A.accBound.size());
    std::size_t rank = 0;
    for (std::size_t i = 0, j; i < r.accepts.size(); i = j, ++rank) {
      for (j = i; j < r.accepts.size() && r.accepts[j].term == r.accepts[i].term;)
        ++j;
      if (j - i > 1)
        lower_refuse(g, LowerFail::boundaryDisagrees, r.accepts[i].term);
      A.accBound[A.accBoundBase[self] + rank] = r.accepts[i].bound;
      A.accTier[A.accBoundBase[self] + rank] = r.accepts[i].tier;
      std::uint16_t stand = kNoSet;
      for (const LexDfaReading::Accept& own : r.stands) {
        if (own.term != r.accepts[i].term) continue;
        if (stand != kNoSet && stand != own.bound)
          lower_refuse(g, LowerFail::boundaryDisagrees, own.term);
        stand = own.bound;
      }
      A.accStand[A.accBoundBase[self] + rank] = stand;
    }
  };
  lex_build_dfa(A, alpha, cut, modes.set, push_relay, write);
  A.accBoundBase[A.ndfa] = static_cast<std::uint32_t>(A.accBound.size());
  // A state that accepts nothing still has its sets.
  A.dfaAcc.row(0, A.ndfa);
  A.dfaRes.row(0, A.ndfa);
  A.dfaRelayRes.row(0, A.ndfa);
}

// The reserve and the election it moves; answers the terminals a word
// boundary closes at each DFA state.
std::vector<DynSet> lr_lex_reserve(LrBuildTable& A) {
  const auto& g = A.cfg;
  const unsigned nT = g.nterms;
  const LexReserved reserved = lex_reserve(
      g, LrLexView{A}, A.ndfa, std::span(A.relays.data(), A.nrelays),
      [&](std::size_t s) -> const DynSet& {
        return A.dfa_acc_set(static_cast<std::uint16_t>(s));
      },
      [&](std::size_t s, auto&& each) {
        const auto state = static_cast<std::uint16_t>(s);
        const DynSet& accS = A.dfa_acc_set(state);
        each_bit(accS.w.data(), accS.w.size(), [&](std::size_t bit) {
          const auto t = static_cast<unsigned>(bit);
          if (t == 0 || t >= nT) return;
          each(t, A.dfa_acc_bound(state, t), A.dfa_acc_tier(state, t));
          if (const auto stand = A.dfa_acc_stand(state, t); stand != kNoSet)
            each(t, stand, unsigned{kTierKeyword});
        });
      },
      [&](std::size_t s, unsigned u) { A.dfaRes[s].set(u); },
      [&](std::size_t s, unsigned u) { A.dfaRelayRes[s].set(u); });

  // The election moves too.
  for (std::uint16_t s = 0; s < A.ndfa; ++s) {
    if (A.dfaAccept[s] == 0) continue;
    const DynSet& res = A.dfa_res_set(s);
    if (!res.test(A.dfaAccept[s] - 1u)) continue;
    const DynSet& accS = A.dfa_acc_set(s);
    int best = -1;
    each_bit(accS.w.data(), accS.w.size(), [&](std::size_t bit) {
      const auto t = static_cast<unsigned>(bit);
      if (t == 0 || t >= nT || res.test(t)) return;
      if (best < 0 || A.dfa_before(s, t, static_cast<unsigned>(best)))
        best = static_cast<int>(t);
    });
    if (best >= 0) A.dfaAccept[s] = static_cast<std::uint16_t>(best + 1);
  }
  // The boundary is the elected acceptance's own, never the terminal's.
  for (std::uint16_t s = 0; s < A.ndfa; ++s)
    A.dfaBound[s] =
        A.dfaAccept[s] == 0 ? kNoSet : A.dfa_acc_bound(s, A.dfaAccept[s] - 1u);
  return reserved.keywords;
}

// What a state accepts, as one reading of the DFA: the table's own, or the one
// it would have without the acceptances that stand only where no byte follows.
struct LrMultiView {
  const std::uint16_t* accept;
  const std::uint16_t* bound;
  const DynSet* acc;
};

// The modes that read several candidates under `v`; only the table's own
// reading is written to `A`.
std::vector<char> lr_multi_pass(LrBuildTable& A, const LexAlphabet& alpha,
                                const LexModes& modes,
                                const std::vector<DynSet>& wordAt,
                                const LrMultiView& v, bool commit) {
  auto& g = A.cfg;
  std::vector<char> multiOf(A.nmodes, 0);
  const unsigned nT = g.nterms;
  const std::vector<unsigned>& repByte = alpha.repByte;
  const unsigned ncls = alpha.ncls;
  // The classes of the keywords a state accepts, and what a byte class is
  // to them: a word byte only if every one holds it; a lead byte of their
  // scalars is sent both answers.
  std::vector<std::uint16_t> keys;
  const auto word_class = [&](unsigned c) -> unsigned {
    const auto byte = static_cast<unsigned char>(repByte[c]);
    unsigned sel = keys.empty() ? 0u : 2u;
    for (const std::uint16_t own : keys) {
      if (lex_decode_bytes(g, own).has(byte)) return 4;
      if (!g.sets[own].has(byte)) sel = 0;
    }
    return sel;
  };

  // A parser state's actions plus trivia. Only the diagnostic needs the
  // individual state; candidate detection uses their union per mode.
  const auto reads = [&](unsigned state) {
    DynSet out = modes.trivia;
    out |= modes.joins;
    for (const LrAction& action : A.action_row(state))
      if (action.term < nT) out.set(action.term);
    return out;
  };
  // Each row compacted to its distinct (successor, set) pairs.
  std::vector<std::uint32_t> edgeAt(A.ndfa + 1u);
  std::vector<std::uint32_t> edgeOf;
  std::vector<unsigned char> selMask(A.ndfa);
  {
    std::vector<char> seenPair(static_cast<std::size_t>(A.ndfa) * 8u);
    char* const seen = seenPair.data();
    std::uint32_t* const ea = edgeAt.data();
    unsigned char* const sm = selMask.data();
    const std::uint16_t* const nextRow = A.dfaNext.data();
    const std::uint16_t* const accept = v.accept;
    const std::uint16_t* const bound = v.bound;
    for (std::uint16_t s = 0; s < A.ndfa; ++s) {
      ea[s] = static_cast<std::uint32_t>(edgeOf.size());
      const ByteSet* boundary =
          accept[s] != 0 && bound[s] != kNoSet && bound[s] < g.sets.size()
              ? &g.sets[bound[s]]
              : nullptr;
      const std::uint16_t* const nr =
          nextRow + static_cast<std::size_t>(s) * ncls;
      keys.clear();
      each_bit(v.acc[s].w.data(), v.acc[s].w.size(), [&](std::size_t t) {
        const std::uint16_t own =
            t < nT ? A.dfa_acc_bound(s, static_cast<unsigned>(t)) : kNoSet;
        if (own != kNoSet && lex_word_set(g, own)) keys.push_back(own);
        const std::uint16_t stand =
            t < nT ? A.dfa_acc_stand(s, static_cast<unsigned>(t)) : kNoSet;
        if (stand != kNoSet && lex_word_set(g, stand)) keys.push_back(stand);
      });
      for (unsigned c = 0; c < ncls; ++c) {
        const std::uint16_t r = nr[c];
        if (r == kDeadState) continue;
        const unsigned sel =
            word_class(c) |
            (boundary && boundary->has(static_cast<unsigned char>(repByte[c])));
        const std::uint32_t key = (static_cast<std::uint32_t>(r) << 3) | sel;
        if (seen[key]) continue;
        seen[key] = 1;
        edgeOf.push_back(key);
        sm[s] |= static_cast<unsigned char>(1u << sel);
      }
      const std::uint32_t* const ed = edgeOf.data();
      for (std::size_t q = ea[s], qe = edgeOf.size(); q < qe; ++q)
        seen[ed[q]] = 0;
    }
    ea[A.ndfa] = static_cast<std::uint32_t>(edgeOf.size());
  }
  // The parser states of each mode.
  const LrBuckets<> mSt(A.nmodes, A.nstates,
                        [&](std::uint32_t f) { return A.mode[f]; });
  std::vector<DynSet> live(A.ndfa);
  DynSet relayTerms;
  for (unsigned k = 0; k < A.nrelays; ++k) relayTerms.set(A.relays[k].term);
  const DynSet empty{};
  DynSet* const lv = live.data();
  const std::uint32_t* const ea = edgeAt.data();
  const unsigned char* const sm = selMask.data();
  const std::uint16_t* const accept = v.accept;
  const std::uint16_t* const bound = v.bound;
  const DynSet* const accSet = v.acc;
  const DynSet* const resSet = A.dfaRes.data();
  const std::uint32_t* const ed = edgeOf.data();
  std::vector<unsigned> stkRow(A.ndfa);
  unsigned* const stk = stkRow.data();
  std::vector<char> queued(A.ndfa);
  char* const qd = queued.data();
  // What a state holds as candidates whatever mode reaches it: acceptances
  // less the reserved names, relays' tokens, and the names a refused keyword
  // hands back beyond their best.
  std::vector<DynSet> held(A.ndfa);
  for (std::uint16_t s = 0; s < A.ndfa; ++s) {
    DynSet acc = accSet[s];
    acc.andnot(resSet[s]);
    for (auto r = A.dfaRelay[s]; r != kNoRelay; r = A.relays[r].next)
      if (!A.relays[r].gate) acc.set(A.relays[r].term);
    DynSet lifted = accSet[s];
    lifted.andnot(wordAt[s]);
    int lwin = -1;
    each_bit(lifted.w.data(), lifted.w.size(), [&](std::size_t t) {
      if (lwin < 0 || A.dfa_before(s, static_cast<unsigned>(t),
                                   static_cast<unsigned>(lwin)))
        lwin = static_cast<int>(t);
    });
    if (lwin >= 0) {
      lifted.reset(static_cast<unsigned>(lwin));
      acc |= lifted;
    }
    held[s] = acc;
  }
  for (unsigned m = 0; m < A.nmodes; ++m) {
    const auto parserStates = mSt.of(m);
    if (parserStates.empty()) continue;
    // Read the reachable DFA directly. Its sorted state order also fixes
    // which witness the diagnostic reports.
    std::vector<unsigned> states{A.modeStart[m]};
    qd[states.front()] = 1;
    for (std::size_t i = 0; i < states.size(); ++i) {
      const unsigned s = states[i];
      for (std::uint32_t q = ea[s]; q < ea[s + 1]; ++q) {
        const unsigned to = ed[q] >> 3;
        if (qd[to]) continue;
        qd[to] = 1;
        states.push_back(to);
      }
    }
    std::ranges::sort(states);
    // A worklist: what a state sends changes only when its `live` grows,
    // and is one of a few sets the class picks.
    std::size_t wn = 0;
    for (auto it = states.rbegin(); it != states.rend(); ++it) {
      lv[*it] = empty;
      stk[wn++] = *it;
    }
    while (wn != 0) {
      const unsigned s = stk[--wn];
      qd[s] = 0;
      // Nothing of its own and nothing carried in: it sends nothing.
      if (accSet[s].none() && lv[s].none() && A.dfaRelay[s] == kNoRelay)
        continue;
      const DynSet& resS = resSet[s];
      const bool hasBound = accept[s] != 0 && bound[s] != kNoSet;
      const unsigned win = hasBound ? accept[s] - 1u : 0u;
      // Bit 1 of the choice is the word class, bit 0 the boundary, bit 2 a
      // lead byte the scalar decides -- both answers.
      const auto variant = [&](unsigned k) {
        DynSet got = accSet[s];
        for (auto r = A.dfaRelay[s]; r != kNoRelay; r = A.relays[r].next)
          if (!A.relays[r].gate) got.set(A.relays[r].term);
        if (k & 2u) got.andnot(wordAt[s]);  // the word class refuses it here
        if (k & 1u) got.reset(win);
        DynSet o = lv[s];
        o |= got;
        // Where the word class refused the keyword, the name is handed back.
        if ((k & 2u) == 0) o.andnot(resS);
        return o;
      };
      DynSet out[8];
      const unsigned char have = sm[s];
      for (unsigned k = 0; k < 8; ++k) {
        if (((have >> k) & 1u) == 0) continue;
        out[k] = variant(k & 3u);
        if (k & 4u) out[k] |= variant((k & 1u) | 2u);
      }
      for (std::uint32_t q = ea[s], qe = ea[s + 1]; q < qe; ++q) {
        const std::uint32_t key = ed[q];
        const unsigned r = key >> 3;
        if (!lv[r].add(out[key & 7u])) continue;
        if (!qd[r]) {
          qd[r] = 1;
          stk[wn++] = r;
        }
      }
    }
    // What triggers is a state's actions meeting an acceptance other than
    // the one the priority answers.
    bool multi = false;
    // What the mode's parser states read, unioned once.
    DynSet modeAct;
    for (const auto f : parserStates) modeAct |= reads(f);
    // Whether every parser state of the mode answers `t` as it answers
    // `win`, by reduces alone, and one answers it at all: which of the two is
    // elected changes nothing the mode's states do.
    std::map<std::pair<unsigned, unsigned>, bool> alike;
    const auto indifferent = [&](unsigned t, unsigned win) {
      const auto [at, fresh] = alike.try_emplace({t, win}, false);
      if (!fresh) return at->second;
      bool answered = false;
      for (const auto f : parserStates) {
        std::vector<std::pair<LrActionKind, std::uint16_t>> mine, theirs;
        for (const LrAction& x : A.action_row(f)) {
          if (x.term == t) mine.emplace_back(x.kind, x.arg);
          if (x.term == win) theirs.emplace_back(x.kind, x.arg);
        }
        if (mine != theirs) return false;
        for (const auto& action : mine)
          if (action.first != LrActionKind::reduce) return false;
        answered = answered || !mine.empty();
      }
      return at->second = answered;
    };
    for (const unsigned s : states) {
      DynSet acc = held[s];
      unsigned relayTerm = 0, relaying = 0;
      for (auto r = A.dfaRelay[s]; r != kNoRelay; r = A.relays[r].next) {
        if (A.relays[r].gate) continue;
        if (relaying == 0) relayTerm = A.relays[r].term;
        ++relaying;
      }
      // A relay launched earlier can finish at this cursor.
      DynSet relayed = lv[s];
      relayed &= relayTerms;
      acc |= relayed;
      if (!acc.any()) continue;
      const unsigned win = accept[s] != 0 ? accept[s] - 1u : relayTerm;
      DynSet accX = acc;
      if (relaying < 2) accX.reset(win);
      accX &= modeAct;
      const DynSet read = accX;
      each_bit(read.w.data(), read.w.size(), [&](std::size_t t) {
        if (indifferent(static_cast<unsigned>(t), win))
          accX.reset(static_cast<unsigned>(t));
      });
      if (!accX.any()) continue;
      // The first matching state fixes the diagnostic witness. The mode's
      // union already proves that at least one such state exists.
      if (commit && !A.multiCand && !multi) {
        for (const auto f : parserStates) {
          const DynSet af = reads(f);
          if (!accX.intersects(af)) continue;
          DynSet owed = acc;
          owed &= af;
          A.multiMode = static_cast<std::uint16_t>(m);
          for (unsigned t = 1; t < nT; ++t) {
            if (!owed.test(t)) continue;
            if (A.multiTerm == 0)
              A.multiTerm = static_cast<std::uint16_t>(t);
            else if (A.multiInto == 0)
              A.multiInto = static_cast<std::uint16_t>(t);
          }
          // Only a pair held on every way into the state meets on that text.
          if (held[s].test(A.multiTerm) &&
              (A.multiInto == 0 || held[s].test(A.multiInto)))
            A.multiText = lr_dfa_text(A, alpha, A.modeStart[m], s);
          break;
        }
      }
      // Any acceptance can win once another's own guard refuses.
      if (commit) {
        A.modeWin[m] |= acc;
        A.modeWin[m] |= lv[s];
      }
      multi = true;
    }
    multiOf[m] = multi;
    if (!commit) continue;
    // A trivia never stands as the token a reading holds.
    A.modeWin[m].andnot(modes.trivia);
    if (multi) {
      A.modeMulti[m] = 1;
      A.multiCand = true;
      // Every terminal the mode's DFA accepts anywhere: the room a sweep
      // fills on its way.
      DynSet all{};
      for (const unsigned st : states) {
        all |= accSet[st];
        for (auto r = A.dfaRelay[st]; r != kNoRelay; r = A.relays[r].next)
          all.set(A.relays[r].term);
      }
      unsigned wide = 0;
      for (unsigned t = 0; t < nT; ++t) wide += all.test(t) ? 1u : 0u;
      if (wide > A.maxCands) A.maxCands = static_cast<std::uint16_t>(wide);
    }
  }
  if (commit) {
    // One cell per mode, not only up to the last one owed a walk.
    A.modeMulti.row(0, A.nmodes);
    A.modeWin.row(0, A.nmodes);
  }
  return multiOf;
}

}  // namespace

std::vector<DynSet> lr_lex_automaton(LrBuildTable& A, const LexAlphabet& alpha,
                                     const LexModes& modes) {
  lr_lex_dfa(A, alpha, modes);
  auto words = lr_lex_reserve(A);
  A.modeBacktrack = lex_backtrack_modes(A, &A.dfaBacktrack);
  return words;
}

// A mode whose rivals meet only where the text ends keeps the table's single
// reading and answers its rivals there alone: it is marked 2, not 1.
void lr_lex_candidates(LrBuildTable& A, const LexAlphabet& alpha,
                       const LexModes& modes,
                       const std::vector<DynSet>& wordAt) {
  const auto& g = A.cfg;
  const LrMultiView real{A.dfaAccept.data(), A.dfaBound.data(),
                         A.dfaAcc.data()};
  const std::vector<char> multi =
      lr_multi_pass(A, alpha, modes, wordAt, real, true);
  if (!A.multiCand) return;
  std::vector<DynSet> acc(A.ndfa);
  std::vector<std::uint16_t> accept(A.ndfa), bound(A.ndfa, kNoSet);
  bool ends = false;
  for (std::uint16_t s = 0; s < A.ndfa; ++s) {
    acc[s] = A.dfaAcc[s];
    each_bit(A.dfaAcc[s].w.data(), A.dfaAcc[s].w.size(), [&](std::size_t t) {
      const std::uint16_t b = A.dfa_acc_bound(s, static_cast<unsigned>(t));
      if (!lex_bound_ends(g, b)) return;
      acc[s].reset(static_cast<unsigned>(t));
      ends = true;
    });
    const DynSet& res = A.dfa_res_set(s);
    int best = -1, open = -1;
    each_bit(acc[s].w.data(), acc[s].w.size(), [&](std::size_t t) {
      const auto u = static_cast<unsigned>(t);
      if (best < 0 || A.dfa_before(s, u, static_cast<unsigned>(best)))
        best = static_cast<int>(u);
      if (!res.test(u) &&
          (open < 0 || A.dfa_before(s, u, static_cast<unsigned>(open))))
        open = static_cast<int>(u);
    });
    if (best >= 0 && res.test(static_cast<unsigned>(best)) && open >= 0)
      best = open;
    accept[s] = best < 0 ? 0 : static_cast<std::uint16_t>(best + 1);
    if (best >= 0) bound[s] = A.dfa_acc_bound(s, static_cast<unsigned>(best));
  }
  if (!ends) return;
  const LrMultiView plain{accept.data(), bound.data(), acc.data()};
  const std::vector<char> strict =
      lr_multi_pass(A, alpha, modes, wordAt, plain, false);
  for (unsigned m = 0; m < A.nmodes; ++m)
    if (multi[m] && !strict[m]) A.modeMulti[m] = 2;
}

void lr_derive_lexical(const LrBuildTable& a, LrDerivedRows& rows) {
  rows.lexElseBase.clear();
  rows.lexElseRows.clear();
  rows.lexElseBase.reserve(a.ndfa + 1u);
  const ByteSet& leads = a.cfg.wordLeads;
  for (std::uint16_t s = 0; s < a.ndfa; ++s) {
    const auto first = static_cast<std::uint32_t>(rows.lexElseRows.size());
    rows.lexElseBase.push_back(first);
    const std::uint16_t bound = a.dfaBound[s];
    if (a.dfaAccept[s] != 0 && bound != kNoSet) {
      // What stands at `byte` once the boundary refused it, `word` saying
      // whether the scalar there is a word.
      const auto elect = [&](unsigned char byte, bool word) {
        const auto refuses = [&](std::uint16_t set) {
          return a.cfg.sets[set].has(byte) ||
                 (word && lex_decode_bytes(a.cfg, set).has(byte));
        };
        const auto& open = a.dfa_acc_set(s);
        const auto& closed = a.dfa_res_set(s);
        bool reserved = false;
        each_bit(open.w.data(), open.w.size(), [&](std::size_t bit) {
          if (bit >= a.cfg.nterms || a.cfg.terms[bit].trivia) return;
          for (const auto own :
               {a.dfa_acc_bound(s, static_cast<unsigned>(bit)),
                a.dfa_acc_stand(s, static_cast<unsigned>(bit))})
            reserved = reserved || (own != kNoSet && lex_word_set(a.cfg, own) &&
                                    !refuses(own));
        });
        SymbolId best = kInvalidSymbol;
        each_bit(open.w.data(), open.w.size(), [&](std::size_t bit) {
          const auto t = static_cast<unsigned>(bit);
          if (t >= a.cfg.nterms || (reserved && closed.test(t))) return;
          const auto own = a.dfa_acc_bound(s, t);
          if (own != kNoSet && refuses(own)) return;
          if (best == kInvalidSymbol || a.dfa_before(s, t, best))
            best = static_cast<SymbolId>(t);
        });
        return best;
      };
      const auto add = [&](unsigned char byte, SymbolId term,
                           std::uint8_t scalar) {
        if (term == kInvalidSymbol && scalar != 3) return;
        auto i = first;
        while (i < rows.lexElseRows.size() &&
               (rows.lexElseRows[i].term != term ||
                rows.lexElseRows[i].scalar != scalar))
          ++i;
        if (i == rows.lexElseRows.size())
          rows.lexElseRows.push_back({{}, term, scalar});
        rows.lexElseRows[i].bytes.add(byte);
      };
      // A lead byte where an acceptance's own class of scalars decides: what
      // stands there is elected as the text is read, wherever anything may.
      ByteSet classed;
      const auto& accS = a.dfa_acc_set(s);
      each_bit(accS.w.data(), accS.w.size(), [&](std::size_t bit) {
        const auto t = static_cast<unsigned>(bit);
        if (t >= a.cfg.nterms) return;
        for (const auto own : {a.dfa_acc_bound(s, t), a.dfa_acc_stand(s, t)})
          if (own != kNoSet && lex_set_scalars(a.cfg, own) != 0)
            classed |= lex_decode_bytes(a.cfg, own);
      });
      const auto stands = [&](unsigned char byte) {
        bool any = false;
        each_bit(accS.w.data(), accS.w.size(), [&](std::size_t bit) {
          const auto t = static_cast<unsigned>(bit);
          if (any || t >= a.cfg.nterms || t + 1u == a.dfaAccept[s]) return;
          const auto own = a.dfa_acc_bound(s, t);
          any = own == kNoSet || !a.cfg.sets[own].has(byte);
        });
        return any;
      };
      for (unsigned b = 0; b < 256; ++b) {
        const auto byte = static_cast<unsigned char>(b);
        if (classed.has(byte)) {
          if ((a.cfg.sets[bound].has(byte) ||
               lex_decode_bytes(a.cfg, bound).has(byte)) &&
              stands(byte))
            add(byte, 0, 3);
          continue;
        }
        // Refused at a lead byte it decodes at, the scalar was a word.
        if (!a.cfg.sets[bound].has(byte)) {
          if (lex_decode_bytes(a.cfg, bound).has(byte))
            add(byte, elect(byte, true), 0);
          continue;
        }
        // Refused outright at a lead byte: where the answer turns on the
        // scalar, the row asks it.
        const SymbolId word = elect(byte, true);
        const SymbolId other = leads.has(byte) ? elect(byte, false) : word;
        if (word == other) {
          add(byte, word, 0);
        } else {
          add(byte, word, 1);
          add(byte, other, 2);
        }
      }
    }
  }
  rows.lexElseBase.push_back(static_cast<std::uint32_t>(rows.lexElseRows.size()));
  LexSuccessors successors(a.dfaNext.data(), a.ndfa, a.ncls);
  rows.typoSuccBase = std::move(successors.base);
  rows.typoSucc = std::move(successors.states);
}

}  // namespace cursiv
