#include "cursiv/engine/parser/Pack.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "cursiv/engine/lexer/CharClass.hpp"
#include "cursiv/engine/parser/Closers.hpp"
#include "cursiv/engine/parser/InsertOrder.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine::gen {

namespace {

// The sets of bytes the lexer runs over, each written once.
struct RunSets {
  Packed& out;
  std::map<ByteSet, std::uint32_t> known{};

  std::uint32_t of(const ByteSet& set) {
    const auto [it, fresh] = known.try_emplace(set, known.size());
    if (fresh) {
      for (unsigned b = 0; b < 256; ++b) out.runSets.push_back(set.has(b) ? 1 : 0);
      out.runBytes.push_back(set);
    }
    return it->second;
  }
};

// Each state's run: the bytes that keep an automaton where it is, as one
// more than their set of `runs`.
std::vector<std::uint8_t> runs_of(const auto& dfa, RunSets& runs, auto canSkip) {
  if (dfa.nclasses == 0) return {};
  const std::size_t n = dfa.next.size() / dfa.nclasses;
  std::vector<ByteSet> classBytes(dfa.nclasses);
  for (unsigned b = 0; b < 256; ++b) classBytes[dfa.classOf[b]].add(b);
  std::vector<std::uint8_t> out(n, 0);
  for (std::size_t s = 1; s < n; ++s) {
    if (!canSkip(s)) continue;
    ByteSet self;
    for (unsigned c = 0; c < dfa.nclasses; ++c)
      if (dfa.next[s * dfa.nclasses + c] == s) self |= classBytes[c];
    if (!self.any()) continue;
    if (const auto run = runs.of(self); run < 255)
      out[s] = static_cast<std::uint8_t>(run + 1);
  }
  return out;
}

// Refuses a nonterminal that derives itself with only symbols beside it that
// can read nothing: that cycle reads no input.
void refuse_empty_cycles(const Cfg& cfg, const std::vector<bool>& nullable) {
  const std::size_t nnts = cfg.nonterminals;
  std::vector<std::vector<std::uint32_t>> parents(nnts);
  std::vector<std::uint32_t> waits(nnts, 0);
  for (const Production& q : cfg.productions) {
    const auto solid = std::ranges::count_if(
        q.rhs, [&](Symbol s) { return is_terminal(s) || !nullable[index_of(s)]; });
    for (const Symbol s : q.rhs) {
      if (is_terminal(s) || solid > !nullable[index_of(s)]) continue;
      parents[index_of(s)].push_back(q.lhs);
      ++waits[q.lhs];
    }
  }
  std::vector<std::uint32_t> work;
  for (std::uint32_t nt = 0; nt < nnts; ++nt)
    if (waits[nt] == 0) work.push_back(nt);
  std::size_t done = 0;
  for (; !work.empty(); ++done) {
    const std::uint32_t nt = work.back();
    work.pop_back();
    for (const std::uint32_t parent : parents[nt])
      if (--waits[parent] == 0) work.push_back(parent);
  }
  if (done != nnts) throw Refused("a cycle of rules reads no input");
}

// What the records a production closes over can be: the entries of a lone
// record it takes over, and whether it can close over nothing or over several.
std::vector<std::set<std::uint32_t>> record_shapes(Cfg& cfg, Packed& out) {
  const std::size_t nnts = cfg.nonterminals;
  const std::size_t nprods = cfg.productions.size();
  std::vector<std::vector<std::uint32_t>> users(nnts);
  std::vector<std::uint32_t> loud(nprods), writers(nprods);
  for (std::uint32_t p = 0; p < nprods; ++p) {
    const auto& rhs = cfg.productions[p].rhs;
    loud[p] = static_cast<std::uint32_t>(rhs.size());
    for (const Symbol s : rhs) {
      if (is_terminal(s)) ++writers[p];
      else users[index_of(s)].push_back(p);
    }
  }

  // Counts saturate at two. A production can yield nothing once every child
  // can be silent; an entry always yields one record to its caller.
  struct Change {
    std::uint32_t nt;
    std::uint8_t more;
    bool silent;
  };
  std::vector<Change> changes;
  std::vector<bool> silent(nnts, false);
  std::vector<std::uint8_t> most(nnts);
  const auto update = [&](std::uint32_t p) {
    const Production& q = cfg.productions[p];
    const auto count = q.entry == kNone ? std::min(writers[p], 2u) : 1u;
    const bool hush = q.entry == kNone && loud[p] == 0 && !silent[q.lhs];
    const auto more = static_cast<std::uint8_t>(count > most[q.lhs] ? count - most[q.lhs] : 0);
    if (!hush && more == 0) return;
    changes.push_back({q.lhs, more, hush});
    most[q.lhs] += more;
    if (hush) silent[q.lhs] = true;
  };
  for (std::uint32_t p = 0; p < nprods; ++p) update(p);
  while (!changes.empty()) {
    const Change change = changes.back();
    changes.pop_back();
    for (const auto p : users[change.nt]) {
      loud[p] -= change.silent;
      writers[p] += change.more;
      update(p);
    }
  }
  // A child is lone where its siblings can all write nothing: the only loud
  // one, or any child where there is none.
  const auto may_be_lone = [&](std::uint32_t p, Symbol s) {
    return loud[p] == 0 ||
           (loud[p] == 1 && (is_terminal(s) || !silent[index_of(s)]));
  };
  // A nonterminal is worked out once those its record can take over are.
  std::vector<std::uint32_t> waits(nnts, 0);
  for (std::uint32_t nt = 0; nt < nnts; ++nt)
    for (const std::uint32_t p : users[nt])
      if (may_be_lone(p, nonterminal(nt))) ++waits[cfg.productions[p].lhs];
  std::vector<std::uint32_t> work;
  for (std::uint32_t nt = 0; nt < nnts; ++nt)
    if (waits[nt] == 0) work.push_back(nt);
  // Per nonterminal, the entries its record carries where it is one record.
  std::vector<std::set<std::uint32_t>> records(nnts), childrenOf(nprods);
  out.shapes.resize(nprods);
  while (!work.empty()) {
    const std::uint32_t nt = work.back();
    work.pop_back();
    for (const std::uint32_t p : cfg.productionsOf[nt]) {
      const Production& prod = cfg.productions[p];
      std::set<std::uint32_t>& children = childrenOf[p];
      for (std::size_t at = 0; at < prod.rhs.size(); ++at) {
        const Symbol s = prod.rhs[at];
        if (!may_be_lone(p, s)) continue;
        // A record the production gives an entry to carries the two.
        const std::uint32_t take = prod.takes.empty() ? kNone : prod.takes[at];
        if (is_terminal(s)) {
          children.insert(out.terminalEntry[s]);
          continue;
        }
        for (const std::uint32_t child : records[index_of(s)])
          children.insert(take == kNone ? child : cfg.fuse(child, take));
      }
      out.shapes[p] = (loud[p] == 0 ? kRecordEmpty : 0u) |
                      (children.empty() ? 0u : kRecordLone) |
                      (writers[p] >= 2 ? kRecordSeveral : 0u);
      if (prod.entry == kNone) {
        records[nt].insert(children.begin(), children.end());
        continue;
      }
      // Its own entry stands alone only over nothing or over several.
      if ((out.shapes[p] & (kRecordEmpty | kRecordSeveral)) != 0) records[nt].insert(prod.entry);
      for (const std::uint32_t child : children)
        records[nt].insert(cfg.fuse(child, prod.entry));
    }
    for (const std::uint32_t p : users[nt])
      if (may_be_lone(p, nonterminal(nt)) && --waits[cfg.productions[p].lhs] == 0)
        work.push_back(cfg.productions[p].lhs);
  }
  // A copy of an infix node writes what its node does.
  for (std::uint32_t p = 0; p < nprods; ++p)
    if (const std::uint32_t node = cfg.productions[p].copyOf; node != kNone) {
      out.shapes[p] = out.shapes[node];
      childrenOf[p] = childrenOf[node];
    }
  // What the one record of a symbol becomes under the entry its production
  // gives it.
  for (const Production& prod : cfg.productions)
    for (std::size_t at = 0; at < prod.takes.size(); ++at) {
      const std::uint32_t take = prod.takes[at];
      if (take == kNone) continue;
      for (const std::uint32_t child : records[index_of(prod.rhs[at])])
        out.fused.try_emplace({take, child}, cfg.fuse(child, take));
    }
  return childrenOf;
}

// The terminals: how each stands among the tokens, its rank and the entry
// of a bare leaf.
void terminal_facts(const Cfg& cfg, Packed& out) {
  const std::size_t nterms = cfg.terminals.size();
  for (std::size_t t = 0; t < nterms; ++t) {
    const Terminal& term = cfg.terminals[t];
    out.terminalTrivia.push_back(term.trivia);
    out.terminalRank.push_back(lexical_rank(term.trivia, term.literal()));
    out.terminalEntry.push_back(term.entry);
  }
  // The terminal past the last stands for text no terminal reads: nothing
  // that ends where it does replaces it.
  out.terminalTrivia.push_back(Trivia::none);
  out.terminalRank.push_back(0);
}

// The grammar's word class.
CharClass word_of(const GrammarImage& image) {
  return CharClass::of(image.bound.props, image.bound.complement, image.bound.scalars);
}

// Per byte, how a word of class `word` reads it: 0 where no word goes on with
// it, 1 where one does, 2 where the scalar it leads is to be read.
std::array<std::uint8_t, 256> word_bytes(const CharClass& word) {
  bool wide = word.invalid;
  for (const unicode::Range r : word.ranges) wide = wide || r.hi >= 0x80;
  std::array<std::uint8_t, 256> bytes{};
  for (unsigned b = 0; b < 256; ++b)
    bytes[b] = b < 0x80                  ? (word.has(b) ? 1 : 0)
               : b >= 0xC2 && b <= 0xF4 ? (wide ? 2 : 0)
                                        : (word.invalid ? 1 : 0);
  return bytes;
}

// The word class, for a keyword's guard: its ASCII bytes, and its scalars
// past ASCII, which the guard reads from their lead byte.
void word_class(const GrammarImage& image, Packed& out) {
  const CharClass word = word_of(image);
  for (const unicode::Range r : word.ranges)
    if (r.hi >= 0x80) out.wordRanges.push_back({std::max<char32_t>(r.lo, 0x80), r.hi});
  out.wordInvalid = word.invalid;
  out.wordBytes = word_bytes(word);
}

// Per production, how it stands among those that read one text. A left
// recursion reads as its fold: its bases, then its turns, each numbered as a
// walk from the rule called from outside the recursion reaches them.
std::vector<std::uint32_t> election_order(const Cfg& cfg, const std::vector<bool>& turns) {
  const std::size_t nnts = cfg.nonterminals;
  const std::size_t nprods = cfg.productions.size();
  // Each nonterminal's productions, the lowest rank first.
  std::vector<std::vector<std::uint32_t>> of = cfg.productionsOf;
  for (std::vector<std::uint32_t>& list : of)
    std::ranges::stable_sort(list, {}, [&](std::uint32_t p) { return cfg.productions[p].rank; });
  std::vector<bool> loop(nnts, false), outside(nnts, false), seen(nnts, false);
  for (std::uint32_t p = 0; p < nprods; ++p) {
    const Production& q = cfg.productions[p];
    if (turns[p]) loop[q.lhs] = true;
    for (std::size_t i = turns[p] ? 1 : 0; i < q.rhs.size(); ++i)
      if (!is_terminal(q.rhs[i])) outside[index_of(q.rhs[i])] = true;
  }
  std::vector<std::uint32_t> order(nprods, 0);
  for (std::uint32_t p = 0; p < nprods; ++p)
    order[p] = cfg.productions[p].rank << 2 | (cfg.productions[p].prefixTurn ? 1u : 0u);
  std::uint32_t bases = 0, turned = 0;
  std::vector<std::pair<std::uint32_t, std::size_t>> walk;
  const auto visit = [&](std::uint32_t root) {
    seen[root] = true;
    walk.assign(1, {root, 0});
    while (!walk.empty()) {
      const std::uint32_t nt = walk.back().first;
      const std::size_t i = walk.back().second++;
      if (i == of[nt].size()) {
        walk.pop_back();
        continue;
      }
      const std::uint32_t p = of[nt][i];
      if (!cfg.productions[p].prefixTurn)
        order[p] = (turns[p] ? turned++ : bases++) << 2 | 2u | (turns[p] ? 1u : 0u);
      // A turn's left operand is read before the rule's next production.
      if (const std::uint32_t lead = turns[p] ? index_of(cfg.productions[p].rhs[0]) : nt;
          !seen[lead]) {
        seen[lead] = true;
        walk.emplace_back(lead, 0);
      }
    }
  };
  for (const bool called : {true, false})
    for (std::uint32_t nt = 0; nt < nnts; ++nt)
      if (loop[nt] && !seen[nt] && outside[nt] == called) visit(nt);
  // A copy of an infix node stands where its node does.
  for (std::uint32_t p = 0; p < nprods; ++p)
    if (const std::uint32_t node = cfg.productions[p].copyOf; node != kNone) order[p] = order[node];
  return order;
}

// A lexer's scopes contribute their trivia. A shared shift needs one policy.
Bits trivia_of(const Cfg& cfg, std::uint64_t scopes, std::uint16_t shift = kNoToken) {
  Bits passed(cfg.terminals.size());
  const Region* first = nullptr;
  while (scopes) {
    const Region& region = cfg.regions[std::countr_zero(scopes)];
    scopes &= scopes - 1;
    if (shift != kNoToken && first &&
        (region.ignored != first->ignored || region.hidden != first->hidden))
      throw Refused("the trivia before " + cfg.terminals[shift].name +
                    " could follow either setting");
    first = &region;
    for (const auto t : region.ignored) passed.set(t);
    for (const auto t : region.hidden) passed.set(t);
  }
  return passed;
}

// A text read as trivia of a scope there and as a token of another scope
// goes either way where the first scope governs a token of the state other
// than the text's own: refused. Elsewhere a token of the text wins, as over
// any trivia.
void refuse_either_way(const Cfg& cfg, const std::vector<std::vector<std::uint16_t>>& shared,
                      const LrState& from, const PackedState& state) {
  const auto root = [&](std::uint16_t t) {
    return cfg.terminals[t].twin != 0 ? cfg.terminals[t].twin : t;
  };
  struct Use {
    std::uint16_t first;
    std::uint64_t governed = 0, outside = 0;
  };
  std::map<std::uint16_t, Use> uses;
  std::vector<std::pair<std::uint16_t, std::uint64_t>> passed;
  for (std::size_t r = 0; r < shared.size(); ++r) {
    const auto scope = std::uint64_t{1} << r;
    if ((from.scopes & scope) == 0) continue;
    for (const auto t : shared[r]) {
      uses.try_emplace(root(t), Use{t});
      passed.emplace_back(t, scope);
    }
  }
  if (passed.empty()) return;
  const auto governs = [&](const auto& list, auto key) {
    const auto at = std::ranges::lower_bound(list, key, {}, [](const auto& x) { return x.first; });
    return at != list.end() && at->first == key ? at->second : from.scopes;
  };
  std::uint64_t governed = 0, several = 0;
  for (const PackedAction& a : state.actions) {
    const auto scopes = a.kind == PackedAction::Kind::shift ? governs(from.governs, a.terminal)
                                                            : governs(from.behind, a.value);
    const auto at = uses.find(root(a.terminal));
    if (at == uses.end()) {
      governed |= scopes;
    } else {
      at->second.governed |= scopes;
      if (a.terminal != at->second.first) at->second.outside |= ~scopes;
    }
  }
  // A scope governs another spelling if it governs several or only a different one.
  for (const auto& [t, use] : uses) {
    several |= governed & use.governed;
    governed |= use.governed;
  }
  for (const auto& [t, scope] : passed) {
    const Use& use = uses.at(root(t));
    if (t != use.first || ((several | (governed & ~use.governed)) & use.outside & scope))
      throw Refused(cfg.terminals[root(t)].name +
                    " is read both inside a trivia scope and outside it, as a token and as "
                    "trivia or kept and dropped: the trivia before a token could follow "
                    "either setting");
  }
}

// Transitions through the states that only reduce a unit production, and
// hold no bar, are taken in one step: the entry such a production closes is
// composed into the transition.
std::vector<bool> reached_states(Cfg& cfg, const LrAutomaton& automaton, Packed& out,
                                 std::vector<Bits>& gaps) {
  const std::size_t nnts = cfg.nonterminals;
  const std::size_t nstates = automaton.states.size();
  std::vector<std::uint32_t> unit(nstates, kNone);
  for (std::size_t s = 0; s < nstates; ++s) {
    const LrState& state = automaton.states[s];
    if (!state.gotos.empty() || !state.shifts.empty() || state.reduces.size() != 1 ||
        state.barred)
      continue;
    const std::uint32_t prod = state.reduces.front().first;
    if (prod != 0 && cfg.productions[prod].rhs.size() == 1 && !cfg.productions[prod].tested() &&
        !cfg.productions[prod].start && !cfg.productions[prod].capture && cfg.productions[prod].takes.empty())
      unit[s] = prod;
  }
  struct Hop {
    std::uint32_t target;
    std::uint32_t entry;
    std::uint32_t hops;
  };
  // A goto followed by several unit reductions shares its target and outer chain.
  struct Suffix {
    std::uint32_t target = kNone, chain = 0;
  };
  std::vector<std::vector<Suffix>> suffixes(nstates);
  std::vector<std::pair<std::uint32_t, Suffix*>> taken;
  std::vector<std::uint32_t> first(cfg.productions.size()), stamped{0};
  std::unordered_map<std::uint64_t, std::uint32_t> alternatives;
  const auto hop = [&](std::uint32_t from, std::uint32_t to, std::uint32_t entry) {
    if (unit[to] == kNone) return Hop{to, entry, 0};
    std::uint32_t chain = 0;
    taken.clear();
    const auto& gotos = automaton.states[from].gotos;
    auto& row = suffixes[from];
    while (unit[to] != kNone) {
      const auto p = unit[to], nt = cfg.productions[p].lhs;
      const auto next = std::ranges::lower_bound(gotos, nt, {}, [](const auto& g) { return g.first; });
      to = next->second;
      Suffix* tail = nullptr;
      if (unit[to] != kNone) {
        if (row.empty()) row.resize(gotos.size());
        tail = &row[static_cast<std::size_t>(next - gotos.begin())];
      }
      taken.emplace_back(p, tail);
      if (tail && tail->target != kNone) {
        to = tail->target;
        chain = tail->chain;
        break;
      }
    }
    for (const auto& [p, tail] : std::views::reverse(taken)) {
      if (tail) *tail = {to, chain};
      auto& id = !first[p] || out.hops[first[p]].outer == chain
                     ? first[p] : alternatives[std::uint64_t{chain} << 32 | p];
      if (!id) {
        id = static_cast<std::uint32_t>(out.hops.size());
        out.hops.push_back({chain, p});
        stamped.push_back(cfg.productions[p].entry == kNone ? stamped[chain] : id);
      }
      chain = id;
    }
    for (auto at = stamped[chain]; at; at = stamped[out.hops[at].outer])
      entry = cfg.fuse(entry, cfg.productions[out.hops[at].production].entry);
    return Hop{to, entry, chain};
  };

  // The states reached and what each does, numbered as the automaton has
  // them. A goto is followed once a reached state reduces a production of
  // its nonterminal: no other is ever taken.
  std::vector<PackedState> states(nstates);
  const bool scoped = cfg.regions.size() > 1;
  std::vector<std::vector<std::uint16_t>> shared;
  // Per state, the trivia a lexer may have passed before the token it acts
  // on: its own, and where a goto reaches it, those of the states that
  // reduce the nonterminal, which hand their token on; and the trivia sets a
  // shift's gap is read with.
  std::vector<Bits> passed;
  std::map<Bits, std::uint16_t> gapOf;
  if (scoped) {
    // Only terminals with twins can be read under different trivia roles.
    Bits roots(cfg.terminals.size());
    for (const Terminal& t : cfg.terminals)
      if (t.twin != 0) roots.set(t.twin);
    if (roots.any()) {
      shared.resize(cfg.regions.size());
      for (std::size_t r = 0; r < shared.size(); ++r)
        for (const auto* list : {&cfg.regions[r].ignored, &cfg.regions[r].hidden})
          for (const auto t : *list)
            if (cfg.terminals[t].twin != 0 || roots.test(t)) shared[r].push_back(t);
    }
    std::vector<std::vector<std::uint32_t>> targets(nnts);
    std::vector<std::uint64_t> scopes(nstates), reducedUnder(nnts);
    std::vector<std::uint32_t> todo;
    for (std::uint32_t s = 0; s < nstates; ++s) {
      const LrState& state = automaton.states[s];
      for (const auto& [nt, to] : state.gotos) targets[nt].push_back(to);
      scopes[s] = state.scopes;
      if (scopes[s] && !state.reduces.empty()) todo.push_back(s);
    }
    while (!todo.empty()) {
      const auto s = todo.back();
      todo.pop_back();
      for (const auto& [prod, la] : automaton.states[s].reduces) {
        const auto nt = cfg.productions[prod].lhs;
        const auto added = scopes[s] & ~reducedUnder[nt];
        if (!added) continue;
        reducedUnder[nt] |= added;
        for (const auto to : targets[nt]) {
          if (!(added & ~scopes[to])) continue;
          scopes[to] |= added;
          if (!automaton.states[to].reduces.empty()) todo.push_back(to);
        }
      }
    }
    for (const auto mask : scopes) passed.push_back(trivia_of(cfg, mask));
  }
  Closers needs = closers(cfg, automaton);
  refuse_empty_cycles(cfg, needs.nullable);
  out.nullable = needs.nullable;
  out.yields = std::move(needs.yields);
  for (std::size_t s = 0; s < nstates; ++s) {
    states[s].close = std::move(needs.rows[s]);
    states[s].kernel = std::move(needs.kernels[s]);
  }
  if (std::ranges::any_of(needs.turns, std::identity{})) {
    // Per item of a state's kernel, the symbols that stand after a dot once
    // it is closed: what is read next over the state. For a turn that goes
    // on from its left operand, each with the production of the turn's
    // second symbol it begins.
    const std::vector<std::vector<std::uint32_t>>& of = cfg.productionsOf;
    const std::size_t nsymbols = cfg.terminals.size() + nnts;
    const auto slot = [&](Symbol symbol) {
      return is_terminal(symbol) ? std::size_t{symbol} : cfg.terminals.size() + index_of(symbol);
    };
    std::vector<Symbol> open;
    std::vector<std::pair<Symbol, std::uint32_t>> direct;
    // Per symbol read over a state: 0 while no item reads it, 1 for items
    // that are no turn, else 2 with the turn and the production it begins.
    struct Read {
      std::uint8_t by = 0;
      std::uint32_t turn = kNone;
      std::uint32_t via = kNone;
    };
    std::vector<Read> reads(nsymbols);
    std::vector<std::size_t> touched;
    std::vector<bool> told(nstates, false);
    for (std::size_t s = 0; s < nstates; ++s) {
      const LrState& state = automaton.states[s];
      for (const auto i : touched) reads[i] = {};
      touched.clear();
      const auto read = [&](Symbol symbol, std::uint32_t turn, std::uint32_t via) {
        Read& r = reads[slot(symbol)];
        if (r.by == 0) {
          touched.push_back(slot(symbol));
          r = {static_cast<std::uint8_t>(turn == kNone ? 1 : 2), turn, via};
        } else {
          if (r.turn == kSomeProduction || (r.turn == turn && r.via == via)) return false;
          r = {2, kSomeProduction, kNone};
        }
        return true;
      };
      // Propagate each first-symbol origin until the symbol is already known or ambiguous.
      const auto close = [&](Symbol from, std::uint32_t turn, std::uint32_t via) {
        open.assign(1, from);
        while (!open.empty()) {
          const Symbol symbol = open.back();
          open.pop_back();
          if (!read(symbol, turn, via) || is_terminal(symbol)) continue;
          for (const auto prod : of[index_of(symbol)])
            if (const auto& rhs = cfg.productions[prod].rhs; !rhs.empty()) open.push_back(rhs.front());
        }
      };
      direct.clear();
      for (const auto& [production, dot] : state.kernel) {
        const Production& p = cfg.productions[production];
        if (dot >= p.rhs.size()) continue;
        if (!needs.turns[production] || dot != 1) {
          close(p.rhs[dot], kNone, kNone);
          continue;
        }
        direct.emplace_back(p.rhs[1], production);
        if (is_terminal(p.rhs[1])) continue;
        for (const std::uint32_t via : of[index_of(p.rhs[1])])
          if (!cfg.productions[via].rhs.empty()) close(cfg.productions[via].rhs[0], production, via);
      }
      for (const auto& [symbol, turn] : direct) read(symbol, turn, kNone);
      const auto lead = [&](std::uint32_t target, Symbol symbol) {
        const Read& r = reads[slot(symbol)];
        PackedState& to = states[target];
        const std::uint32_t turn = r.by == 2 ? r.turn : kNone;
        const std::uint32_t via = r.by == 2 ? r.via : kNone;
        if (!told[target]) {
          to.turn = turn;
          to.via = via;
          told[target] = true;
        } else if (to.turn != turn || to.via != via) {
          to.turn = kSomeProduction;
          to.via = kNone;
        }
      };
      for (const auto& [terminal, target] : state.shifts) lead(target, terminal);
      for (const auto& [nt, target] : state.gotos) lead(target, kNonterminal | nt);
    }
  }
  InsertOrder order(cfg, automaton, needs);
  Bits acted(cfg.terminals.size());
  std::vector<std::uint16_t> inserts, terminals;
  std::vector<std::vector<PackedGoto>> gotos(nnts);
  std::vector<bool> reached(nstates, false);
  std::vector<bool> reduced(cfg.productions.size(), false);
  std::vector<bool> closed(nnts, false);
  {
    // Per nonterminal no production of which is reduced yet, the gotos over
    // it: the state below and the automaton's target.
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> waiting(nnts);
    std::vector<std::uint32_t> work{0};
    reached[0] = true;
    for (const std::uint32_t reads : automaton.conditions) {
      if (!reached[reads]) work.push_back(reads);
      reached[reads] = true;
    }
    const auto visit = [&](std::uint32_t to) {
      if (reached[to]) return;
      reached[to] = true;
      work.push_back(to);
    };
    const auto follow = [&](std::uint32_t nt, std::uint32_t below, std::uint32_t to) {
      const Hop site = hop(below, to, kNone);
      visit(site.target);
      gotos[nt].push_back({below, site.target, site.entry, site.hops});
    };
    while (!work.empty()) {
      const std::uint32_t s = work.back();
      work.pop_back();
      const LrState& from = automaton.states[s];
      PackedState& state = states[s];
      state.named = from.named;
      state.follow = from.follow;
      state.doubted = from.doubted;
      state.scopes = from.scopes;
      for (const auto& [terminal, to] : from.shifts) {
        const Hop site = hop(s, to, out.terminalEntry[terminal]);
        visit(site.target);
        state.actions.push_back(
            {terminal, PackedAction::Kind::shift, site.target, site.entry, site.hops});
        // A token read under scopes that pass less than some lexer does
        // stands after their trivia alone, whichever state's lexer read it
        // before the reductions that led here: the gap is read again with
        // them and the token, which a trivia of theirs may also spell.
        if (!scoped) continue;
        const auto governed = std::ranges::lower_bound(
            from.governs, terminal, {}, [](const auto& g) { return g.first; });
        if (governed == from.governs.end() || governed->first != terminal) continue;
        Bits own = trivia_of(cfg, governed->second, terminal);
        Bits more = passed[s];
        more.remove(own);
        if (!more.any()) continue;
        if (!own.any()) {
          state.actions.back().gap = kNoGap;
          continue;
        }
        own.set(terminal);
        state.actions.back().gap =
            gapOf.try_emplace(std::move(own), static_cast<std::uint16_t>(gapOf.size())).first->second;
      }
      for (const auto& [prod, la] : from.reduces) {
        const PackedAction::Kind kind = prod == 0 || cfg.productions[prod].start
                                            ? PackedAction::Kind::accept
                                            : PackedAction::Kind::reduce;
        la.each([&](std::size_t t) {
          state.actions.push_back({static_cast<std::uint16_t>(t), kind, prod, kNone, 0});
        });
        reduced[prod] = true;
        const std::uint32_t nt = cfg.productions[prod].lhs;
        if (prod == 0 || cfg.productions[prod].start || closed[nt]) continue;
        closed[nt] = true;
        for (const auto& [below, to] : waiting[nt]) follow(nt, below, to);
        waiting[nt] = {};
      }
      if (!shared.empty()) refuse_either_way(cfg, shared, from, state);
      // A terminal with several actions is a cell the text decides: its
      // shift first, then its reductions in the order of their productions.
      std::ranges::stable_sort(state.actions, {}, &PackedAction::terminal);
      // An implied token is left out only where the state has nothing else
      // to do with the token that comes: beside reductions whose tests may
      // refuse it, it stays, and is taken where they do.
      const auto leaves_out = [&](const PackedAction& a) {
        return a.kind == PackedAction::Kind::reduce && cfg.productions[a.value].implied;
      };
      const auto refusable = [&](const PackedAction& a) {
        return leaves_out(a) ||
               (a.kind == PackedAction::Kind::reduce && cfg.productions[a.value].checks());
      };
      auto kept = state.actions.begin();
      for (auto first = kept; first != state.actions.end();) {
        auto next = first + 1;
        while (next != state.actions.end() && next->terminal == first->terminal) ++next;
        const bool stays = std::all_of(first, next, refusable);
        for (; first != next; ++first)
          if (stays || !leaves_out(*first)) {
            if (kept != first) *kept = *first;
            ++kept;
          }
      }
      state.actions.erase(kept, state.actions.end());
      if (const std::uint32_t lone = reduced_alone(cfg, from); lone != kNone) {
        state.reduce = lone;
        state.actions.clear();
      }
      // The terminals in the order the grammar reaches them, kept where it
      // parts from their numbers': those it leaves out follow by number.
      acted.clear();
      terminals.clear();
      for (const PackedAction& action : state.actions)
        if (terminals.empty() || terminals.back() != action.terminal) {
          terminals.push_back(action.terminal);
          acted.set(action.terminal);
        }
      if (terminals.size() > 1) {
        order.of(s, acted, inserts);
        // Their numbers' order is the list read as the first of the
        // terminals; a list kept ends with those it leaves out, by number.
        if (!std::ranges::equal(inserts, std::span(terminals).first(inserts.size()))) {
          for (const std::uint16_t terminal : inserts) acted.reset(terminal);
          acted.each([&](std::size_t terminal) { inserts.push_back(static_cast<std::uint16_t>(terminal)); });
          state.inserts = inserts;
        }
      }
      for (const auto& [nt, to] : from.gotos) {
        if (closed[nt])
          follow(nt, s, to);
        else
          waiting[nt].emplace_back(s, to);
      }
    }
  }

  // The states numbered in their order, and what names a state with them.
  {
    std::vector<std::uint32_t> number(nstates, kNone);
    std::uint32_t count = 0;
    for (std::size_t s = 0; s < nstates; ++s)
      if (reached[s]) number[s] = count++;
    out.states.resize(count);
    for (std::size_t s = 0; s < nstates; ++s) {
      if (!reached[s]) continue;
      PackedState& state = out.states[number[s]] = std::move(states[s]);
      for (PackedAction& action : state.actions)
        if (action.kind == PackedAction::Kind::shift)
          action.value = number[action.value];
    }
    out.gotos.assign(nnts, {});
    for (std::size_t nt = 0; nt < nnts; ++nt) {
      for (const PackedGoto& site : gotos[nt])
        out.gotos[nt].push_back(
            {number[site.from], number[site.target], site.entry, site.hops});
      std::ranges::sort(out.gotos[nt], {}, &PackedGoto::from);
    }
    for (const std::uint32_t reads : automaton.conditions) out.conditions.push_back(number[reads]);
    out.order = election_order(cfg, needs.turns);
  }
  // The left corners a completion looks up: of each nonterminal a kernel item
  // of a state kept reads after its dot, those such an item completes.
  std::vector<bool> asked(nnts, false), completed(nnts, false);
  for (const PackedState& state : out.states)
    for (const std::uint64_t packed : state.kernel) {
      const KernelCost item = KernelCost::of(packed);
      if (item.symbol != KernelCost::kNone) asked[item.symbol] = true;
      if (item.lhs != KernelCost::kAccept) completed[item.lhs] = true;
    }
  needs.corners(asked, completed, out.cornerAt, out.corners);
  gaps.resize(gapOf.size());
  for (auto& [set, at] : gapOf) gaps[at] = set;
  return reduced;
}

// The record each production reduced leaves at each of its call sites, and
// what a lone child's entry becomes under it.
std::set<std::uint32_t> record_closes(Cfg& cfg, const std::vector<bool>& reduced, Packed& out) {
  std::set<std::uint32_t> stamped;
  const std::size_t nnts = cfg.nonterminals;
  cfg.swapRecordInputs();
  const auto children = record_shapes(cfg, out);
  cfg.swapRecordInputs();
  std::vector<std::set<std::uint32_t>> sites(nnts);
  for (std::size_t nt = 0; nt < nnts; ++nt)
    for (const PackedGoto& site : out.gotos[nt]) sites[nt].insert(site.entry);
  for (std::uint32_t p = 1; p < cfg.productions.size(); ++p) {
    if (!reduced[p]) continue;
    const std::uint32_t own = cfg.productions[p].entry;
    for (const std::uint32_t site : sites[cfg.productions[p].lhs]) {
      if (own != kNone && site != kNone)
        out.composed.try_emplace({own, site}, cfg.fuse(own, site));
      const std::uint32_t record = out.closed(own, site);
      if (record == kNone) continue;
      stamped.insert(record);
      for (const std::uint32_t child : children[p])
        out.fused.try_emplace({record, child}, cfg.fuse(child, record));
    }
  }
  return stamped;
}

// The entries the drive stamps on records, numbered as the tape names them:
// those of the leaves, of the records closed, and of what a lone child's
// becomes under them.
void stamped_elements(const Cfg& cfg, std::set<std::uint32_t> stamped, Packed& out) {
  const auto stamp = [&](std::uint32_t entry) {
    if (entry != kNone) stamped.insert(entry);
  };
  for (const std::uint32_t entry : out.terminalEntry) stamp(entry);
  for (const PackedState& state : out.states)
    for (const PackedAction& action : state.actions)
      if (action.kind == PackedAction::Kind::shift) stamp(action.entry);
  for (const auto& [pair, result] : out.fused) {
    stamp(pair.second);
    stamp(result);
  }
  if (stamped.size() >= kNoElement) throw Refused("the grammar has too many records");
  // Deferred member writes, other tree actions, then records with no action.
  enum class Work { none, write, run };
  std::vector<Work> work;
  work.reserve(cfg.entries.size());
  for (const auto& e : cfg.entries) {
    Work kind = e.before == kNone ? Work::none : work[e.before];
    const RowDesc& row = *cfg.rows[e.action];
    if (runs(row.kind)) {
      const bool write = row.kind == WriteKind::flag ||
          ((row.kind == WriteKind::assign || row.kind == WriteKind::append) &&
           row.member->content != detail::FieldContent::node &&
           row.member->content != detail::FieldContent::variant);
      kind = kind == Work::none && write ? Work::write : Work::run;
    }
    work.push_back(kind);
  }
  std::vector<std::uint32_t> order(stamped.begin(), stamped.end());
  const auto end = std::stable_partition(order.begin(), order.end(),
                                         [&](auto e) { return work[e] != Work::none; });
  const auto writes = std::stable_partition(order.begin(), end,
                                            [&](auto e) { return work[e] == Work::write; });
  out.deferred = static_cast<std::size_t>(writes - order.begin());
  for (const std::uint32_t entry : order) {
    out.elementOf.emplace(entry, static_cast<std::uint16_t>(out.elements.size()));
    out.elements.push_back(entry);
  }
  for (const std::uint32_t entry : out.terminalEntry)
    out.terminalElement.push_back(static_cast<std::uint16_t>(out.element(entry)));
}

// The sets of terminals the runtime asks of a lexer state and of a state.
void terminal_sets(const Cfg& cfg, const LexDfa& dfa, Packed& out) {
  std::map<Bits, std::uint16_t> known;
  const auto set_of = [&](const Bits& set) {
    const auto [at, fresh] = known.try_emplace(set, static_cast<std::uint16_t>(known.size()));
    if (fresh) {
      if (known.size() > 0xFFFF) throw Refused("the grammar has too many sets of terminals");
      out.terminalSets.insert(out.terminalSets.end(), set.words().begin(), set.words().end());
    }
    return at->second;
  };
  for (const Bits& set : dfa.reach) out.lexReachOf.push_back(set_of(set));
  // Of the terminals a state acts on, those whose insertion may be required:
  // its own, and those it reduces on that some state needs.
  const std::size_t nterms = cfg.terminals.size();
  Bits needed(nterms);
  for (const PackedState& state : out.states) needed.merge(state.close);
  Bits follow(nterms), mayClose(nterms);
  std::map<std::vector<std::uint64_t>, std::uint16_t> kernelLists;
  out.kernelAt.push_back(0);
  for (const PackedState& state : out.states) {
    follow.clear();
    mayClose.clear();
    for (const PackedAction& action : state.actions) {
      // The terminal no text reads is no word a state follows with.
      if (action.terminal != cfg.unread || cfg.unread == 0) follow.set(action.terminal);
      if (needed.test(action.terminal) &&
          (state.close.test(action.terminal) || action.kind != PackedAction::Kind::shift))
        mayClose.set(action.terminal);
    }
    out.followOf.push_back(set_of(follow));
    out.mayCloseOf.push_back(set_of(mayClose));
    const auto [list, fresh] =
        kernelLists.try_emplace(state.kernel, static_cast<std::uint16_t>(kernelLists.size()));
    if (fresh) {
      if (kernelLists.size() > 0xFFFF) throw Refused("the grammar has too many kernels");
      out.kernels.insert(out.kernels.end(), state.kernel.begin(), state.kernel.end());
      out.kernelAt.push_back(static_cast<std::uint32_t>(out.kernels.size()));
    }
    out.kernelOf.push_back(list->second);
  }
  for (auto [from, to] : {std::pair{&cfg.lines, &out.lines}, {&cfg.newlineIn, &out.newlineIn}}) {
    if (from->empty()) continue;
    follow.clear();
    for (const std::uint16_t t : *from) follow.set(t);
    *to = set_of(follow);
  }
  Bits peers(nterms);
  for (std::size_t i = 1; i < dfa.acceptSets.size(); ++i) {
    const auto& ends = dfa.acceptSets[i];
    if (ends.front().term == ends.back().term) continue;
    if (out.lexTieEnds.empty()) out.lexTieEnds.resize(dfa.acceptSets.size());
    peers.clear();
    for (const LexAccept& end : ends) peers.set(end.term);
    out.lexTieEnds[i] = set_of(peers);
  }
}

// Whether `p` is reduced only on what tests of the token that comes name, or
// on tokens they read again: the parse reads none of them there.
bool tested_only(const Production& p) {
  return !p.tests.empty() && std::ranges::all_of(p.tests, [](const Test& test) {
           return !test.unless && !test.ahead.empty();
         });
}

// The modes: the terminals each state reads, and its trivia; `takes` as
// `lex_rivals` gives it.
void lexer_modes(const Cfg& cfg, const LexDfa& dfa, const std::vector<RivalRow>& takes,
                 Packed& out, std::vector<Bits>& sets, std::vector<std::uint16_t>& setModes) {
  const std::size_t nterms = cfg.terminals.size();
  // Per mode, the terminals whose token may stand where one it reads does,
  // once a doubting state reads in it.
  std::vector<Bits> rivalsOf;
  std::vector<std::uint16_t> level;
  std::map<std::vector<std::uint16_t>, std::uint32_t> peerLists;
  std::vector<std::vector<std::uint16_t>> peers(nterms);
  std::vector<PackedAction> actions;
  Bits trivia(nterms), twins(nterms), passed(nterms);
  for (std::size_t t = 1; t < nterms; ++t) {
    if (cfg.terminals[t].twin != 0)
      twins.set(t);
    else if (cfg.terminals[t].trivia != Trivia::none)
      trivia.set(t);
    if (cfg.terminals[t].trivia != Trivia::none) passed.set(t);
  }
  // Where a bracket mutes line ends, the end of a line is a blank: where a
  // line end byte is no trivia's, every state reads the line's terminal to
  // pass it there, by name only where a trivia may read one, to yield to it.
  bool lines = dfa.nstates() < 2, yields = false;
  for (const unsigned char end : {'\n', '\r'}) {
    if (dfa.nstates() < 2) break;
    const std::uint16_t at = dfa.next[dfa.nclasses + dfa.classOf[end]];
    yields = yields || dfa.reach[at].intersects(passed);
    lines = lines || std::ranges::none_of(dfa.acceptSets[dfa.acceptSet[at]],
                                          [&](const LexAccept& a) { return passed.test(a.term); });
  }
  lines = lines && cfg.newline != 0;
  // The bytes trivia or the line's terminal may read first past a line's
  // blanks, starting at any blank or past them; any byte where that terminal
  // reads blanks alone.
  if (cfg.newline != 0 && (cfg.indent != 0 || cfg.dedent != 0) && dfa.nstates() >= 2) {
    Bits lineish = passed;
    lineish.set(cfg.newline);
    std::vector<std::uint16_t> blanks{1};
    std::vector<bool> seen(dfa.nstates());
    out.lineLeads.assign(256, 0);
    for (std::size_t i = 0; i < blanks.size(); ++i) {
      const std::uint16_t s = blanks[i];
      if (i != 0 && std::ranges::any_of(dfa.acceptSets[dfa.acceptSet[s]],
                                        [&](const LexAccept& a) { return a.term == cfg.newline; }))
        out.lineLeads.assign(256, 1);
      for (unsigned b = 0; b < 256; ++b) {
        const std::uint16_t to = dfa.next[s * dfa.nclasses + dfa.classOf[b]];
        if (to == 0) continue;
        if ((b == ' ' || b == '\t' || b == '\f') && !seen[to]) {
          seen[to] = true;
          blanks.push_back(to);
        }
        if (dfa.reach[to].intersects(lineish)) out.lineLeads[b] = 1;
      }
    }
  }
  auto& modeTerminals = out.modeTerminals;
  // A mode is what it reads, and of that what only tests name.
  std::map<std::pair<Bits, Bits>, std::uint16_t> modes;
  const Bits none(nterms);
  const auto intern = [&](Bits terminals, Bits only) {
    const auto [it, fresh] = modes.try_emplace(std::pair(std::move(terminals), std::move(only)),
                                               static_cast<std::uint16_t>(modes.size()));
    if (fresh) {
      // The three highest numbers key the matcher's and the runs' stretches.
      if (modes.size() >= 0xFFFD) throw Refused("the grammar has too many lexer modes");
      modeTerminals.push_back(it->first.first);
      out.modeNamed.push_back(it->first.second);
    }
    return it->second;
  };
  for (std::uint32_t s = 0; s < out.states.size(); ++s) {
    PackedState& state = out.states[s];
    if (state.reduce != kNone) continue;
    // The end of input is no token of the lexer's: it tells no mode apart.
    // A state passes the trivia of the scopes its tokens are read under.
    Bits read = cfg.regions.size() > 1 ? trivia_of(cfg, state.scopes) : trivia;
    // A state of a section reads the token that closes it wherever it
    // stands; one state reads the text of one section, or none.
    const Region* in = nullptr;
    for (std::size_t r = 0; r < cfg.regions.size(); ++r) {
      if ((state.scopes >> r & 1) == 0) continue;
      const Region& region = cfg.regions[r];
      if (in != nullptr && (region.grammar != in->grammar || region.end != in->end))
        throw Refused("the grammar reaches the same token both inside a `section<>` and outside it");
      in = &region;
      if (region.end != kEndOfInput) read.set(region.end);
    }
    for (const PackedAction& action : state.actions)
      if (action.terminal != kEndOfInput &&
          (action.kind != PackedAction::Kind::reduce ||
           !tested_only(cfg.productions[action.value])))
        read.set(action.terminal);
    if (lines && !yields) read.set(cfg.newline);
    // What only its tests name is read too, but the parse reads its own.
    Bits named(nterms);
    for (const std::uint16_t terminal : state.named)
      if (!read.test(terminal)) named.set(terminal);
    if (lines && yields && !read.test(cfg.newline)) named.set(cfg.newline);
    for (const std::uint16_t terminal : state.follow) {
      read.set(terminal);
      named.reset(terminal);
    }
    read.merge(named);
    trivia_shadows(cfg, state.scopes).each([&](std::size_t t) {
      if (!read.test(t) && !std::ranges::binary_search(state.actions, static_cast<std::uint16_t>(t),
                                                        {}, &PackedAction::terminal))
        state.passes.push_back(static_cast<std::uint16_t>(t));
    });
    // The terminals it reads only for a reduction whose tests may refuse
    // them: where they do, its text is read again without them.
    for (std::size_t i = 0; i < state.actions.size(); ++i) {
      const PackedAction& a = state.actions[i];
      if (a.kind == PackedAction::Kind::reduce && a.terminal != kEndOfInput &&
          cfg.productions[a.value].checks() &&
          (i == 0 || state.actions[i - 1].terminal != a.terminal) &&
          (i + 1 == state.actions.size() || state.actions[i + 1].terminal != a.terminal))
        state.tested.push_back(a.terminal);
    }
    state.mode = intern(std::move(read), std::move(named));
    if (!state.tested.empty()) {
      Bits without = modeTerminals[state.mode], only = out.modeNamed[state.mode];
      for (const std::uint16_t t : state.tested) {
        without.reset(t);
        only.reset(t);
      }
      state.again = intern(std::move(without), std::move(only));
    }
    // Of its doubted terminals, those it reduces on, shifts none of, and reads
    // over the text of another it reads. Where the stack takes none of their
    // reductions, the text is read again without them; of two such, the one
    // read over the other's text is left out first.
    if (state.doubted.empty()) continue;
    if (rivalsOf.size() <= state.mode) rivalsOf.resize(state.mode + 1);
    Bits& rivals = rivalsOf[state.mode];
    if (rivals.words().empty()) {
      rivals = Bits(nterms);
      modeTerminals[state.mode].each([&](std::size_t u) {
        if (!out.modeNamed[state.mode].test(u)) rivals.merge(takes[u]);
      });
    }
    Bits left(nterms);
    for (const std::uint16_t t : state.doubted) {
      const auto [begin, end] =
          std::ranges::equal_range(state.actions, t, {}, &PackedAction::terminal);
      if (begin != end && rivals.test(t) && std::all_of(begin, end, [](const PackedAction& a) {
            return a.kind == PackedAction::Kind::reduce;
          }))
        left.set(t);
    }
    if (!left.any()) continue;
    Bits without = modeTerminals[state.mode], only = out.modeNamed[state.mode];
    std::vector<std::pair<std::uint16_t, std::uint16_t>> reread;
    while (left.any()) {
      level.clear();
      left.each([&](std::size_t t) {
        if (!left.intersects(takes[t])) level.push_back(static_cast<std::uint16_t>(t));
      });
      if (level.empty()) {
        // A longer match can outrank a literal which wins their equal-length tie.
        // Keep the actual competitors for a stack-dependent retry.
        left.each([&](std::uint16_t candidate) {
          level.push_back(candidate);
          for (const auto& [block, mask] : takes[candidate])
            for (auto bits = left.words()[block] & mask; bits; bits &= bits - 1) {
              const auto winner = block * 64u + std::countr_zero(bits);
              peers[winner].push_back(candidate);
              if (out.solo[candidate] == kNoMode) {
                Bits one(nterms);
                one.set(candidate);
                out.solo[candidate] = intern(std::move(one), none);
              }
            }
        });
      }
      for (const std::uint16_t t : level) {
        without.reset(t);
        only.reset(t);
        left.reset(t);
      }
      const std::uint16_t mode = intern(Bits(without), Bits(only));
      for (const std::uint16_t t : level) reread.emplace_back(t, mode);
    }
    std::ranges::sort(reread);
    actions.clear();
    std::size_t next = 0;
    for (std::size_t i = 0; i < state.actions.size(); ++i) {
      actions.push_back(state.actions[i]);
      const std::uint16_t t = state.actions[i].terminal;
      if (next < reread.size() && reread[next].first == t &&
          (i + 1 == state.actions.size() || state.actions[i + 1].terminal != t)) {
        auto& retry = actions.emplace_back(PackedAction{t, PackedAction::Kind::reread, reread[next++].second, s});
        if (!peers[t].empty()) {
          const auto [it, fresh] = peerLists.try_emplace(peers[t], out.retryTerminals.size());
          if (fresh) out.retryTerminals.push_back(it->first);
          retry.rivals = it->second;
          peers[t].clear();
        }
      }
    }
    state.actions.swap(actions);
  }
  // Under a scope, the states a transition joins read one text, a place: a
  // fault is read with what the states of its places shift and what any
  // scope passes. A grammar of one scope reads it with every terminal.
  const std::size_t nregions = cfg.regions.size(), nstates = out.states.size();
  const std::size_t stateModes = modeTerminals.size();
  if (nregions == 1 && stateModes != 0) {
    Bits all(nterms);
    for (std::size_t t = 1; t < nterms; ++t) all.set(t);
    out.modeFault.assign(stateModes, intern(std::move(all), none));
  } else {
    std::vector<std::uint32_t> place(nregions * nstates);
    for (std::size_t at = 0; at < place.size(); ++at)
      place[at] = static_cast<std::uint32_t>(at % nstates);
    const auto place_of = [&](std::size_t r, std::uint32_t state) {
      std::uint32_t* const of = place.data() + r * nstates;
      while (of[state] != state) state = of[state] = of[of[state]];
      return state;
    };
    const auto join = [&](std::uint32_t a, std::uint32_t b) {
      const std::uint64_t both = out.states[a].scopes & out.states[b].scopes;
      for (std::size_t r = 0; r < nregions; ++r)
        if ((both >> r & 1) != 0) place[r * nstates + place_of(r, a)] = place_of(r, b);
    };
    for (std::uint32_t s = 0; s < nstates; ++s)
      for (const PackedAction& action : out.states[s].actions)
        if (action.kind == PackedAction::Kind::shift) join(s, action.value);
    for (const std::vector<PackedGoto>& sites : out.gotos)
      for (const PackedGoto& site : sites) join(site.from, site.target);
    std::map<std::pair<std::size_t, std::uint32_t>, Bits> shifted;
    std::vector<Bits> faults(stateModes, trivia);
    for (std::size_t mode = 0; mode < stateModes; ++mode) {
      Bits passed = modeTerminals[mode];
      passed &= twins;
      faults[mode].merge(passed);
    }
    for (const bool gathered : {false, true})
      for (std::uint32_t s = 0; s < nstates; ++s) {
        const PackedState& state = out.states[s];
        if (state.reduce != kNone) continue;
        for (std::size_t r = 0; r < nregions; ++r) {
          if ((state.scopes >> r & 1) == 0) continue;
          Bits& own = shifted.try_emplace({r, place_of(r, s)}, nterms).first->second;
          if (gathered) {
            faults[state.mode].merge(own);
            continue;
          }
          for (const PackedAction& action : state.actions)
            if (action.kind == PackedAction::Kind::shift) own.set(action.terminal);
        }
      }
    for (Bits& fault : faults) out.modeFault.push_back(intern(std::move(fault), none));
  }
  for (Bits& set : sets) setModes.push_back(intern(std::move(set), none));
  // A fault mode reads its own faults.
  for (std::size_t mode = out.modeFault.size(); mode < modeTerminals.size(); ++mode)
    out.modeFault.push_back(static_cast<std::uint16_t>(mode));
  out.modeCount = static_cast<std::uint16_t>(modeTerminals.size());
  for (const Bits& read : modeTerminals) {
    bool all = true;
    for (std::size_t t = 1; t < nterms; ++t) {
      const Terminal& term = cfg.terminals[t];
      all = all && (term.trivia != Trivia::ignored || term.twin != 0 || read.test(t)) &&
            !(term.twin != 0 && term.trivia == Trivia::none && read.test(t));
    }
    out.modeSkips.push_back(all);
  }
}

// Terminals that can tie at a guarded end. Project lookahead back to the
// token boundary, where a competing terminal may already have ended.
std::map<std::uint16_t, Bits> guarded_rivals(const Cfg& cfg, const LexDfa& dfa) {
  std::map<std::uint16_t, Bits> rivals;
  const auto conditional = [](const auto& ends) {
    return ends.size() > 1 && std::ranges::any_of(ends, [](const LexAccept& a) {
      return a.back == 0 && a.guard > LexTest::match;
    });
  };
  if (std::ranges::none_of(dfa.acceptSets, [&](const auto& ends) {
        return conditional(ends) || std::ranges::any_of(ends, [](const LexAccept& a) { return a.back != 0; });
      })) return rivals;
  std::map<std::tuple<std::uint16_t, bool, std::uint8_t>, std::vector<std::uint16_t>> sources;
  for (std::size_t s = 1; s < dfa.nstates(); ++s)
    for (const LexAccept& a : dfa.acceptSets[dfa.acceptSet[s]])
      if (a.back != 0) sources[{a.term, a.literal, a.back}].push_back(static_cast<std::uint16_t>(s));

  std::map<std::uint16_t, std::vector<LexAccept>> late;
  std::vector<std::uint16_t> previous;
  std::vector<unsigned> seen(dfa.nstates());
  unsigned round = 0;
  for (auto& [end, states] : sources) {
    const auto [term, literal, back] = end;
    for (unsigned left = back; left != 0; --left) {
      previous.clear();
      ++round;
      for (const auto state : states)
        for (const auto pred : dfa.predecessors[state])
          if (std::exchange(seen[pred], round) != round) previous.push_back(pred);
      states.swap(previous);
    }
    for (const auto state : states)
      late[state].push_back({.term = term, .literal = literal});
  }
  const auto n = cfg.terminals.size();
  const auto group = [&](const auto& ends, bool guarded) {
    std::array<std::vector<std::uint16_t>, 4> groups;
    for (const LexAccept& a : ends)
      if (a.guard != LexTest::match && a.back == 0)
        groups[lexical_rank(cfg.terminals[a.term].trivia, a.literal)].push_back(a.term);
    for (auto& terms : groups) {
      std::ranges::sort(terms);
      terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
      if (terms.size() < 2) continue;
      Bits members(n);
      for (const auto t : terms) members.set(t);
      for (const auto t : terms)
        if (guarded || rivals.contains(t)) rivals.try_emplace(t, n).first->second.merge(members);
    }
  };
  for (auto& [there, ends] : late) {
    for (const LexAccept& a : dfa.acceptSets[dfa.acceptSet[there]])
      if (a.back == 0) ends.push_back(a);
    group(ends, true);
  }
  for (const auto& ends : dfa.acceptSets)
    if (conditional(ends)) group(ends, true);
  if (rivals.empty()) return rivals;
  for (const auto& ends : dfa.acceptSets) group(ends, false);
  if (!dfa.matcher.entries.empty()) {
    Bits matched(n);
    for (std::size_t t = 0; t < n; ++t)
      if (dfa.matcher.matched[t]) matched.set(t);
    for (unsigned c = 0; c < dfa.nclasses; ++c) {
      const Bits& opening = dfa.reach[dfa.next[dfa.nclasses + c]];
      if (!opening.intersects(matched)) continue;
      Bits possible = matched;
      possible &= opening;
      for (auto& [t, peers] : rivals)
        if (opening.test(t)) peers.merge(possible);
    }
  }
  for (auto& [t, peers] : rivals) peers.reset(t);
  return rivals;
}

// How each set of terminals ending together reads in each mode: a row per
// mode, the terminal itself where nothing more is asked.
void mode_wins(const Cfg& cfg, const LexDfa& dfa,
               const std::map<std::uint16_t, Bits>& rivals, Packed& out) {
  const std::size_t nsets = dfa.acceptSets.size();
  out.winRow = static_cast<std::uint32_t>(nsets);
  // The keywords some trivia reads on past: read, they keep their token.
  std::vector<bool> beside(cfg.terminals.size(), false);
  {
    Bits trivia(cfg.terminals.size());
    for (std::size_t t = 1; t < cfg.terminals.size(); ++t)
      if (cfg.terminals[t].trivia != Trivia::none) trivia.set(t);
    for (const Bits& reach : dfa.reach) out.lexTrivia.push_back(reach.intersects(trivia));
    for (std::size_t s = 1; s < dfa.nstates(); ++s)
      if (out.lexTrivia[s])
        for (const LexAccept& a : dfa.acceptSets[dfa.acceptSet[s]])
          if (cfg.terminals[a.term].keyword) beside[a.term] = true;
  }
  std::map<LexResolution, std::uint16_t> known;
  const auto intern = [&](const LexResolution& r) {
    const auto [it, fresh] = known.try_emplace(r, static_cast<std::uint16_t>(out.resolutions.size()));
    if (fresh) {
      if (out.resolutions.size() >= kWinNone)
        throw Refused("the terminals end in too many ways for one lexer");
      out.resolutions.push_back(r);
    }
    return it->second;
  };
  out.lexWin.assign(nsets * out.modeCount, kWinNone);
  out.modeTies.assign(out.modeCount, {});
  // A common shift consumes the token. A reduction keeps it for a later
  // state, where two readings of the same spelling may still differ.
  std::vector<std::vector<std::uint32_t>> statesOf(out.modeCount);
  for (std::uint32_t s = 0; s < out.states.size(); ++s)
    if (out.states[s].reduce == kNone)
      out.states[s].eachMode([&](std::uint16_t mode) {
        auto& states = statesOf[mode];
        if (states.empty() || states.back() != s) states.push_back(s);
      });
  const auto told_apart = [&](std::uint16_t mode, std::uint16_t a, std::uint16_t b) {
    const auto cell = [](const PackedState& s, std::uint16_t t) {
      return std::ranges::equal_range(s.actions, t, {}, &PackedAction::terminal);
    };
    const auto same = [&](const PackedAction& x, const PackedAction& y) {
      return x.kind == PackedAction::Kind::shift && x.hops == y.hops && x.key(cfg) == y.key(cfg);
    };
    return std::ranges::any_of(statesOf[mode], [&](std::uint32_t s) {
      return !std::ranges::equal(cell(out.states[s], a), cell(out.states[s], b), same);
    });
  };
  // Longer first; then what the parse reads over what only tests name, a
  // token over trivia, a literal over a pattern.
  const auto rank = [&](std::uint16_t mode, const LexAccept& a) {
    return std::tuple(a.back, out.modeNamed[mode].test(a.term),
                      lexical_rank(cfg.terminals[a.term].trivia, a.literal));
  };
  // The terminals of `mode` ending together in `set`, the one read first.
  // Of two that rank alike, the one a test must let through comes first: the
  // other is what the text reads as where the test fails.
  // Apart, the terminals whose first bytes only end there: the matcher reads
  // each before the others are read.
  std::vector<LexAccept> read, matched;
  const auto ending = [&](std::uint16_t mode, std::size_t set) {
    read.clear();
    matched.clear();
    for (const LexAccept& a : dfa.acceptSets[set])
      if (!out.modeTerminals[mode].test(a.term))
        continue;
      else if (a.guard == LexTest::match)
        matched.push_back(a);
      else
        read.push_back(a);
    std::ranges::stable_sort(read, {}, [&](const LexAccept& a) {
      return std::tuple(rank(mode, a), a.guard == LexTest::none);
    });
  };
  // A tie is the terminal delivered by the lexer followed by its alternatives.
  std::map<std::pair<std::vector<std::uint16_t>, bool>, std::uint32_t> ties;
  std::vector<std::uint16_t> lastMode, tied;
  const auto tie = [&](std::uint16_t mode, std::size_t set, bool checked) {
    const auto [it, fresh] = ties.try_emplace({tied, checked}, out.ties.size());
    if (fresh) {
      const auto row = checked ? kNone : static_cast<std::uint32_t>(out.tieSets.size());
      out.ties.push_back({tied, row});
      if (!checked) out.tieSets.emplace_back(nsets);
      lastMode.push_back(kNoMode);
    }
    const auto id = it->second;
    if (!checked) out.tieSets[out.ties[id].row].set(set);
    if (std::exchange(lastMode[id], mode) != mode) out.modeTies[mode].push_back(id);
  };
  std::vector<std::size_t> tiedIn(cfg.terminals.size());
  std::vector<std::size_t> chain;
  for (std::uint16_t mode = 0; mode < out.modeCount; ++mode) {
    for (const auto& [first, peers] : rivals) {
      if (!out.modeTerminals[mode].test(first)) continue;
      tied.assign(1, first);
      peers.each([&](std::uint16_t t) {
        if (out.modeTerminals[mode].test(t) && out.modeNamed[mode].test(t) == out.modeNamed[mode].test(first) &&
            told_apart(mode, first, t)) tied.push_back(t);
      });
      if (tied.size() > 1) tie(mode, 0, true);
    }
    for (std::size_t set = 1; set < nsets; ++set) {
      ending(mode, set);
      if (read.empty() && matched.empty()) continue;
      // Where every state of the mode does the same with all, the first is
      // read.
      if (!read.empty() && read[0].back == 0 && !rivals.contains(read[0].term)) {
        const auto first = read[0].term;
        tied.assign(1, first);
        const auto group = mode * nsets + set;
        tiedIn[first] = group;
        for (std::size_t k = 1; k < read.size() && rank(mode, read[k]) == rank(mode, read[0]); ++k) {
          const auto term = read[k].term;
          if (std::exchange(tiedIn[term], group) != group && told_apart(mode, first, term))
            tied.push_back(term);
        }
        if (tied.size() > 1) tie(mode, set, false);
      }
      // Each is read where its test holds, else the next: none past the
      // first that asks nothing, and one alone of those the end of input
      // lets through.
      chain.clear();
      bool ends = false;
      for (std::size_t k = 0; k < read.size(); ++k) {
        if (read[k].guard == LexTest::eof && std::exchange(ends, true)) continue;
        chain.push_back(k);
        if (read[k].guard == LexTest::none) break;
      }
      std::uint16_t win = kWinNone;
      for (std::size_t at = chain.size(); at-- > 0;) {
        const std::size_t k = chain[at];
        const LexAccept& a = read[k];
        const bool named = out.modeNamed[mode].test(a.term);
        const bool cuts = beside[a.term] && !named;
        const bool literal = a.literal && out.mixed[a.term];
        if (a.guard == LexTest::none && a.back == 0 && !cuts && !named && !literal) {
          win = a.term;
          continue;
        }
        LexResolution r;
        r.token = a.term;
        r.otherwise = win;
        r.back = a.back;
        r.literal = literal;
        r.keyword = cuts;
        r.named = named;
        r.test = a.guard;
        r.waits = a.waits;
        win = static_cast<std::uint16_t>(kWinResolved | intern(r));
      }
      for (std::size_t k = matched.size(); k-- > 0;) {
        const LexAccept& branch = matched[k];
        LexResolution r;
        r.token = branch.term;
        r.literal = branch.literal && out.mixed[r.token];
        r.named = out.modeNamed[mode].test(r.token);
        r.keyword = beside[r.token] && !r.named;
        r.otherwise = win;
        r.test = LexTest::match;
        r.waits = branch.waits;
        win = static_cast<std::uint16_t>(kWinResolved | intern(r));
      }
      out.lexWin[mode * nsets + set] = win;
    }
  }
}

// Specialise an opening for the reachable terminals and their priorities.
// Terminals named only in tests have lower priority than those the parser reads.
void first_bytes(const Cfg& cfg, const LexDfa& dfa, RunSets& runs, Packed& out) {
  const std::vector<Bits>& reach = dfa.reach;
  const std::size_t nsets = dfa.acceptSets.size();
  const std::size_t nlex = dfa.nstates();
  const unsigned nclasses = dfa.nclasses;
  std::map<WordPath, std::uint32_t> paths;
  std::map<std::vector<std::uint16_t>, std::uint32_t> literalLists;
  std::map<std::tuple<std::uint16_t, Bits, Bits>, std::uint32_t> known;
  out.lexFirst.assign(std::size_t{out.modeCount} * 256, 0);
  out.onwardRow = static_cast<std::uint32_t>((nlex + 7) / 8);
  out.lexOnward.assign(std::size_t{out.modeCount} * out.onwardRow, 0);
  Bits ascii(nclasses), all(nclasses);
  for (unsigned c = 0; c < nclasses; ++c) all.set(c);
  for (unsigned b = 0; b < 0x80; ++b) ascii.set(dfa.classOf[b]);
  std::vector<std::uint32_t> seen(nlex, 0);
  std::uint32_t stamp = 0;
  // A positive length is known when every path here follows the same chain
  // of predecessor states from the first byte of the run.
  std::vector<std::uint16_t> parent(nlex), length(nlex);
  std::vector<bool> sinks(nlex, false), lives(nlex, false);
  for (std::uint16_t mode = 0; mode < out.modeCount; ++mode) {
    const Bits& read = out.modeTerminals[mode];
    // The states a terminal of the mode ends in or past, and those one
    // ends past.
    for (std::size_t s = 1; s < nlex; ++s) lives[s] = reach[s].intersects(read);
    std::uint8_t* const onwardBits = out.lexOnward.data() + std::size_t{mode} * out.onwardRow;
    for (std::size_t s = 1; s < nlex; ++s)
      for (const std::uint16_t to : out.paths.after(s))
        if (lives[to]) {
          onwardBits[s >> 3] |= static_cast<std::uint8_t>(1u << (s & 7));
          break;
        }
    const auto live = [&](std::uint16_t s) { return lives[s]; };
    const auto win_of = [&](std::uint16_t s) {
      return out.lexWin[mode * nsets + dfa.acceptSet[s]];
    };
    // The states a token's end is read one byte into: they lead to no state
    // the mode can still end in, and end the token a byte back.
    for (std::size_t s = 1; s < nlex; ++s) {
      const std::uint16_t w = win_of(static_cast<std::uint16_t>(s));
      const LexResolution* r = w > kWinNone ? &out.resolutions[w & ~kWinResolved] : nullptr;
      sinks[s] = lives[s] && ((onwardBits[s >> 3] >> (s & 7)) & 1) == 0 && r != nullptr &&
                 r->test == LexTest::none && r->back == 1 && !r->named && !r->literal;
    }
    // The classes that lead from `s` to a state the mode can still end in,
    // but a sink.
    const auto onward = [&](std::uint16_t s) {
      Bits classes(nclasses);
      for (unsigned c = 0; c < nclasses; ++c)
        if (const std::uint16_t to = dfa.next[s * nclasses + c]; live(to) && !sinks[to])
          classes.set(c);
      return classes;
    };
    // Visits live states from `from` through `classes`; false as soon as
    // `each` refuses one.
    const auto each_from = [&](std::uint16_t from, const Bits& classes, auto&& each) {
      std::vector<std::uint16_t> work{from};
      seen[from] = ++stamp;
      while (!work.empty()) {
        const std::uint16_t s = work.back();
        work.pop_back();
        if (!each(s)) return false;
        for (unsigned c = 0; c < nclasses; ++c)
          if (const std::uint16_t to = dfa.next[s * nclasses + c];
              classes.test(c) && live(to) && seen[to] != stamp) {
            seen[to] = stamp;
            work.push_back(to);
          }
      }
      return true;
    };
    // A literal its bytes spell, as the code reads it: one read in either
    // case past ASCII is the automaton's.
    const auto coded = [&](const Terminal& k) {
      return k.literal() && !(k.folded && std::ranges::any_of(k.text, [](char c) {
        return static_cast<unsigned char>(c) >= 0x80;
      }));
    };
    // The terminal `word` reads where a run stops, at the end of the input if
    // `atEnd`, where no word goes on past it if `noWord`: kWinNone where none,
    // kNoToken where the text past the stop tells or what ends is not alone.
    const auto stopped = [&](std::uint16_t word, bool atEnd, bool noWord) {
      while (word > kWinNone) {
        const LexResolution& r = out.resolutions[word & ~kWinResolved];
        const int holds = r.test == LexTest::eof                   ? int{atEnd}
                          : r.test == LexTest::none                ? 1
                          : r.test == LexTest::word && r.back == 0 ? int{noWord || atEnd}
                                                                   : -1;
        if (holds < 0 || (holds > 0 && (r.back != 0 || r.named || r.literal))) return kNoToken;
        if (holds > 0) return r.token;
        word = r.otherwise;
      }
      return word;
    };
    // All states in the run read the same byte classes. They end the same
    // token except for states reached at a known length.
    const auto word = [&](std::uint16_t from, WordPath& path, unsigned limit) {
      const Bits& alphabet = limit == 256 ? all : ascii;
      Bits classes = onward(from);
      classes &= alphabet;
      std::vector<std::uint16_t> order;
      bool keyworded = false;
      const bool one = each_from(from, classes, [&](std::uint16_t s) {
        const Bits next = onward(s);
        if (!Bits::same_within(next, classes, alphabet)) return false;
        path.wide = path.wide || next != classes;
        for (std::uint16_t w = win_of(s); w > kWinNone;) {
          const LexResolution& r = out.resolutions[w & ~kWinResolved];
          if (r.test == LexTest::match || r.literal) return false;
          keyworded = keyworded || r.keyword;
          w = r.otherwise;
        }
        order.push_back(s);
        return true;
      });
      if (!one) return false;
      // The run's bytes; the classes of the bytes it stops before, of no word
      // and of a word, those past ASCII left to the automaton where wide.
      ByteSet bytes;
      for (unsigned b = 0; b < 256; ++b)
        if (classes.test(dfa.classOf[b])) {
          if (b < limit) bytes.add(b);
          else path.wide = true;
        }
      std::array<Bits, 2> stops{Bits(nclasses), Bits(nclasses)};
      for (unsigned b = 0; b < 256; ++b)
        if (!bytes.has(b) && (b < 0x80 || !path.wide)) {
          if (out.wordBytes[b] != 1) stops[0].set(dfa.classOf[b]);
          if (out.wordBytes[b] != 0) stops[1].set(dfa.classOf[b]);
        }
      // What `s` reads where the run stops: at the end of the input, before a
      // byte of no word, and before one of a word. It ends a token there, or
      // the byte leads to a sink, which wins a tie by rank; kNoToken where
      // the walk tells.
      const auto ends_of = [&](std::uint16_t s) {
        std::array<std::uint16_t, 3> e{};
        for (unsigned k = 0; k < 3; ++k) {
          const std::uint16_t own = stopped(win_of(s), k == 0, k < 2);
          std::uint16_t read = k == 0 ? own : kWinResolved;
          for (unsigned c = 0; k > 0 && c < nclasses; ++c)
            if (stops[k - 1].test(c)) {
              const std::uint16_t to = dfa.next[s * nclasses + c];
              const std::uint16_t past =
                  sinks[to] ? out.resolutions[win_of(to) & ~kWinResolved].token : kWinNone;
              const std::uint16_t won =
                  own == kNoToken   ? kNoToken
                  : own == kWinNone ? past
                  : past != kWinNone && out.terminalRank[past] <= out.terminalRank[own] ? past
                                                                                        : own;
              read = read == kWinResolved || read == won ? won : kNoToken;
            }
          e[k] = read == kWinResolved ? e[k - 1] : read == kWinNone ? kNoToken : read;
        }
        return e;
      };
      for (const auto s : order) parent[s] = kNoToken;
      for (const auto s : order)
        for (unsigned c = 0; c < nclasses; ++c)
          if (const auto to = dfa.next[s * nclasses + c]; classes.test(c)) {
            auto& p = parent[to];
            if (p == kNoToken) p = s;
            else if (p != s) p = 0;
          }
      for (const auto s : order) {
        const auto p = parent[s];
        length[s] = s == from ? (p == kNoToken) : p && length[p] ? length[p] + 1 : 0;
      }
      std::uint16_t pattern = kNoToken;
      for (const std::uint16_t s : order)
        if (length[s] == 0) {
          const auto e = ends_of(s);
          if (e[0] >= kWinNone || e[0] != e[1] || e[1] != e[2] ||
              (pattern != kNoToken && e[0] != pattern))
            return false;
          pattern = e[0];
        }
      const auto quiet = [&](std::uint16_t t) {
        return keyworded && t != kNoToken && cfg.terminals[t].trivia != Trivia::none;
      };
      if (pattern == kNoToken || quiet(pattern)) return false;
      for (const std::uint16_t s : order) {
        if (length[s] == 0) continue;
        const auto [atEnd, before, beforeWord] = ends_of(s);
        if (atEnd == pattern && before == pattern && beforeWord == pattern) continue;
        const auto terminal = atEnd == before && beforeWord == (stops[1].any() ? pattern : before)
                                  ? before : kNoToken;
        if (quiet(terminal)) return false;
        path.spelled.push_back({length[s], s, terminal});
      }
      if (!path.spelled.empty()) path.first = from;
      path.terminal = pattern;
      path.run = runs.of(bytes);
      return true;
    };
    // Whether only literals end from `from` on: those literals, the longest
    // first. A literal read under a word's guard gives way to nothing.
    const auto literals = [&](std::uint16_t from, std::vector<std::uint16_t>& found) {
      const bool only = each_from(from, all, [&](std::uint16_t s) {
        std::uint16_t terminal = win_of(s);
        if (terminal == kWinNone) return true;
        if (terminal > kWinNone) {
          const LexResolution& r = out.resolutions[terminal & ~kWinResolved];
          if (r.test != LexTest::word || r.back != 0 || r.otherwise != kWinNone || r.named || r.literal)
            return false;
          terminal = r.token;
        }
        const Terminal& k = cfg.terminals[terminal];
        if (!coded(k) || k.trivia != Trivia::none) return false;
        found.push_back(terminal);
        return true;
      });
      if (!only || found.empty()) return false;
      std::ranges::sort(found);
      found.erase(std::unique(found.begin(), found.end()), found.end());
      std::ranges::stable_sort(found, [&](std::uint16_t a, std::uint16_t b) {
        return cfg.terminals[a].text.size() > cfg.terminals[b].text.size();
      });
      return true;
    };
    const auto first = [&](std::uint16_t s) -> std::uint32_t {
      if (WordPath path; onward(s).any() &&
                         (word(s, path, 256) || (path = {}, word(s, path, 0x80)))) {
        const auto [at, added] = paths.try_emplace(path, out.wordPaths.size());
        if (added) out.wordPaths.push_back(std::move(path));
        return kFirstWord | at->second;
      } else if (std::vector<std::uint16_t> found; literals(s, found)) {
        const auto [at, added] = literalLists.try_emplace(found, out.literalPaths.size());
        if (added) out.literalPaths.push_back(std::move(found));
        return kFirstLiterals | at->second;
      }
      return 0;
    };
    std::vector<std::uint32_t> ofClass(nclasses, 0);
    for (unsigned c = 0; c < nclasses && nlex > 1; ++c) {
      const std::uint16_t s = dfa.next[nclasses + c];
      if (!live(s)) continue;
      Bits among = reach[s];
      among &= read;
      Bits named;
      if (among.intersects(out.modeNamed[mode])) {
        named = among;
        named &= out.modeNamed[mode];
      }
      const auto [it, fresh] = known.try_emplace({s, std::move(among), std::move(named)}, 0);
      if (fresh) it->second = first(s);
      ofClass[c] = it->second;
    }
    for (unsigned b = 0; b < 256; ++b)
      out.lexFirst[std::size_t{mode} * 256 + b] = ofClass[dfa.classOf[b]];
  }
}

// Compare a section close with the terminals read in its embedded grammar.
void check_section_ends(const Cfg& cfg, const LexDfa& dfa, Packed& out) {
  struct Overlaps { Bits possible, certain; };
  struct Checked { Overlaps overlaps; std::set<std::uint16_t> modes; };
  if (std::ranges::all_of(cfg.regions, [](const Region& r) { return r.end == kEndOfInput; })) return;
  int back = 0;
  for (const auto& set : dfa.acceptSets)
    for (const auto& a : set) back = std::max(back, int{a.back});
  const auto overlaps = [&](std::uint16_t end) {
    const auto n = cfg.terminals.size();
    Overlaps ends{Bits(n), Bits(n)};
    const auto& terminal = cfg.terminals[end];
    if (!terminal.literal() && terminal.words.empty()) {
      for (unsigned c = 0; c < dfa.nclasses && dfa.nstates() > 1; ++c) {
        const auto& opening = dfa.reach[dfa.next[dfa.nclasses + c]];
        if (opening.test(end)) ends.possible.merge(opening);
      }
      return ends;
    }
    std::vector<std::uint16_t> from, next;
    std::vector<bool> seen(dfa.nstates());
    using Step = std::pair<std::uint16_t, int>;
    std::vector<Step> certain, possible;
    terminal.each_word([&](const GrammarWord& word) {
      from.assign(1, 1);
      for (auto text = word.text; !text.empty() && !from.empty();) {
        std::size_t length = 1;
        const auto scalar = word.folded ? scalarAt(text.data(), text.size(), length) : kNoScalar;
        const auto spell = [&](std::string_view bytes) {
          for (auto s : from) {
            for (const unsigned char c : bytes) {
              s = dfa.next[s * dfa.nclasses + dfa.classOf[c]];
              if (!s) break;
              for (const auto& a : dfa.acceptSets[dfa.acceptSet[s]])
                if (a.guard == LexTest::match) ends.possible.set(a.term);
            }
            if (s && !seen[s]) {
              seen[s] = true;
              next.push_back(s);
            }
          }
        };
        if (scalar == kNoScalar) length = 1;
        spell(text.substr(0, length));
        if (scalar != kNoScalar)
          detail::for_each_other_case(scalar, [&](char32_t other) {
            std::string bytes;
            appendScalar(bytes, other);
            spell(bytes);
          });
        for (const auto s : next) seen[s] = false;
        from.swap(next);
        next.clear();
        text.remove_prefix(length);
      }
      for (const auto s : from) {
        const auto& accepts = dfa.acceptSets[dfa.acceptSet[s]];
        bool plain = false, boundary = false;
        for (const auto& a : accepts)
          if (a.term == end && a.back == 0) {
            plain |= a.guard == LexTest::none;
            boundary |= a.guard == LexTest::word;
          }
        if (plain) {
          certain.emplace_back(s, 0);
        } else if (boundary) {
          // At the word's end only a non-word byte or the end of input can follow.
          for (const auto& a : accepts)
            if (a.back == 0) {
              ends.possible.set(a.term);
              if (a.guard == LexTest::none || a.guard == LexTest::word || a.guard == LexTest::eof)
                ends.certain.set(a.term);
            }
          for (unsigned b = 0; b < 256; ++b)
            if (out.wordBytes[b] != 1)
              (out.wordBytes[b] == 0 ? certain : possible).emplace_back(
                  dfa.next[s * dfa.nclasses + dfa.classOf[b]], 1);
        } else {
          possible.emplace_back(s, 0);
        }
      }
    });
    // A token overlaps if its consumed end reaches the delimiter's end.
    // Beyond the largest lookahead, further distance no longer distinguishes paths.
    for (auto* work : {&certain, &possible}) {
      if (work->empty()) continue;
      std::vector<int> far(dfa.nstates(), -1);
      while (!work->empty()) {
        const auto [s, distance] = work->back();
        work->pop_back();
        if (!s || distance <= far[s]) continue;
        far[s] = distance;
        for (const auto& a : dfa.acceptSets[dfa.acceptSet[s]])
          if (a.guard == LexTest::match || a.back <= distance) {
            const bool proved = work == &certain && a.guard != LexTest::match && a.guard != LexTest::waits;
            (proved ? ends.certain : ends.possible).set(a.term);
          }
        const auto further = std::min(distance + 1, back);
        for (const auto to : dfa.paths.after(s))
          if (far[to] < further) work->emplace_back(to, further);
      }
    }
    return ends;
  };
  std::map<std::uint16_t, Checked> checked;
  std::set<std::pair<std::uint16_t, std::uint16_t>> warned;
  for (const PackedState& state : out.states) {
    if (state.reduce != kNone) continue;
    for (std::size_t r = 0; r < cfg.regions.size(); ++r) {
      const auto end = cfg.regions[r].end;
      if ((state.scopes >> r & 1) == 0 || end == kEndOfInput) continue;
      auto [it, fresh] = checked.try_emplace(end);
      auto& [ends, modes] = it->second;
      if (!modes.insert(state.mode).second) continue;
      if (fresh) ends = overlaps(end);
      for (std::size_t t = 1; t < cfg.terminals.size(); ++t) {
        if (t == end || cfg.terminals[t].twin == end || !out.modeTerminals[state.mode].test(t)) continue;
        if (ends.certain.test(t))
          throw Refused("the grammar closes a `section<>` on a token that the embedded grammar "
                        "also reads, or that starts one of its tokens");
        if (ends.possible.test(t) && warned.emplace(end, static_cast<std::uint16_t>(t)).second)
          out.warnings.push_back("terminal `" + cfg.terminals[end].name +
                                 "` closes a `section<>`, but the generator could not check that `" +
                                 cfg.terminals[t].name +
                                 "`, a terminal of the embedded grammar, neither matches it nor "
                                 "starts with it");
      }
    }
  }
}

// The bytes dropped before a token: each alone reads as ignored terminals
// only. One that follows them otherwise than it begins a token is read
// again by the lexer's walk, from the first byte dropped.
void dropped_bytes(const Cfg& cfg, const LexDfa& dfa, Packed& out) {
  if (dfa.nstates() < 2) return;
  const std::size_t nterms = cfg.terminals.size();
  const std::vector<Bits>& reach = dfa.reach;
  // A twin reads where its terminal does, which decides.
  Bits ignored(nterms);
  for (std::size_t t = 1; t < nterms; ++t)
    if (cfg.terminals[t].trivia == Trivia::ignored || cfg.terminals[t].twin != 0) ignored.set(t);
  const unsigned nclasses = dfa.nclasses;
  std::vector<bool> dropped(nclasses, false);
  std::vector<bool> resumed(nclasses, false);
  for (unsigned c = 0; c < nclasses; ++c) {
    const std::uint16_t to = dfa.next[nclasses + c];
    if (to == 0 || dfa.acceptSet[to] == 0) continue;
    Bits others = reach[to];
    others.remove(ignored);
    bool plain = !others.any();
    for (const LexAccept& a : dfa.acceptSets[dfa.acceptSet[to]])
      plain = plain && a.back == 0 && a.guard == LexTest::none;
    dropped[c] = plain;
  }
  for (unsigned c = 0; c < nclasses; ++c) {
    if (!dropped[c]) continue;
    const std::uint16_t to = dfa.next[nclasses + c];
    for (unsigned d = 0; d < nclasses; ++d)
      if (const std::uint16_t on = dfa.next[to * nclasses + d];
          on != 0 && on != dfa.next[nclasses + d])
        resumed[d] = true;
  }
  for (unsigned b = 0; b < 256; ++b) {
    const unsigned c = dfa.classOf[b];
    if (resumed[c])
      out.resumed.add(b);
    else if (dropped[c])
      out.skipped.add(b);
  }
}

// Per mode, the terminal that closes the section its states read in: the end
// of a scope a state reads under, where every state of the mode has that one.
void section_closes(const Cfg& cfg, Packed& out) {
  constexpr std::uint32_t kUnseen = kNoToken + 1u;
  std::vector<std::uint32_t> close(out.modeCount, kUnseen);
  for (const PackedState& state : out.states) {
    if (state.reduce != kNone) continue;
    std::uint32_t own = kNoToken;
    for (std::size_t r = 0; r < cfg.regions.size(); ++r)
      if (((state.scopes >> r) & 1) != 0 && cfg.regions[r].end != kEndOfInput)
        own = own == kNoToken || own == cfg.regions[r].end ? cfg.regions[r].end : kNone;
    std::uint32_t& mode = close[state.mode];
    mode = mode == kUnseen || mode == own ? own : kNone;
  }
  out.modeClose.clear();
  for (const std::uint32_t c : close)
    out.modeClose.push_back(c == kUnseen || c == kNone ? kNoToken : static_cast<std::uint16_t>(c));
}

constexpr std::uint32_t kUncertain = kNone - 1;

// Per terminal, the place among its actions of the one action `state` takes
// on it: kNone where it takes none, kUncertain where several, a pass or a tie.
const std::vector<std::uint32_t>& one_actions(PrefixPredictions::Cache& cache, const Cfg& cfg,
                                              const Packed& p, std::uint32_t state) {
  auto& row = cache.actions[state];
  if (!row.empty()) return row;
  const PackedState& s = p.states[state];
  row.assign(cfg.terminals.size() + 1, kNone);
  for (const auto t : s.passes) row[t] = kUncertain;
  for (std::size_t i = 0; i < s.actions.size();) {
    const auto t = s.actions[i].terminal;
    const auto first = i++;
    while (i < s.actions.size() && s.actions[i].terminal == t) ++i;
    row[t] = (i == first + 1 || s.actions[first + 1].kind == PackedAction::Kind::reread)
                 ? static_cast<std::uint32_t>(first) : kUncertain;
  }
  cache.tied[s.mode].each([&](std::size_t t) { row[t] = kUncertain; });
  return row;
}

}  // namespace

