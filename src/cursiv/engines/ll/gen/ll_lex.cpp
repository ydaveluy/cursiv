// The lexer of one program: a token graph per terminal, the byte classes its
// edges are written in, and one automaton per point the parse reads at. What
// a point may read is the whole of a mode -- a position expecting one
// terminal lexes that one, a choice lexes what its arms admit.
#include "cursiv/engines/ll/gen/ll_lex.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlExitReads.hpp"
#include "cursiv/engines/ll/gen/LlLexView.hpp"
#include "cursiv/engines/ll/gen/LlStanding.hpp"
#include "cursiv/engines/shared/gen/LexAlphabet.hpp"
#include "cursiv/engines/shared/gen/LexBacktrack.hpp"
#include "cursiv/engines/shared/gen/LexDfaBuild.hpp"
#include "cursiv/engines/shared/gen/LexModes.hpp"
#include "cursiv/engines/shared/gen/LexOvertakes.hpp"
#include "cursiv/engines/shared/gen/LexOwedRows.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LexSuccessors.hpp"
#include "cursiv/engines/shared/gen/PredicateModes.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/gen/lex_reserve.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {
namespace detail {

// What each section's grammar reads, by the section's own configuration: the
// terminals of its positions and operators.
std::vector<DynSet> ll_section_reads(const LlLowCfg& g) {
  const auto& home = g.cfgHome;
  std::vector<DynSet> own(home.size());
  for (std::uint32_t id = 0; id < g.nnodes; ++id)
    if (g.nodes[id].kind == LlShape::term)
      own[home[g.nodeBuild[id].cfg]].set(g.nodes[id].sym);
  return own;
}

// Where a token is read, and what may stand there. A position expecting one
// terminal lexes that one; a choice lexes what its arms admit. Trivia rides
// with both: what the configuration the point reads under skips, so a blank a
// region does not skip stops the walk instead of being eaten.
LexModes ll_lex_modes_of(LlBuiltProgram& b) {
  auto& g = b.cfg;
  LexModes modes(g);
  const DynSet& trivia = modes.trivia;
  const auto& skips = modes.skips;
  const auto skip = [&](const std::vector<std::uint8_t>& at, std::size_t i) {
    return skips[i < at.size() && at[i] < skips.size() ? at[i] : 0] |
           modes.joins;
  };
  // An implied token answers a refused token in hand: where it may stand, the
  // text its condition reads must lex, or a shorter token is taken and kept.
  // The token in hand is the one its normal continuation elects; what follows
  // an insertion reads the same text again, under its own mode.
  const std::vector<DynSet> firsts = condition_firsts(g);
  std::map<unsigned, DynSet> implied;
  for (std::uint32_t id = 0; id < g.nnodes; ++id)
    if (const LlNode& n = g.nodes[id]; n.kind == LlShape::implied)
      implied[n.sym] |= firsts[n.min];
  const auto intern = [&](DynSet want, DynSet beyond = {}) {
    for (const auto& [u, heads] : implied)
      if (want.test(u)) beyond |= heads;
    return modes.intern(want, 0, longer_firsts(g, want, beyond));
  };

  const LlStanding standing(b);
  const auto standing_firsts = [&](const DynSet& conditions) {
    DynSet out;
    for (unsigned k = 0; k < g.ngapConds; ++k)
      if (conditions.test(k)) out |= firsts[k];
    return out;
  };
  for (std::uint32_t id = 0; id < g.nnodes; ++id) {
    LlNode& n = g.nodes[id];
    if (n.kind == LlShape::condition || n.kind == LlShape::implied) {
      DynSet want = skip(b.gapCfg, id);
      if (n.kind == LlShape::implied)
        want.set(n.sym);
      else
        want |= b.after[id];
      n.mode = intern(want, n.kind == LlShape::condition
                                ? standing_firsts(standing.point[id])
                                : DynSet{});
    } else if (n.kind == LlShape::endMark || n.kind == LlShape::term) {
      DynSet want = skip(b.gapCfg, id);
      want.set(n.sym);
      n.mode = intern(want);
    } else if (n.kind == LlShape::call && g.cfgTriviaAt.size() > 2 &&
               (b.nullable[id] || !b.first[id].test(kEofSymbol) ||
                b.first[id].more_than_one())) {
      n.mode = intern(b.lookahead(static_cast<std::uint16_t>(id), b.after[id]) |
                      skip(b.gapCfg, id));
    }
  }
  b.headMode.assign(g.ndecisions, 0);
  b.decStands.assign(g.ndecisions, 0);
  for (std::uint16_t d = 0; d < g.ndecisions; ++d) {
    LlDecision& dec = g.decisions[d];
    DynSet want;
    for (std::uint16_t a = 0; a < g.decisionBuild[d].nalts; ++a)
      want |= b.laSet[dec.set + a];
    const DynSet& stands = standing.point[dec.node];
    b.decStands[d] = stands.any() ? 1 : 0;
    const DynSet beyond = standing_firsts(stands);
    dec.mode = intern(want | skip(b.gapCfg, dec.node), beyond);
    if (g.nodes[dec.node].kind == LlShape::rep)
      b.headMode[d] = intern(want | skip(b.headCfg, dec.node), beyond);
  }
  for (std::uint16_t c = 0; c < g.nclimbs; ++c) {
    LlClimb& cl = g.climbs[c];
    if (cl.selectNode != kNoRow) {
      cl.mode = g.decisions[g.nodes[cl.selectNode].decision].mode;
      continue;
    }
    DynSet want;
    // An outer operator can extend one of this climb's spellings ("->"
    // versus "-"). Keep the whole follow alphabet in the lexical election
    // so the inner climb leaves that longer token to its caller.
    for (std::uint16_t r = 1; r < g.nrules; ++r)
      if (g.rules[r].climb == c) want |= b.ruleFollow[r];
    want |= skip(b.climbCfg, c);
    cl.mode = intern(want);
  }
  b.errMode = modes.error_mode(g);
  DynSet tail = skips[0];
  tail.set(kEofSymbol);
  b.tailMode = intern(tail);
  const auto& home = g.cfgHome;
  b.sectionErr.clear();
  b.sectionEnd.clear();
  if (g.sections()) {
    b.sectionErr = modes.section_modes(g, ll_section_reads(g), b.errMode);
    b.sectionEnd.assign(home.size(), 0);
    for (std::size_t h = 1; h < home.size(); ++h)
      if (home[h] == h) b.sectionEnd[h] = g.cfgEnd[h];
  }
  // For a choice in a region, what its configuration skips and every token it
  // does not read in another role: a lookahead crosses no other trivia -- in a
  // section, no token its grammar does not read.
  b.lookMode.clear();
  if (skips.size() > 1) {
    std::vector byCfg(skips.size(), kNoRow);
    b.lookMode.assign(g.ndecisions, b.errMode);
    for (std::uint16_t d = 0; d < g.ndecisions; ++d) {
      const std::uint8_t k = b.gapCfg[g.decisions[d].node];
      if (byCfg[k] == kNoRow && !b.sectionErr.empty() && home[k] != 0) {
        DynSet all = skips[k];
        for (std::uint16_t t = 1; t < g.nterms; ++t)
          if (!trivia.test(t) && modes.set[b.sectionErr[home[k]]].test(t))
            all.set(t);
        byCfg[k] = intern(all);
      }
      if (byCfg[k] == kNoRow) {
        DynSet all = skips[k];
        DynSet recast;
        for (std::uint16_t t = 1; t < g.nterms; ++t)
          if (skips[k].test(t))
            recast.set(t < g.cloneBase.size() && g.cloneBase[t] != 0
                           ? g.cloneBase[t]
                           : t);
        for (std::size_t t = 1; t < g.cloneBase.size(); ++t)
          if (g.cloneBase[t] != 0 && recast.test(g.cloneBase[t]))
            recast.set(static_cast<std::uint16_t>(t));
        for (std::uint16_t t = 1; t < g.nterms; ++t)
          if (!trivia.test(t) && !recast.test(t) &&
              !g.termBuild[t].predicateOnly)
            all.set(t);
        byCfg[k] = intern(all);
      }
      b.lookMode[d] = byCfg[k];
    }
    if (std::ranges::all_of(b.lookMode,
                            [&](std::uint16_t m) { return m == b.errMode; }))
      b.lookMode.clear();
  }
  predicate_modes(g, modes);
  b.afterGap.clear();
  if (std::ranges::any_of(b.afterCfg,
                          [&](std::uint16_t k) { return k < skips.size(); })) {
    b.afterGap.assign(g.nnodes, kNoRow);
    for (std::uint32_t id = 0; id < b.afterCfg.size(); ++id)
      if (b.afterCfg[id] < skips.size())
        b.afterGap[id] = intern(skips[b.afterCfg[id]]);
  }
  b.modeTrivia.clear();
  b.modeJoinedTrivia.clear();
  // By index: interning a trivia set may append a mode while it is read.
  for (std::size_t m = 0; m < modes.set.size(); ++m) {
    DynSet blank, joined;
    for (std::uint16_t t = 1; t < g.nterms; ++t) {
      if (!modes.set[m].test(t)) continue;
      if (g.terms[t].trivia) blank.set(t);
      if (g.terms[t].trivia || g.terms[t].joins) joined.set(t);
    }
    b.modeTrivia.push_back(intern(blank));
    b.modeJoinedTrivia.push_back(intern(joined));
  }
  b.nmodes = static_cast<std::uint16_t>(modes.set.size());
  b.modeLonger = modes.longerMode;
  b.modeHeads = modes.heads(g);
  return modes;
}

// Whether a relay's rest begins where the walk launched it: an assertion
// alone, rather than a search for an echo, a closer or a recognized run.
static bool in_step(const LexRelayGen& r) {
  return r.predicate != kNoLexNode && r.recognition == kNoLexNode;
}

void ll_lex_dfa(LlBuiltProgram& built, LexAlphabet& alpha,
                const LexModes& modes, std::vector<DynSet>& modeTie) {
  const auto& g = built.cfg;
  const std::vector<DynSet>& modeSet = modes.set;
  const auto push_relay = [&built](const LexRelayGen& row) {
    built.relays.push_back(row);
    return built.relays.size() - 1;
  };

  // Each accept list once in the pool, keyed by its packed entries.
  std::map<std::vector<std::uint64_t>, std::uint32_t> listAt;
  const auto write = [&](std::uint16_t self, const LexDfaReading& r) {
    built.dfaAccSet[self] = r.acc;
    built.dfaAccept[self] =
        r.best < 0 ? 0 : static_cast<std::uint16_t>(r.best + 1);
    // The boundary is the match state's own, put there by a literal's guard
    // or a refusal inside the body -- never the terminal's, which would close
    // every alternative on the class of one.
    built.dfaBound[self] = r.best_bound();
    std::vector<LlLexAccept> own;
    for (const auto& a : r.accepts) own.push_back({a.term, a.bound, a.tier});
    for (const auto& a : r.stands)
      own.push_back({a.term, a.bound, kTierKeyword});
    // A list the pool holds already is read where it stands.
    std::vector<std::uint64_t> key;
    for (const LlLexAccept& a : own)
      key.push_back(std::uint64_t{a.term} << 24 | std::uint64_t{a.bound} << 8 |
                    a.tier);
    auto& pool = built.lexChoices;
    const auto [at, fresh] = listAt.try_emplace(
        std::move(key), static_cast<std::uint32_t>(pool.size()));
    if (fresh) pool.insert(pool.end(), own.begin(), own.end());
    built.lexChoiceAt[std::size_t{self} * 2] = at->second;
    built.lexChoiceAt[std::size_t{self} * 2 + 1] =
        static_cast<std::uint32_t>(own.size());
    built.lexChecks[self] =
        std::ranges::any_of(
            r.accepts,
            [&](const auto& a) { return a.bound != built.dfaBound[self]; }) ||
        !r.stands.empty();
  };
  lex_build_dfa(built, alpha, alpha.cut, modeSet, push_relay, write);
  LexSuccessors successors(built.dfaNext.data(), built.ndfa, built.ncls);
  built.typoSuccBase = std::move(successors.base);
  built.typoSucc = std::move(successors.states);
  {
    LexOwedRows owed = lex_owed_rows(LexOwedView{
        LlLexView{built}, built.ndfa, built.typoSuccBase, built.typoSucc});
    built.lexBodies = std::move(owed.bodies);
    built.lexOwed = std::move(owed.owed);
    built.lexOpeners = std::move(owed.openers);
  }
  // A state that reserves nothing still has its two sets: only the reserving
  // ones are written, and every reader below walks all `ndfa`.
  built.dfaResSet.row(0, built.ndfa);
  built.dfaRelayResSet.row(0, built.ndfa);

  built.wordBounds = lex_word_bounds(g);
  const LexReserved reserved = lex_reserve(
      g, LlLexView{built}, built.ndfa, built.relays,
      [&](std::size_t s) -> const DynSet& { return built.dfaAccSet.data()[s]; },
      [&](std::size_t s, auto&& each) {
        for (const LlLexAccept& a : built.choices(s))
          each(a.term, a.bound, unsigned{a.tier});
      },
      [&](std::size_t s, unsigned u) {
        built.dfaResSet[s].set(u);
        // A state that reserves is elected from its choices.
        built.lexChecks[s] = 1;
      },
      [&](std::size_t s, unsigned u) { built.dfaRelayResSet[s].set(u); });
  const std::vector<DynSet>& wordAt = reserved.keywords;

  // An acceptance bounded by every byte stands only where no byte follows.
  std::vector<DynSet> ends(built.ndfa);
  for (std::uint16_t s = 0; s < built.ndfa; ++s) {
    DynSet other;
    for (const LlLexAccept& a : built.choices(s))
      (lex_bound_ends(g, a.bound) ? ends[s] : other).set(a.term);
    ends[s].andnot(other);
  }

  std::vector<DynSet> reads(built.ndfa);
  // Where a word byte follows, a keyword among a terminal's branches does not
  // stand and nothing is reserved.
  std::vector<DynSet> unreserved(built.ndfa);
  for (std::uint16_t s = 0; s < built.ndfa; ++s) {
    reads[s] = built.dfaAccSet[s];
    const bool stands = reserved.standing[s];
    if (stands) {
      unreserved[s] = built.dfaAccSet[s];
      unreserved[s].andnot(wordAt[s]);
    }
    const bool wordAsRaw =
        std::ranges::any_of(built.choices(s), [&](const LlLexAccept& a) {
          return wordAt[s].test(a.term) &&
                 std::ranges::find(built.wordBounds, a.bound) ==
                     built.wordBounds.end();
        });
    // A raw arm survives when the word boundary disables the reservation.
    if (!wordAsRaw) reads[s].andnot(built.dfaResSet[s]);
    for (auto r = built.dfaRelay[s]; r != kNoRelay; r = built.relays[r].next) {
      if (built.relays[r].gate) continue;
      reads[s].set(built.relays[r].term);
      if (stands) unreserved[s].set(built.relays[r].term);
    }
  }
  // A mode two terminals can match from one position: the parse then has a
  // lexical choice as well as a syntactic one, and the prediction is what
  // settles it. Where only a token that ends the text can, the choice is the
  // prediction's there alone. Trivia is never a rival: the lexer elects a
  // visible terminal over it, and a prediction reads the visible ones alone
  // again. A statement's newline and a blank of the same spelling are one
  // token.
  modeTie.assign(built.nmodes, DynSet{});
  // The terminals read at one length, `endOnly` those of them that stand only
  // where the text ends: true once the mode has a choice everywhere.
  const auto tie = [&](std::uint16_t m, const DynSet& row,
                       const DynSet& endOnly) {
    DynSet mine = row & modeSet[m];
    mine.andnot(modes.trivia);
    if (!mine.more_than_one()) return false;
    DynSet strict = mine;
    strict.andnot(endOnly);
    if (strict.more_than_one()) {
      built.modeMulti[m] = 1;
      return true;
    }
    built.modeMulti[m] = 2;
    modeTie[m] |= mine;
    return false;
  };
  // Two cursors in step, at one length: a terminal stands only where the text
  // ends when neither reads it anywhere else.
  const auto tie_in_step = [&](std::uint16_t m, std::uint16_t a,
                               std::uint16_t b) {
    for (const DynSet* ra : {&reads[a], &unreserved[a]})
      for (const DynSet* rb : {&reads[b], &unreserved[b]}) {
        DynSet anywhere = *ra;
        anywhere.andnot(ends[a]);
        DynSet fromB = *rb;
        fromB.andnot(ends[b]);
        anywhere |= fromB;
        DynSet endOnly = *ra | *rb;
        endOnly.andnot(anywhere);
        if (tie(m, *ra | *rb, endOnly)) return true;
      }
    return false;
  };
  // Only the states a mode's walk reaches: another mode's state may accept
  // two of its terminals at a spelling this mode's own reads otherwise.
  std::vector<std::uint16_t> reach;
  std::vector<std::uint8_t> seen;
  // A relay that asserts where the walk stands reads the rest of its token
  // in step with the walk it leaves, from one position: what the two accept
  // after the same bytes ties as a state's own terminals do. The pairs are
  // the walk with each rest it launches, and two rests of one walk.
  std::vector<std::pair<std::uint16_t, std::uint16_t>> pairs;
  std::unordered_set<std::uint32_t> paired;
  for (std::uint16_t m = 0; m < built.nmodes; ++m) {
    built.modeMulti[m] = 0;
    seen.assign(built.ndfa, 0);
    reach.assign(1, built.modeStart[m]);
    seen[built.modeStart[m]] = 1;
    pairs.clear();
    paired.clear();
    const auto enter = [&](std::uint16_t to) {
      if (to == kDeadState || seen[to] != 0) return;
      seen[to] = 1;
      reach.push_back(to);
    };
    const auto pair = [&](std::uint16_t a, std::uint16_t b) {
      if (a == b || a == kDeadState || b == kDeadState) return;
      if (a > b) std::swap(a, b);
      if (paired.insert(std::uint32_t{a} << 16 | b).second)
        pairs.emplace_back(a, b);
    };
    // The rests `s` launches, each paired with `with` and every rest after it.
    const auto launch = [&](std::uint16_t s, std::uint16_t with) {
      for (auto r = built.dfaRelay[s]; r != kNoRelay;
           r = built.relays[r].next) {
        if (built.relays[r].gate || !in_step(built.relays[r])) continue;
        pair(with, built.relays[r].resume);
        for (auto q = built.relays[r].next; q != kNoRelay;
             q = built.relays[q].next)
          if (!built.relays[q].gate && in_step(built.relays[q]))
            pair(built.relays[r].resume, built.relays[q].resume);
      }
    };
    for (std::size_t i = 0; i < reach.size() && built.modeMulti[m] != 1; ++i) {
      const std::uint16_t s = reach[i];
      for (unsigned k = 0; k < alpha.ncls; ++k)
        enter(built.dfaNext[std::size_t{s} * alpha.ncls + k]);
      for (auto r = built.dfaRelay[s]; r != kNoRelay; r = built.relays[r].next)
        enter(built.relays[r].resume);
      for (const DynSet* row : {&reads[s], &unreserved[s]})
        if (tie(m, *row, ends[s])) break;
      launch(s, s);
    }
    for (std::size_t i = 0; i < pairs.size() && built.modeMulti[m] != 1; ++i) {
      const auto [a, b] = pairs[i];
      if (tie_in_step(m, a, b)) break;
      launch(a, b);
      launch(b, a);
      for (unsigned k = 0; k < alpha.ncls; ++k)
        pair(built.dfaNext[std::size_t{a} * alpha.ncls + k],
             built.dfaNext[std::size_t{b} * alpha.ncls + k]);
    }
    if (built.modeMulti[m] != 2) modeTie[m] = DynSet{};
  }
}

// One alphabet covers another when it admits everything the other does. A
// token the wider one matched then answers the narrower, provided the
// narrower admits the terminal it matched -- no subset can match longer.
void ll_mode_cover(LlBuiltProgram& b, const LexModes& modes) {
  const std::uint32_t words = (b.cfg.nterms + 63u) / 64u;
  b.modeTerms.assign(std::size_t{b.nmodes} * words, 0);
  for (std::uint16_t m = 0; m < b.nmodes; ++m)
    for (std::uint32_t k = 0; k < words; ++k)
      b.modeTerms[std::size_t{m} * words + k] =
          modes.set[m].word(k) | modes.longer[m].word(k);
  b.subWords = (b.nmodes + 63u) / 64u;
  b.modeSub.assign(std::size_t{b.nmodes} * b.subWords, 0);
  modes.cover(false, [&](std::size_t i, std::size_t j) {
    b.modeSub[i * b.subWords + (j >> 6)] |= std::uint64_t{1} << (j & 63);
  });
}

// A choice reads its token under the alphabet of all its arms, and an arm that
// may end the rule admits whatever any caller reads after it. Such a terminal,
// which the rule itself does not read from the choice on, may read over
// another of the alphabet -- `#(` over `#` -- where the caller in hand takes
// none of it: per choice, those terminals, which the descent asks of the
// frames it stands in before it takes one.
void ll_flag_choices(LlBuiltProgram& b, const LexAlphabet& alpha,
                     const LexModes& modes) {
  const auto& g = b.cfg;
  b.decFlag.assign(g.ndecisions, DynSet{});
  const LlExitReads exits(b);
  std::vector<DynSet> followers(g.nterms);
  for (std::uint32_t id = 0; id < g.nnodes; ++id)
    if (g.nodes[id].kind == LlShape::term)
      followers[g.nodes[id].sym] |= b.after[id];
  LexOvertakes overtakes(alpha.nfa, g, alpha.startOf, followers);
  for (std::uint16_t d = 0; d < g.ndecisions; ++d) {
    const LlDecision& dec = g.decisions[d];
    if (!b.liveRules[dec.rule] || exits.ends[dec.state] == 0) continue;
    DynSet caller;
    for (unsigned a = 0; a < g.decisionBuild[d].nalts; ++a)
      caller |= b.laSet[dec.set + a];
    caller.andnot(exits.reads[dec.state]);
    caller.andnot(modes.trivia);
    // What the token is read beside: the tokens of the alphabet, that of each
    // turn of a loop.
    DynSet beside = modes.set[dec.mode];
    if (g.nodes[dec.node].kind == LlShape::rep)
      beside |= modes.set[b.headMode[d]];
    beside.andnot(modes.trivia);
    each_bit(caller.w.data(), caller.w.size(), [&](std::size_t t) {
      if (overtakes.reads_over(static_cast<unsigned>(t), beside))
        b.decFlag[d].set(static_cast<unsigned>(t));
    });
  }
}

void ll_build_lexer(LlBuiltProgram& b) {
  LexAlphabet alpha;
  alpha.terminals(b, [](LlLowCfg& g, std::string_view text) {
    return g.intern_name(text);
  });
  if (b.cfg.lowerFail()) return;
  alpha.section_ends(b.cfg, ll_section_reads);
  const LexModes modes = ll_lex_modes_of(b);
  // For a mode `modeMulti` marks 2, the terminals that meet at the text's end.
  std::vector<DynSet> modeTie;
  ll_lex_dfa(b, alpha, modes, modeTie);
  b.modeBacktrack = lex_backtrack_modes(b);
  ll_mode_cover(b, modes);
  // Different terminals may spell the same bytes. Their token ids alone
  // cannot settle a choice; prediction must retain all those readings.
  b.decEnded.assign(b.cfg.ndecisions, 0);
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    auto& decision = b.cfg.decisions[d];
    const std::uint8_t multi = b.modeMulti[decision.mode];
    if (multi == 0) continue;
    if (multi == 2) {
      bool meets = false;
      for (unsigned i = 0; i < b.cfg.decisionBuild[d].nalts; ++i)
        meets = meets ||
                b.laSet[decision.set + i].intersects(modeTie[decision.mode]);
      if (!meets) continue;
      b.decEnded[d] = 1;
      if (decision.how == LlPredict::single) continue;
    }
    if (decision.how == LlPredict::adaptive) continue;
    decision.how = LlPredict::adaptive;
    ++b.nadaptive;
  }
  ll_flag_choices(b, alpha, modes);
}

}  // namespace detail
}  // namespace cursiv