LexRivals lex_rivals(const GrammarImage& image, const Cfg& cfg, const LexDfa& dfa) {
  const std::size_t nterms = cfg.terminals.size();
  LexRivals out{std::vector<RivalRow>(nterms), std::vector<RivalRow>(nterms)};
  const auto rank = [&](const LexAccept& a) {
    return lexical_rank(cfg.terminals[a.term].trivia, a.literal);
  };
  const std::array<std::uint8_t, 256> bytes = word_bytes(word_of(image));
  std::vector<bool> word(dfa.nclasses, false);
  for (unsigned b = 0; b < 256; ++b)
    if (bytes[b] != 0) word[dfa.classOf[b]] = true;
  const auto add = [](RivalRow& row, std::size_t block, std::uint64_t bits) {
    if (!bits) return;
    if (row.empty() || row.back().first != block)
      row.emplace_back(static_cast<std::uint16_t>(block), 0);
    row.back().second |= bits;
  };
  for (std::size_t block = 0; block < (nterms + 63) / 64; ++block) {
    for (std::size_t s = 1; s < dfa.nstates(); ++s) {
      const auto& accepts = dfa.acceptSets[dfa.acceptSet[s]];
      if (accepts.empty()) continue;
      std::uint64_t past = 0, apart = 0;
      for (unsigned c = 0; c < dfa.nclasses; ++c)
        if (const auto to = dfa.next[s * dfa.nclasses + c]; to != 0)
          (word[c] ? past : apart) |= dfa.reach[to].words()[block];
      std::array<std::uint64_t, 4> better{};
      if (accepts.size() > 1)
        for (const LexAccept& a : accepts)
          if (a.term / 64 == block)
            for (std::size_t r = rank(a) + 1; r < better.size(); ++r)
              better[r] |= std::uint64_t{1} << (a.term % 64);
      for (const LexAccept& a : accepts) {
        const auto self = a.term / 64 == block ? std::uint64_t{1} << (a.term % 64) : 0;
        add(out.reads[a.term], block, dfa.reach[s].words()[block] & ~self);
        const auto takes = apart | (a.guard == LexTest::word ? 0 : past) | better[rank(a)];
        add(out.takes[a.term], block, takes & ~self);
      }
    }
  }
  return out;
}

void prefix_predictions(PrefixPredictions& out, const Cfg& cfg, const Packed& p,
    std::uint32_t state, std::uint32_t cell, std::span<const PackedAction> actions, bool tied) {
  constexpr std::size_t kWalkSteps = 32;
  // A cell whose last action reads the token again is told by the token in
  // hand; the others read the next token in a mode, whose reader sees neither
  // indentation nor a trivia's line end, which may stand before any token.
  const bool rereads = actions.back().kind == PackedAction::Kind::reread;
  if (!rereads && (cfg.indent != 0 || cfg.dedent != 0 || !cfg.newlineIn.empty())) return;
  const auto terminal = actions.front().terminal;
  if (terminal == kEndOfInput) return;
  if (out.cache.tied.empty()) {
    out.cache.tied.assign(p.modeCount, Bits(cfg.terminals.size() + 1));
    for (std::uint16_t mode = 0; mode < p.modeCount; ++mode)
      for (auto tie : p.modeTies[mode])
        for (auto t : p.ties[tie].terminals) out.cache.tied[mode].set(t);
    out.cache.actions.resize(p.states.size());
    out.cache.visited.resize(2 * p.states.size());
    out.cache.predecessors.resize(p.states.size());
    std::map<std::vector<std::uint16_t>, std::uint32_t> readers;
    std::vector<std::uint16_t> signature;
    for (std::uint32_t from = 0; from < p.states.size(); ++from) {
      signature.assign(1, p.states[from].mode);
      for (const PackedAction& a : p.states[from].actions) {
        signature.push_back(a.terminal);
        if (a.kind == PackedAction::Kind::shift) out.cache.predecessors[a.value].push_back(from);
      }
      out.cache.readerOf.push_back(readers.try_emplace(signature, readers.size()).first->second);
    }
    for (std::uint32_t nt = 0; nt < p.gotos.size(); ++nt)
      for (const PackedGoto& site : p.gotos[nt]) {
        out.cache.predecessors[site.target].push_back(site.from);
        out.cache.sites.try_emplace(std::uint64_t{nt} << 32 | site.from, site.target);
      }
  }
  if (!tied && out.cache.tied[p.states[state].mode].test(terminal)) return;
  struct Walk {
    const Cfg& cfg;
    const Packed& p;
    PrefixPredictions& predictions;
    std::uint32_t depth, previous;
    std::vector<std::uint32_t> stack;
    // States reached with a held token or, past the state count, by reading one.
    std::vector<std::uint32_t> made;
    // Past the frames it knows: how deep it went, and on what.
    std::uint32_t below = 0, missing = 0, lhs = 0;
    std::uint8_t steps = 0;
    // Whether it stopped on a state that neither acts on its token nor reads
    // it again.
    bool refused = false;

    // Begins anew over `from` in context `context` at `depth`, `under`
    // frames of the context below it.
    void begin(std::uint32_t at, std::uint32_t context, std::span<const std::uint32_t> from,
               std::uint32_t under) {
      depth = at;
      previous = context;
      stack.assign(from.begin(), from.end());
      made.clear();
      below = under;
      missing = 0;
      steps = 0;
      refused = false;
    }

    // Goes from the top state on `nonterminal`, the token `held` or read.
    bool go(std::uint32_t nonterminal, bool held) {
      const auto site = predictions.cache.sites.find(std::uint64_t{nonterminal} << 32 | stack.back());
      if (site == predictions.cache.sites.end()) return false;
      const auto target = site->second.target;
      if (held && p.states[target].rereads()) return false;
      const auto node = target + (held ? 0 : static_cast<std::uint32_t>(p.states.size()));
      if (std::ranges::find(made, node) != made.end()) return false;
      stack.push_back(target);
      made.push_back(node);
      ++steps;
      return true;
    }

    bool reduce(std::uint32_t production, bool held) {
      const Production& rule = cfg.productions[production];
      if (steps == kWalkSteps || rule.tested() || rule.start || rule.implied) return false;
      if (rule.rhs.size() >= stack.size()) {
        const auto wanted = below + rule.rhs.size() + 1 - stack.size();
        if (wanted != depth) {
          missing = static_cast<std::uint32_t>(wanted);
          lhs = rule.lhs;
          return false;
        }
        stack.assign(1, previous);
        below = depth;
      } else {
        stack.resize(stack.size() - rule.rhs.size());
      }
      return go(rule.lhs, held);
    }

    // The one action of the top state on `terminal`.
    bool on(std::uint16_t terminal, PackedAction& action) {
      const PackedState& state = p.states[stack.back()];
      if (state.reduce != kNone) {
        action = {terminal, PackedAction::Kind::reduce, state.reduce};
        return true;
      }
      const auto i = one_actions(predictions.cache, cfg, p, stack.back())[terminal];
      if (i >= kUncertain) {
        refused = i == kNone;
        return false;
      }
      action = state.actions[i];
      return true;
    }

    // Takes `action` on `terminal`, then the top state's, to its shift.
    bool shift(std::uint16_t terminal, PackedAction action) {
      for (;;) {
        if (action.kind == PackedAction::Kind::shift) {
          if (steps == kWalkSteps || action.gap != kAnyGap) return false;
          stack.push_back(action.value);
          ++steps;
          return true;
        }
        if (action.kind != PackedAction::Kind::reduce || !reduce(action.value, true) ||
            !on(terminal, action))
          return false;
      }
    }

    bool reader() {
      made.push_back(static_cast<std::uint32_t>(p.states.size()) + stack.back());
      while (p.states[stack.back()].reduce != kNone)
        if (!reduce(p.states[stack.back()].reduce, false)) return false;
      return true;
    }
  };
  Walk w{cfg, p, out, 0, 0, {}, {}};
  Walk up{cfg, p, out, 0, 0, {}, {}};
  // A target that reduces at once climbs alike on every token where the walk
  // stops on its production or pops it past the site's two frames.
  const auto alike = [&](const PackedState& target) {
    const Production& rule = cfg.productions[target.reduce];
    return rule.rhs.size() >= 2 || rule.tested() || rule.start || rule.implied;
  };
  // The walks from state `from` on `nonterminal` by token held, none where
  // it has no goto.
  const auto walks = [&](std::uint32_t from, std::uint32_t nonterminal) -> PrefixPredictions::Site* {
    const auto site = out.cache.sites.find(std::uint64_t{nonterminal} << 32 | from);
    if (site == out.cache.sites.end() || p.states[site->second.target].rereads()) return nullptr;
    PrefixPredictions::Site& at = site->second;
    if (at.columns == nullptr && at.climbs.empty()) {
      const PackedState& target = p.states[at.target];
      if (target.reduce == kNone) {
        at.columns = one_actions(out.cache, cfg, p, at.target).data();
        at.climbs.resize(target.actions.size());
      } else {
        at.climbs.resize(alike(target) ? 1 : cfg.terminals.size() + 1);
      }
    }
    return &at;
  };
  const auto climb = [&](PrefixPredictions::Site* site, std::uint32_t from, std::uint16_t t) {
    if (!site) return PrefixPredictions::Climb{0, 0, 0, false};
    const std::uint32_t column = site->columns != nullptr ? site->columns[t]
                                 : site->climbs.size() == 1 ? 0
                                                            : t;
    // A target that reads the token and takes no one action on it stops at once.
    if (column >= kUncertain) return PrefixPredictions::Climb{0, 0, 1, false};
    auto& walk = site->climbs[column];
    if (walk.steps == PrefixPredictions::kUnread) {
      up.begin(0, 0, std::span(&from, 1), 0);
      up.stack.push_back(site->target);
      up.made.push_back(site->target);
      up.steps = 1;
      PackedAction action;
      const bool shifted = up.on(t, action) && up.shift(t, action);
      const bool under = !shifted && up.missing != 0 && up.missing <= 0xFFFF;
      walk = {under ? up.lhs : 0, static_cast<std::uint16_t>(under ? up.missing : 0), up.steps, shifted};
    }
    return walk;
  };
  // Per action, the stack its walk left and the frames below it.
  struct Reached {
    std::vector<std::uint32_t> stack;
    std::uint32_t below;
  };
  std::vector<std::uint32_t> key;
  std::vector<std::uint8_t> rowShifts;
  std::vector<PrefixPredictions::Under> rowUnder;
  // Whether a reader shifts each next token, the walks that go under its
  // stack apart: they go down one goto site at a time, read once.
  const auto row_of = [&](const Reached& r) -> const PrefixPredictions::Row& {
    key.assign(r.stack.begin(), r.stack.end());
    key.push_back(r.below);
    const auto [at, fresh] = out.cache.rows.try_emplace(key);
    if (!fresh) return at->second;
    rowUnder.clear();
    const PackedState& next = p.states[r.stack.back()];
    rowShifts.assign(cfg.terminals.size() + 1, 0);
    // Bracket depth after the predicted shifts belongs to the full dispatch.
    if (cfg.newline) rowShifts[cfg.newline] = 1;
    for (std::size_t i = 0; i < next.actions.size(); ++i) {
      const PackedAction& action = next.actions[i];
      const std::uint16_t t = action.terminal;
      // A reread, which stands last, is taken only where the token is not.
      if (action.kind == PackedAction::Kind::reread) continue;
      rowShifts[t] = 1;
      if (t == kEndOfInput || t == cfg.newline || out.cache.tied[next.mode].test(t) ||
          (i != 0 && next.actions[i - 1].terminal == t) ||
          (i + 1 != next.actions.size() && next.actions[i + 1].terminal == t &&
           next.actions[i + 1].kind != PackedAction::Kind::reread))
        continue;
      if (action.kind == PackedAction::Kind::shift) {
        if (action.gap == kAnyGap) rowShifts[t] = 2;
        continue;
      }
      if (action.kind != PackedAction::Kind::reduce) continue;
      const Production& rule = cfg.productions[action.value];
      if (rule.tested() || rule.start || rule.implied) continue;
      auto top = static_cast<std::ptrdiff_t>(r.stack.size()) - 1 - static_cast<std::ptrdiff_t>(rule.rhs.size());
      std::uint32_t lhs = rule.lhs, steps = 0;
      while (top >= 0) {
        const auto c = climb(walks(r.stack[top], lhs), r.stack[top], t);
        steps += c.steps;
        if (c.shifted && steps <= kWalkSteps) rowShifts[t] = 2;
        if (c.missing == 0 || steps > kWalkSteps) break;
        top -= c.missing;
        lhs = c.lhs;
      }
      if (top < 0)
        rowUnder.push_back({t, static_cast<std::uint8_t>(steps),
                            static_cast<std::uint32_t>(r.below - top), lhs});
    }
    const std::uint32_t deeper = rowUnder.empty() ? 0 : rowUnder.front().depth;
    std::ranges::stable_sort(rowUnder, {}, &PrefixPredictions::Under::lhs);
    at->second = {&*out.cache.shifts.insert(rowShifts).first,
                  &*out.cache.under.insert(rowUnder).first, deeper};
    return at->second;
  };
  std::vector<std::uint32_t> contexts{0}, chose;
  std::uint32_t depth = 0;
  // The states `needed` frames under the cell's state, each a context.
  const auto deepen = [&](std::uint32_t needed) {
    depth = needed;
    std::vector<std::uint32_t> sources{state}, previous;
    for (std::size_t i = 0; i < depth; ++i) {
      previous.clear();
      for (const std::uint32_t s : sources)
        previous.insert(previous.end(), out.cache.predecessors[s].begin(),
                        out.cache.predecessors[s].end());
      std::ranges::sort(previous);
      previous.erase(std::unique(previous.begin(), previous.end()), previous.end());
      sources.swap(previous);
    }
    contexts.insert(contexts.end(), sources.begin(), sources.end());
  };
  std::vector<Reached> reached;
  std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> arms;
  std::vector<std::size_t> byMode;
  const auto plan_of = [&] {
    byMode.resize(arms.size());
    std::iota(byMode.begin(), byMode.end(), std::size_t{0});
    std::ranges::sort(byMode, {}, [&](auto i) { return arms[i].first; });
    bool winner = false;
    for (std::size_t first = 0; first < byMode.size();) {
      auto end = first + 1;
      while (end < byMode.size() && arms[byMode[end]].first == arms[byMode[first]].first) ++end;
      // Arms sharing a reader see one column. A 1 blocks it, and two 2s tie.
      bool known = false;
      for (std::size_t t = 0; t < arms.front().second.size() && !(known && winner); ++t) {
        unsigned shifts = 0;
        for (auto i = first; i < end; ++i) {
          const auto value = arms[byMode[i]].second[t];
          if (value == 1) {
            shifts = kNone;
            break;
          }
          shifts += value == 2;
        }
        known |= shifts != kNone;
        winner |= shifts == 1;
      }
      if (!known) return kNone;
      first = end;
    }
    if (!winner) return kNone;
    std::vector<std::array<std::uint32_t, 3>> refs;
    for (std::size_t i = 0; i < arms.size(); ++i)
      refs.push_back({arms[i].first, out.shifts.try_emplace(arms[i].second, out.shifts.size()).first->second,
                      tied ? actions[i].terminal : kNoToken});
    return out.plans.try_emplace(std::move(refs), out.plans.size()).first->second;
  };
  // Two reductions can shift through the same readers over different stacks.
  // Follow their common tokens until the readers distinguish the stacks.
  if (!tied && actions.size() == 2 && actions[0].kind == PackedAction::Kind::reduce &&
      actions[1].kind == PackedAction::Kind::reduce) {
    std::vector<std::uint32_t> stack{state};
    while (stack.size() < kWalkSteps && !out.cache.predecessors[stack.back()].empty()) {
      const auto from = out.cache.predecessors[stack.back()].front();
      if (std::ranges::find(stack, from) != stack.end()) break;
      stack.push_back(from);
    }
    std::ranges::reverse(stack);
    struct Pair {
      std::array<std::vector<std::uint32_t>, 2> stacks;
      std::vector<std::array<std::uint16_t, 2>> prefix;
    };
    const auto advance = [&](Pair& pair, std::uint16_t t, std::span<const PackedAction> first) {
      for (std::size_t i = 0; i < pair.stacks.size(); ++i) {
        w.begin(0, 0, pair.stacks[i], 0);
        PackedAction action;
        if (!first.empty()) action = first[i];
        else if (!w.on(t, action) || action.kind != PackedAction::Kind::shift) return false;
        if (!w.shift(t, action) || !w.reader()) return false;
        pair.stacks[i] = w.stack;
      }
      return true;
    };
    Pair start{{stack, stack}, {}};
    if (advance(start, terminal, actions) && start.stacks[0].back() == start.stacks[1].back()) {
      std::vector<Pair> open{std::move(start)};
      std::set<std::uint32_t> seen;
      while (!open.empty()) {
        Pair pair = std::move(open.back());
        open.pop_back();
        const auto left = pair.stacks[0].back(), right = pair.stacks[1].back();
        const auto plain = [&](std::uint32_t reader) {
          const auto& s = p.states[reader];
          return !s.rereads() && s.passes.empty() && s.again == kNoMode &&
                 !out.cache.tied[s.mode].any();
        };
        if (!plain(left) || !plain(right)) continue;
        if (out.cache.readerOf[left] != out.cache.readerOf[right]) {
          arms.clear();
          for (const auto& path : pair.stacks)
            arms.emplace_back(p.states[path.back()].mode, *row_of({path, 0}).shifts);
          if (const auto plan = plan_of(); plan != kNone) {
            const auto context = out.stacks.try_emplace(std::move(stack), out.stacks.size()).first->second;
            PrefixPredictions::Path path{context, plan, std::move(pair.prefix)};
            out.cells[{cell, 0}].fallback = kPlanRoute |
                out.paths.try_emplace(std::move(path), out.paths.size()).first->second;
            return;
          }
        } else if (left == right && pair.stacks[0] != pair.stacks[1] && seen.insert(left).second &&
                   pair.prefix.size() < kWalkSteps) {
          for (const auto& a : p.states[left].actions) {
            if (a.terminal == kEndOfInput || a.terminal == cfg.newline || a.kind != PackedAction::Kind::shift) continue;
            Pair next = pair;
            if (!advance(next, a.terminal, {})) continue;
            next.prefix.push_back({p.states[left].mode, a.terminal});
            open.push_back(std::move(next));
          }
        }
      }
    }
  }
  for (std::size_t context = 0; context < contexts.size(); ++context) {
    chose.push_back(kNone);
    arms.clear();
    std::uint32_t needed = 0;
    if (rereads) {
      // The stack takes the token where one action's walk shifts it and the
      // others' are refused; none of them where every walk is refused.
      std::uint32_t taker = kNone;
      bool told = true;
      for (std::uint32_t a = 0; a + 1 < actions.size(); ++a) {
        w.begin(depth, contexts[context], std::span(&state, 1), 0);
        if (w.shift(terminal, actions[a])) {
          told = told && taker == kNone;
          taker = a;
        } else if (!w.refused) {
          told = false;
          if (needed == 0) needed = w.missing;
        }
      }
      if (told)
        chose.back() = kPlanArm |
                       (taker == kNone ? static_cast<std::uint32_t>(actions.size()) - 1 : taker);
      else if (depth == 0 && needed == 0)
        return;
      else if (depth == 0)
        deepen(needed);
      continue;
    }
    // The walks of the actions first, each to the state that reads the next
    // token: none may fail or meet another.
    if (++out.cache.generation == 0) {
      std::ranges::fill(out.cache.visited, 0);
      ++out.cache.generation;
    }
    reached.clear();
    for (const PackedAction& action : actions) {
      w.begin(depth, contexts[context], std::span(&state, 1), 0);
      const bool walked = w.shift(action.terminal, action) && w.reader();
      if (!walked || std::ranges::any_of(w.made, [&](std::uint32_t node) {
            return out.cache.visited[node] == out.cache.generation;
          })) {
        needed = w.missing;
        break;
      }
      for (const auto node : w.made) out.cache.visited[node] = out.cache.generation;
      reached.push_back({w.stack, w.below});
    }
    if (reached.size() != actions.size()) {
      // A walk that stops above the frames it knows stops in every context.
      if (depth != 0) continue;
      if (needed == 0) return;
    } else {
      // Readers alike in mode and in the terminals they act on tell nothing
      // apart by the next token, whatever stands below.
      const auto reader = out.cache.readerOf[reached.front().stack.back()];
      if (std::ranges::all_of(reached, [&](const Reached& r) {
            return out.cache.readerOf[r.stack.back()] == reader;
          })) {
        if (depth == 0) return;
        continue;
      }
      for (const Reached& r : reached) {
        const auto& [read, under, deeper] = row_of(r);
        auto& arm = arms.emplace_back(p.states[r.stack.back()].mode, *read).second;
        if (needed == 0) needed = deeper;
        // The walks under the stack go on from the context's frame.
        PrefixPredictions::Site* known = nullptr;
        std::uint32_t knownOf = kNone;
        for (const auto& u : *under) {
          if (u.depth != depth) continue;
          if (u.lhs != knownOf) known = walks(contexts[context], knownOf = u.lhs);
          const auto c = climb(known, contexts[context], u.terminal);
          if (c.shifted && u.steps + c.steps <= kWalkSteps) arm[u.terminal] = 2;
        }
      }
    }
    if (depth == 0 && needed != 0) {
      deepen(needed);
      continue;
    }
    chose.back() = plan_of();
  }
  // The plan most contexts take stands for all; the others are listed.
  const std::size_t first = depth == 0 ? 0 : 1;
  std::map<std::uint32_t, std::size_t> taken;
  for (std::size_t i = first; i < chose.size(); ++i) ++taken[chose[i]];
  if (taken.empty()) return;
  const auto fallback = std::ranges::max_element(taken, {}, [](const auto& t) { return t.second; })->first;
  if (fallback == kNone && taken.size() == 1) return;
  PrefixPredictions::Decision& decision = out.cells[{cell, depth}];
  decision.fallback = fallback;
  for (std::size_t i = first; i < chose.size(); ++i)
    if (chose[i] != fallback) decision.contexts.emplace(contexts[i], chose[i]);
}

// Each distinct row occupies one slice; callers index from its offset.
template <class T>
static std::vector<std::uint32_t> pack_rows(std::vector<T>& table, std::size_t width,
                                            std::size_t block = SIZE_MAX) {
  std::unordered_map<std::string_view, std::uint32_t> known;
  std::vector<T> packed;
  std::vector<std::uint32_t> offsets;
  for (std::size_t i = 0; i < table.size();) {
    const std::size_t n = std::min(block, width - i % width);
    const T* row = table.data() + i;
    std::string_view bytes(reinterpret_cast<const char*>(row), n * sizeof(T));
    const auto [where, fresh] = known.try_emplace(bytes, static_cast<std::uint32_t>(packed.size()));
    if (fresh) packed.insert(packed.end(), row, row + n);
    i += n;
    offsets.push_back(where->second);
  }
  table.swap(packed);
  return offsets;
}

std::vector<std::uint32_t> Packed::unitWords(std::vector<std::uint32_t>& number) const {
  constexpr auto width = 1u << Tables::unitBits;
  number.assign(hops.size(), 0);
  if (hops.size() <= 1) return {};
  std::vector<std::uint32_t> depth(hops.size());
  for (std::uint32_t i = 1; i < depth.size(); ++i) depth[i] = depth[hops[i].outer] + 1;
  for (const auto& state : states)
    for (const auto& action : state.actions) if (action.hops) number[action.hops] = kNone;
  for (const auto& sites : gotos)
    for (const auto& site : sites) if (site.hops) number[site.hops] = kNone;
  // Each round packs one block per full prefix or required end. The
  // next round indexes these blocks through the same outer prefixes.
  std::vector<std::uint32_t> units{0};
  auto level = hops;
  const auto deepest = std::ranges::max(depth);
  for (unsigned shift = 0;; shift += Tables::unitBits) {
    const bool last = ((deepest - 1) >> shift) < width;
    std::vector<Packed::Unit> next(last ? 0 : level.size());
    for (std::uint32_t i = 1; i < level.size(); ++i) {
      const auto count = (((depth[i] - 1) >> shift) % width) + 1;
      if (!level[i].production || (!number[i] && (last || count != width))) continue;
      if (last) {
        number[i] = static_cast<std::uint32_t>(units.size());
        units.push_back(depth[i]);
      }
      const auto at = units.size();
      if (at > kNone - count) throw Refused("the grammar takes too many unit reductions");
      units.resize(at + count);
      auto from = i;
      for (auto j = count; j-- > 0; from = level[from].outer)
        units[at + j] = level[from].production;
      if (!last) next[i] = {from, static_cast<std::uint32_t>(at)};
    }
    if (last) {
      units[0] = shift;
      break;
    }
    level.swap(next);
  }
  return units;
}

Packed pack(const GrammarImage& image, Cfg& cfg, LexDfa dfa, const std::vector<RivalRow>& takes,
            const LrAutomaton& automaton) {
  // A token is some text: the lexer's start ends none.
  if (dfa.nstates() > 1 && dfa.acceptSet[1] != 0)
    throw Refused(cfg.terminals[dfa.acceptSets[dfa.acceptSet[1]].front().term].name +
                  " reads no text");
  Packed out;
  terminal_facts(cfg, out);
  std::vector<bool> pattern(cfg.terminals.size());
  out.mixed.resize(cfg.terminals.size());
  for (const auto& ends : dfa.acceptSets)
    for (const LexAccept& a : ends) {
      if (a.literal) out.mixed[a.term] = true;
      else pattern[a.term] = true;
    }
  for (std::size_t t = 0; t < cfg.terminals.size(); ++t)
    if (out.mixed[t]) {
      if (!pattern[t]) out.terminalRank[t] = lexical_rank(cfg.terminals[t].trivia, true);
      out.mixed[t] = (out.terminalRank[t] & 1) != 0;
    }
  word_class(image, out);
  RunSets runs{out};
  // A run can jump to its last byte only if intermediate positions have no
  // guard to test, earlier token end to offer, or matcher exit to take.
  std::vector<bool> skippable;
  for (const auto& ends : dfa.acceptSets)
    skippable.push_back(std::ranges::all_of(ends, [](const LexAccept& a) {
      return a.guard == LexTest::none && a.back == 0;
    }));
  out.lexRun = runs_of(dfa, runs, [&](std::size_t s) { return skippable[dfa.acceptSet[s]]; });
  out.matchRun = runs_of(dfa.matcher, runs, [&](std::size_t s) { return dfa.matcher.accepts[s].empty(); });
  // The sets of terminals some text is read again with: the gaps of shifts
  // under scopes that pass less, then the alphabets of tests of a token.
  std::vector<Bits> sets;
  const std::vector<bool> reduced = reached_states(cfg, automaton, out, sets);
  const auto firstAlphabet = sets.size();
  for (Production& p : cfg.productions)
    for (Test& test : p.tests) {
      if (test.shadows.empty()) continue;
      Bits alphabet = trivia_of(cfg, std::uint64_t{1} << p.region);
      for (const std::uint16_t t : test.ahead)
        if (t != kEndOfInput) alphabet.set(t);
      sets.push_back(std::move(alphabet));
    }
  stamped_elements(cfg, record_closes(cfg, reduced, out), out);
  const auto rivals = guarded_rivals(cfg, dfa);
  dfa.predecessors.clear();
  Bits checked(cfg.terminals.size());
  for (const auto& [t, peers] : rivals) {
    checked.set(t);
    checked.merge(peers);
  }
  const auto firstSolo = sets.size();
  out.solo.assign(cfg.terminals.size(), kNoMode);
  checked.each([&](std::uint16_t t) { sets.emplace_back(cfg.terminals.size()).set(t); });
  std::vector<std::uint16_t> setModes;
  lexer_modes(cfg, dfa, takes, out, sets, setModes);
  for (PackedState& state : out.states)
    for (PackedAction& action : state.actions)
      if (action.gap < kNoGap) action.gap = setModes[action.gap];
  auto alphabet = setModes.begin() + firstAlphabet;
  for (Production& p : cfg.productions)
    for (Test& test : p.tests)
      if (!test.shadows.empty()) test.alphabet = *alphabet++;
  auto solo = setModes.begin() + firstSolo;
  checked.each([&](std::uint16_t t) { out.solo[t] = *solo++; });
  mode_wins(cfg, dfa, rivals, out);
  check_section_ends(cfg, dfa, out);
  section_closes(cfg, out);
  out.paths = std::move(dfa.paths);
  first_bytes(cfg, dfa, runs, out);
  dropped_bytes(cfg, dfa, out);
  terminal_sets(cfg, dfa, out);
  out.matcher = std::move(dfa.matcher);
  out.matchNext = out.matcher.next;
  out.matchBlocks = pack_rows(out.matchNext, out.matcher.nclasses, LexBlocks::kWidth);
  out.matchOf = pack_rows(out.matchBlocks, LexBlocks::count(out.matcher.nclasses));
  out.monitors = std::move(dfa.monitors);
  out.waits = std::move(dfa.waits);
  // The automaton itself, as the runtime walks it: states that go alike
  // share their row.
  out.byteClass = dfa.classOf;
  out.lexClassCount = static_cast<std::uint16_t>(dfa.nclasses);
  out.lexNext = std::move(dfa.next);
  out.nextBlocks = pack_rows(out.lexNext, out.lexClassCount, LexBlocks::kWidth);
  out.nextOf = pack_rows(out.nextBlocks, LexBlocks::count(out.lexClassCount));
  out.winBlocks = pack_rows(out.lexWin, out.winRow, LexBlocks::kWidth);
  out.winOf = pack_rows(out.winBlocks, LexBlocks::count(out.winRow));
  out.onwardOf = pack_rows(out.lexOnward, out.onwardRow);
  out.lexAccept = std::move(dfa.acceptSet);
  return out;
}

}  // namespace cursiv::engine::gen
