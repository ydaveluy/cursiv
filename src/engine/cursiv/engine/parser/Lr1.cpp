#include "cursiv/engine/parser/Lr1.hpp"
#include "cursiv/engine/grammar/StatePartition.hpp"
#include "cursiv/engine/grammar/StrongComponents.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>

namespace cursiv::engine::gen {

namespace {

// An item is a production and a dot, numbered in one sequence.
struct Items {
  explicit Items(const Cfg& cfg, std::vector<bool> blanks = {}) : cfg(cfg), of(cfg.productionsOf) {
    nterms = cfg.terminals.size();
    const std::size_t nnts = cfg.nonterminals;
    base.reserve(cfg.productions.size());
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
      base.push_back(static_cast<std::uint32_t>(prod.size()));
      for (std::size_t dot = 0; dot <= cfg.productions[p].rhs.size(); ++dot) {
        prod.push_back(p);
        this->dot.push_back(static_cast<std::uint32_t>(dot));
      }
    }

    // What each nonterminal may begin with, whether it may read nothing, and
    // whether it then lets any token follow: a production that reads nothing
    // and holds only before tokens it names begins with them instead.
    nullable = blanks.empty() ? cfg.nullable() : std::move(blanks);
    open.assign(nnts, false);
    first.assign(nnts, Bits(nterms));
    // A production is read again only when a nonterminal it may begin with
    // changed.
    std::vector<std::vector<std::uint32_t>> leads(nnts);
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p)
      for (const Symbol s : cfg.productions[p].rhs)
        if (!is_terminal(s)) leads[index_of(s)].push_back(p);
    std::vector<std::uint32_t> work(cfg.productions.size());
    std::vector<bool> queued(cfg.productions.size(), true);
    for (std::uint32_t p = 0; p < work.size(); ++p) work[p] = p;
    while (!work.empty()) {
      const std::uint32_t at = work.back();
      work.pop_back();
      queued[at] = false;
      const Production& p = cfg.productions[at];
      bool grew = false;
      bool allOpen = true;
      const auto begins = [&](std::uint16_t t) {
        if (first[p.lhs].test(t)) return;
        first[p.lhs].set(t);
        grew = true;
      };
      for (const Symbol s : p.rhs) {
        if (is_terminal(s)) {
          if (allOpen) begins(static_cast<std::uint16_t>(s));
          allOpen = false;
          break;
        }
        if (allOpen && index_of(s) != p.lhs && first[p.lhs].merge(first[index_of(s)]))
          grew = true;
        allOpen = allOpen && open[index_of(s)];
        if (!nullable[index_of(s)]) {
          allOpen = false;
          break;
        }
      }
      const bool names = !p.tests.empty() && std::ranges::all_of(p.tests, [](const Test& test) {
        return !test.unless && !test.ahead.empty();
      });
      if (allOpen && names) {
        for (const Test& test : p.tests)
          for (const std::uint16_t t : test.ahead) begins(t);
      } else if (allOpen && !open[p.lhs]) {
        open[p.lhs] = true;
        grew = true;
      }
      if (!grew) continue;
      for (const std::uint32_t user : leads[p.lhs])
        if (!queued[user]) {
          queued[user] = true;
          work.push_back(user);
        }
    }
    // Past the symbol at each dot: what may come first, and whether nothing
    // may.
    after.assign(prod.size(), Bits(nterms));
    afterNullable.assign(prod.size(), true);
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
      const std::vector<Symbol>& rhs = cfg.productions[p].rhs;
      for (std::size_t dot = rhs.size(); dot-- > 0;) {
        const std::uint32_t item = base[p] + static_cast<std::uint32_t>(dot);
        if (dot + 1 == rhs.size()) continue;
        const Symbol next = rhs[dot + 1];
        if (is_terminal(next)) {
          after[item].set(next);
          afterNullable[item] = false;
        } else {
          after[item] = first[index_of(next)];
          afterNullable[item] = false;
          if (open[index_of(next)]) {
            after[item].merge(after[item + 1]);
            afterNullable[item] = afterNullable[item + 1];
          }
        }
      }
    }
    // Per item, the terminals the bars at its dot and past it refuse.
    barredOf.assign(prod.size(), 0);
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
      const Production& q = cfg.productions[p];
      if (q.bars.empty()) continue;
      Bits refused(nterms);
      std::uint32_t set = 0;
      for (std::size_t b = q.bars.size(); b-- > 0;) {
        const auto& [dot, terms] = q.bars[b];
        if (std::ranges::any_of(terms, [&](auto t) { return !refused.test(t); })) {
          for (const auto t : terms) refused.set(t);
          barred.push_back(refused);
          set = static_cast<std::uint32_t>(barred.size());
        }
        const auto first = barredOf.begin() + base[p];
        std::fill(first + (b == 0 ? 0 : q.bars[b - 1].first + 1), first + dot + 1, set);
      }
    }
    // The nonterminals that read nothing whatever they derive: one that reads
    // something has the productions that use it read again.
    blank.assign(nullable.begin(), nullable.end());
    for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) work.push_back(p);
    while (!work.empty()) {
      const Production& q = cfg.productions[work.back()];
      work.pop_back();
      if (!blank[q.lhs] || std::ranges::none_of(q.rhs, [&](Symbol s) {
            return is_terminal(s) || !blank[index_of(s)];
          }))
        continue;
      blank[q.lhs] = false;
      work.insert(work.end(), leads[q.lhs].begin(), leads[q.lhs].end());
    }
    // The trivia scopes a token is read under, a bit a scope: per item those
    // of the symbol at its dot, per nonterminal those of the token after it.
    // A production's first token is read as its caller's token there; the
    // others under the production's own scope.
    under.assign(prod.size(), 1);
    follows.assign(nnts, 1);
    begins.assign(nnts, 1);
    if (cfg.regions.size() > 1) {
      if (cfg.regions.size() > 64) throw Refused("the grammar has too many trivia scopes");
      begins.assign(nnts, 0);
      follows.assign(nnts, 0);
      begins[0] = follows[0] = 1;
      for (const std::uint32_t condition : cfg.conditions) {
        const Production& reads = cfg.productions[condition];
        begins[reads.lhs] = follows[reads.lhs] = std::uint64_t{1} << reads.region;
      }
      // What scopes the symbol at `at` of `q` is read under: `lead` where
      // the symbols before it need read nothing, `inner` where one may.
      const auto here = [&](const Production& q, std::size_t at, bool lead, bool inner) {
        return (inner ? std::uint64_t{1} << q.scope_at(at) : 0) | (lead ? begins[q.lhs] : 0);
      };
      const auto reads = [&](Symbol s) { return is_terminal(s) || !blank[index_of(s)]; };
      // A production is read again where the scopes of its nonterminal grew.
      for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) work.push_back(p);
      queued.assign(cfg.productions.size(), true);
      while (!work.empty()) {
        const std::uint32_t p = work.back();
        work.pop_back();
        queued[p] = false;
        const Production& q = cfg.productions[p];
        bool lead = true, inner = false;
        for (std::size_t at = 0; at < q.rhs.size(); ++at) {
          const Symbol s = q.rhs[at];
          if (is_terminal(s)) {
            lead = false;
            inner = true;
            continue;
          }
          const std::uint32_t nt = index_of(s);
          const std::uint64_t in = begins[nt] | here(q, at, lead, inner);
          const std::uint64_t then =
              follows[nt] | (at + 1 < q.rhs.size() ? std::uint64_t{1} << q.scope_at(at + 1) : 0) |
              (afterNullable[base[p] + at] ? behind(p) : 0);
          if (in != begins[nt] || then != follows[nt])
            for (const std::uint32_t user : of[nt])
              if (!queued[user]) {
                queued[user] = true;
                work.push_back(user);
              }
          begins[nt] = in;
          follows[nt] = then;
          lead = lead && nullable[nt];
          inner = inner || reads(s);
        }
      }
      blankBefore.assign(prod.size(), false);
      for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
        const Production& q = cfg.productions[p];
        bool lead = true, inner = false;
        for (std::size_t at = 0; at <= q.rhs.size(); ++at) {
          blankBefore[base[p] + at] = !inner;
          if (at == q.rhs.size()) break;
          under[base[p] + at] = here(q, at, lead, inner);
          lead = lead && !is_terminal(q.rhs[at]) && nullable[index_of(q.rhs[at])];
          inner = inner || reads(q.rhs[at]);
        }
      }
    }
  }

  // Whether a test of production `p` names the end of its scope's text.
  [[nodiscard]] bool ends(std::uint32_t p) const {
    const Production& q = cfg.productions[p];
    return std::ranges::any_of(q.tests, [&](const Test& test) {
      return std::ranges::find(test.ahead, cfg.regions[q.region].end) != test.ahead.end();
    });
  }
  // The scopes the token after production `p` is read under: its own where
  // it tests its scope's end, which stands behind a gap of the scope; those
  // after its nonterminal where another token may come.
  [[nodiscard]] std::uint64_t behind(std::uint32_t p) const {
    const Production& q = cfg.productions[p];
    const std::uint16_t end = cfg.regions[q.region].end;
    const bool other = q.tests.empty() || std::ranges::any_of(q.tests, [&](const Test& test) {
      return test.unless || test.ahead.size() != 1 || test.ahead.front() != end;
    });
    return (ends(p) ? std::uint64_t{1} << q.region : 0) | (other ? follows[q.lhs] : 0);
  }

  [[nodiscard]] bool complete(std::uint32_t item) const {
    return dot[item] == cfg.productions[prod[item]].rhs.size();
  }
  [[nodiscard]] Symbol symbol(std::uint32_t item) const {
    return cfg.productions[prod[item]].rhs[dot[item]];
  }

  const Cfg& cfg;
  std::size_t nterms = 0;
  std::vector<std::uint32_t> base, prod, dot;
  const std::vector<std::vector<std::uint32_t>>& of;
  // Per nonterminal, the terminals it may begin with.
  std::vector<Bits> first;
  std::vector<Bits> after;
  std::vector<bool> afterNullable;
  // Per item, one more than its place in `barred`, 0 for none.
  std::vector<std::uint32_t> barredOf;
  std::vector<Bits> barred;
  // Per nonterminal, whether it may read nothing, and whether any token may
  // then follow it.
  std::vector<bool> nullable;
  std::vector<bool> open;
  // Per nonterminal, whether it reads nothing whatever it derives.
  std::vector<bool> blank;
  std::vector<std::uint64_t> under;
  std::vector<std::uint64_t> follows;
  // Per nonterminal, the scopes its first token is read under; per item,
  // whether the symbols before its dot read nothing, where scopes are told.
  std::vector<std::uint64_t> begins;
  std::vector<bool> blankBefore;
  [[nodiscard]] bool blank_before(std::uint32_t item) const {
    return !blankBefore.empty() && blankBefore[item];
  }
};

struct Position {
  std::uint32_t item;
  std::uint64_t lead;
};

struct Start {
  Position position;
  Bits lookahead;
};

// A key of words, hashed whole.
struct WordsHash {
  std::size_t operator()(const std::vector<std::uint64_t>& words) const {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint64_t w : words) h = (h ^ w) * 1099511628211ull;
    return static_cast<std::size_t>(h);
  }
};

// A token that may read the text of a trivia ends what it may follow
// anywhere: where a state acts on it otherwise, such a production is also
// reduced on it, and the state reached reads its text again.
void pass_trivia(const Items& it, std::vector<LrState>& states) {
  const Cfg& cfg = it.cfg;
  if (std::ranges::all_of(cfg.regions, [](const Region& r) { return r.shadows.empty(); }))
    return;
  // Per production, the terminals some state reduces it on.
  std::vector<Bits> ends(cfg.productions.size(), Bits(it.nterms));
  for (const LrState& state : states)
    for (const auto& [prod, on] : state.reduces) ends[prod].merge(on);
  std::unordered_map<std::uint64_t, Bits> shadows;
  Bits acts(it.nterms);
  for (LrState& state : states) {
    if (state.reduces.empty()) continue;
    acts.clear();
    for (const auto& [terminal, to] : state.shifts) acts.set(terminal);
    for (const auto& [prod, on] : state.reduces) acts.merge(on);
    for (const auto& [prod, then] : state.behind) {
      if (cfg.productions[prod].tested()) continue;
      const auto [known, fresh] = shadows.try_emplace(then, it.nterms);
      if (fresh) known->second = trivia_shadows(cfg, then);
      Bits add = known->second;
      add &= acts;
      add &= ends[prod];
      std::ranges::lower_bound(state.reduces, prod, {}, [](const auto& r) { return r.first; })
          ->second.merge(add);
    }
  }
}

// The nonterminals a kernel opens, their lookaheads, and their trivia scopes.
class Closure {
 public:
  explicit Closure(const Items& it)
      : _it(it),
        _opened(it.cfg.nonterminals),
        _here(it.barred.empty() ? 0 : it.cfg.nonterminals),
        _leads(it.cfg.nonterminals, 0),
        _isOpen(it.cfg.nonterminals, false),
        _queued(it.cfg.nonterminals, false) {}

  // Visits each kernel item and the items it opens, with their lookahead,
  // the scopes at the dot and those of the production's first token.
  template <class Take>
  void close(std::span<const Position> kernel, std::span<const Bits> la, Take&& take) {
    const auto under = [&](std::size_t k) {
      return _it.blank_before(kernel[k].item) ? kernel[k].lead : _it.under[kernel[k].item];
    };
    for (std::size_t k = 0; k < kernel.size(); ++k)
      offer(kernel[k].item, la[k], la[k], under(k));
    while (!_work.empty()) {
      const std::uint32_t nt = _work.back();
      _work.pop_back();
      _queued[nt] = false;
      for (const std::uint32_t p : _it.of[nt])
        offer(_it.base[p], _opened[nt], _here.empty() ? _opened[nt] : _here[nt], _leads[nt]);
    }
    for (std::size_t k = 0; k < kernel.size(); ++k) {
      const std::uint32_t item = kernel[k].item;
      take(item, _it.complete(item) ? barred(la[k], item) : la[k], under(k),
           kernel[k].lead);
    }
    for (const std::uint32_t nt : _open) {
      for (const std::uint32_t p : _it.of[nt]) {
        const std::uint32_t item = _it.base[p];
        take(item,
             _it.complete(item) && !_here.empty() ? barred(_here[nt], item) : _opened[nt],
             _leads[nt], _leads[nt]);
      }
      _isOpen[nt] = false;
    }
    _open.clear();
  }

 private:
  // Opens the nonterminal at the dot of `item` with what may follow it:
  // what the item reads next, then `la` where that may be nothing; `here`
  // stands for `la` where the nonterminal reads nothing.
  void offer(std::uint32_t item, const Bits& la, const Bits& here, std::uint64_t scopes) {
    if (_it.complete(item) || is_terminal(_it.symbol(item))) return;
    const std::uint32_t nt = index_of(_it.symbol(item));
    bool grew = !_isOpen[nt];
    if (grew) {
      _isOpen[nt] = true;
      _open.push_back(nt);
      const auto words = la.words().size();
      _opened[nt].clear(words);
      if (!_here.empty()) _here[nt].clear(words);
      _leads[nt] = 0;
    }
    grew |= _opened[nt].merge(_it.after[item]);
    if (_it.afterNullable[item]) grew |= _opened[nt].merge(la);
    if (!_here.empty()) {
      grew |= _here[nt].merge(_it.after[item]);
      if (_it.afterNullable[item]) grew |= _here[nt].merge(barred(here, item));
    }
    grew |= (_leads[nt] | scopes) != _leads[nt];
    _leads[nt] |= scopes;
    if (grew && !_queued[nt]) {
      _queued[nt] = true;
      _work.push_back(nt);
    }
  }
  // `la` less what the bars at the dot of `item` and past it refuse.
  const Bits& barred(const Bits& la, std::uint32_t item) {
    const std::uint32_t at = _it.barredOf[item];
    if (at == 0) return la;
    _cut = la;
    _cut.remove(_it.barred[at - 1]);
    return _cut;
  }

  const Items& _it;
  std::vector<Bits> _opened, _here;
  Bits _cut;
  std::vector<std::uint64_t> _leads;
  std::vector<bool> _isOpen, _queued;
  std::vector<std::uint32_t> _open, _work;
};

// The kernels the states start from: the grammar's, then one per condition,
// in the conditions' order. What a condition reads is read whatever token
// follows it; a text its scope passes is no token there.
std::vector<Start> start_kernels(const Items& it) {
  const Cfg& cfg = it.cfg;
  std::vector<Start> starts;
  const auto add = [&](std::uint32_t p) -> Bits& {
    const auto item = it.base[p];
    const auto scopes = it.blank_before(item) ? it.begins[cfg.productions[p].lhs] : 0;
    return starts.emplace_back(Position{item, scopes}, Bits(it.nterms)).lookahead;
  };
  add(0).set(0);
  const auto root = [&](std::size_t t) {
    return cfg.terminals[t].twin != 0 ? std::size_t{cfg.terminals[t].twin} : t;
  };
  Bits passed(it.nterms);
  for (const std::uint32_t condition : cfg.conditions) {
    const Region& scope = cfg.regions[cfg.productions[condition].region];
    passed.clear();
    for (const auto* list : {&scope.ignored, &scope.hidden})
      for (const std::uint16_t t : *list) passed.set(root(t));
    Bits& reads = add(condition);
    for (std::size_t t = 0; t < it.nterms; ++t)
      if (cfg.terminals[t].trivia == Trivia::none && !passed.test(root(t)))
        reads.set(t);
  }
  return starts;
}

// What flows into a lookahead: terminals of its own, and the lookaheads of
// kernel items of its state, by their places.
struct Flow {
  Bits terminals;
  std::vector<std::uint32_t> from;
};

// A state as its lookaheads leave it: its kernel, the cores it moves to, and
// what flows into the lookahead of each item of the kernels it moves to and
// of each production it reduces.
struct Core {
  std::vector<Position> kernel;
  std::vector<std::pair<Symbol, std::uint32_t>> edges;
  std::vector<std::vector<Flow>> moves;
  std::vector<std::pair<std::uint32_t, Flow>> reduces;
};

// The cores, from the start kernels on, each closed once with a mark in place
// of the lookahead of each of its kernel items, numbered past the terminals.
// `startCores` names the core of each start.
std::vector<Core> cores_of(const Items& it, const std::vector<Start>& starts,
                           std::vector<std::uint32_t>& startCores, std::uint32_t maxStates) {
  const auto flow = [&](const Bits& la) {
    Flow f{Bits(it.nterms), {}};
    la.each([&](std::size_t b) {
      if (b < it.nterms)
        f.terminals.set(b);
      else
        f.from.push_back(static_cast<std::uint32_t>(b - it.nterms));
    });
    return f;
  };
  std::vector<Core> cores;
  // Each kernel item and its possible opening scopes identify the core.
  std::unordered_map<std::vector<std::uint64_t>, std::uint32_t, WordsHash> ids;
  const auto intern = [&](std::vector<Position> items) {
    std::vector<std::uint64_t> key;
    key.reserve(2 * items.size());
    for (const auto& pos : items) {
      key.push_back(pos.item);
      key.push_back(pos.lead);
    }
    const auto [at, fresh] = ids.try_emplace(std::move(key), static_cast<std::uint32_t>(cores.size()));
    if (fresh) {
      if (maxStates && cores.size() == maxStates)
        throw Refused("the grammar has more LR states than LR_MAX_STATES allows (" +
                      std::to_string(maxStates) + ")");
      cores.emplace_back().kernel = std::move(items);
    }
    return at->second;
  };
  for (const Start& start : starts) startCores.push_back(intern({start.position}));
  struct Move {
    Position position;
    Flow flow;
  };
  Closure closure(it);
  std::vector<Bits> marked;
  for (std::uint32_t c = 0; c < cores.size(); ++c) {
    const auto& kernel = cores[c].kernel;
    marked.assign(kernel.size(), Bits(it.nterms + kernel.size()));
    for (std::size_t k = 0; k < marked.size(); ++k) marked[k].set(it.nterms + k);
    std::map<Symbol, std::vector<Move>> moves;
    std::vector<std::pair<std::uint32_t, Flow>> reduces;
    closure.close(kernel, marked, [&](std::uint32_t item, const Bits& la, std::uint64_t, std::uint64_t lead) {
      if (it.complete(item))
        reduces.emplace_back(it.prod[item], flow(la));
      else
        moves[it.symbol(item)].push_back({{item + 1, it.blank_before(item + 1) ? lead : 0}, flow(la)});
    });
    for (auto& [symbol, way] : moves) {
      std::ranges::sort(way, {}, [](const Move& m) { return m.position.item; });
      std::vector<Position> positions;
      std::vector<Flow> flows;
      positions.reserve(way.size());
      flows.reserve(way.size());
      for (auto& m : way) {
        positions.push_back(m.position);
        flows.push_back(std::move(m.flow));
      }
      const auto target = intern(std::move(positions));
      cores[c].edges.emplace_back(symbol, target);
      cores[c].moves.push_back(std::move(flows));
    }
    std::ranges::sort(reduces, {}, [](const auto& r) { return r.first; });
    cores[c].reduces = std::move(reduces);
  }
  return cores;
}

// The states: per state its core, and per edge of its core the state it
// moves to.
struct Graph {
  std::vector<std::uint32_t> coreOf;
  std::vector<std::vector<std::uint32_t>> to;
};

// The kernel items of a graph's states as nodes, numbered state by state.
struct Nodes {
  Nodes(const std::vector<Core>& cores, const Graph& g) : first(g.coreOf.size() + 1, 0) {
    for (std::size_t s = 0; s < g.coreOf.size(); ++s)
      first[s + 1] = first[s] + static_cast<std::uint32_t>(cores[g.coreOf[s]].kernel.size());
  }
  [[nodiscard]] std::size_t size() const { return first.back(); }
  // Per state, its first node; the last entry is the count.
  std::vector<std::uint32_t> first;
};

// Edges between nodes, by source: what a node's set gains crosses each of its
// edges to the target's set, less the edge's filter where it has one.
struct Relation {
  explicit Relation(std::size_t nodes) : first(nodes + 1, 0) {}
  // Per node, its first edge; the last entry is the count.
  std::vector<std::uint32_t> first;
  std::vector<std::uint32_t> to;
  std::vector<const Bits*> filter;

  // Built from `each(add)`, called twice: `add(from, to, filter)` counts the
  // edges, then places them.
  template <class Each>
  void build(Each&& each) {
    each([&](std::uint32_t from, std::uint32_t, const Bits*) { ++first[from + 1]; });
    for (std::size_t x = 1; x < first.size(); ++x) first[x] += first[x - 1];
    to.resize(first.back());
    filter.resize(first.back());
    std::vector<std::uint32_t> at(first.begin(), first.end() - 1);
    each([&](std::uint32_t from, std::uint32_t target, const Bits* f) {
      to[at[from]] = target;
      filter[at[from]++] = f;
    });
  }
};

// Spreads the sets along `r` to a fixpoint: each number a node gains crosses
// each of its edges once.
void spread(const Relation& r, std::vector<Bits>& sets) {
  std::vector<Bits> gained = sets;
  std::vector<std::uint32_t> work;
  std::vector<bool> queued(sets.size(), false);
  for (std::uint32_t x = 0; x < sets.size(); ++x)
    if (gained[x].any()) {
      queued[x] = true;
      work.push_back(x);
    }
  Bits delta, add;
  while (!work.empty()) {
    const std::uint32_t x = work.back();
    work.pop_back();
    queued[x] = false;
    delta = gained[x];
    gained[x].clear();
    for (std::uint32_t e = r.first[x]; e < r.first[x + 1]; ++e) {
      const std::uint32_t y = r.to[e];
      add = delta;
      if (r.filter[e] != nullptr) add.remove(*r.filter[e]);
      add.remove(sets[y]);
      if (!add.any()) continue;
      sets[y].merge(add);
      gained[y].merge(add);
      if (!queued[y]) {
        queued[y] = true;
        work.push_back(y);
      }
    }
  }
}

// The lookaheads of the kernel items of `g`, a set per node: what flows into
// each, then what spreads along the moves, those of `starts` given.
std::vector<Bits> lookaheads(const Items& it, const std::vector<Core>& cores, const Graph& g,
                             const Nodes& nodes, const std::vector<Start>& starts,
                             const std::vector<std::uint32_t>& startStates) {
  std::vector<Bits> la(nodes.size(), Bits(it.nterms));
  for (std::size_t i = 0; i < starts.size(); ++i)
    la[nodes.first[startStates[i]]].merge(starts[i].lookahead);
  Relation r(nodes.size());
  r.build([&](auto&& add) {
    for (std::uint32_t s = 0; s < g.coreOf.size(); ++s) {
      const Core& core = cores[g.coreOf[s]];
      for (std::size_t e = 0; e < core.edges.size(); ++e)
        for (std::size_t k = 0; k < core.moves[e].size(); ++k)
          for (const std::uint32_t from : core.moves[e][k].from)
            add(nodes.first[s] + from, nodes.first[g.to[s][e]] + static_cast<std::uint32_t>(k),
                nullptr);
    }
  });
  for (std::uint32_t s = 0; s < g.coreOf.size(); ++s) {
    const Core& core = cores[g.coreOf[s]];
    for (std::size_t e = 0; e < core.edges.size(); ++e)
      for (std::size_t k = 0; k < core.moves[e].size(); ++k)
        la[nodes.first[g.to[s][e]] + k].merge(core.moves[e][k].terminals);
  }
  spread(r, la);
  return la;
}

// Per core that reads a token, not `alone`: in `cells`, the terminals whose
// presence in a state's lookahead changes what it does; in `rivalled`, those
// it reduces on only as rivals of a terminal it reads. Returns per kernel
// item node the terminals of its core's cells its lookahead reduces on.
std::vector<Bits> cells_of(const Items& it, const std::vector<Core>& cores, const Nodes& nodes,
                           const std::vector<Bits>& la, const std::vector<RivalRow>& takes,
                           const Bits& always, const std::vector<bool>& alone,
                           std::vector<Bits>& cells, std::vector<Bits>& rivalled) {
  const std::size_t n = it.nterms;
  std::vector<Bits> seeds(nodes.size(), Bits(n));
  Bits shifted(n), once(n), twice(n), read(n), own(n), rivals(n), told(n), mark(n);
  std::vector<Bits> on;
  for (std::uint32_t c = 0; c < cores.size(); ++c) {
    const Core& core = cores[c];
    if (core.reduces.empty()) continue;
    shifted.clear();
    for (const auto& [symbol, to] : core.edges)
      if (is_terminal(symbol)) shifted.set(symbol);
    once = shifted;
    twice.clear();
    read = shifted;
    read.merge(always);
    own.clear();
    on.assign(core.reduces.size(), Bits(n));
    for (std::size_t r = 0; r < core.reduces.size(); ++r) {
      const Flow& f = core.reduces[r].second;
      on[r] = f.terminals;
      for (const std::uint32_t from : f.from) on[r].merge(la[nodes.first[c] + from]);
      told = on[r];
      told &= once;
      twice.merge(told);
      once.merge(on[r]);
      read.merge(on[r]);
      own.merge(f.terminals);
    }
    rivals.clear();
    read.each([&](std::size_t t) { rivals.merge(takes[t]); });
    told = once;
    told &= rivals;
    told.remove(shifted);
    told.remove(own);
    rivalled[c] = told;
    if (alone[c]) continue;
    told.merge(twice);
    if (!told.any()) continue;
    cells[c] = told;
    for (std::size_t r = 0; r < core.reduces.size(); ++r) {
      const Flow& f = core.reduces[r].second;
      mark = on[r];
      mark &= told;
      mark.remove(f.terminals);
      for (const std::uint32_t from : f.from) seeds[nodes.first[c] + from].merge(mark);
    }
  }
  return seeds;
}

// Relevance flows against the moves, less what a move brings of its own.
Relation dependencies(const std::vector<Core>& cores, const Graph& perCore,
                       const Nodes& nodes) {
  Relation r(nodes.size());
  r.build([&](auto&& add) {
    for (std::uint32_t c = 0; c < cores.size(); ++c) {
      const Core& core = cores[c];
      for (std::size_t e = 0; e < core.edges.size(); ++e)
        for (std::size_t k = 0; k < core.moves[e].size(); ++k) {
          const Flow& f = core.moves[e][k];
          for (const std::uint32_t from : f.from)
            add(nodes.first[perCore.to[c][e]] + static_cast<std::uint32_t>(k),
                nodes.first[c] + from, &f.terminals);
        }
    }
  });
  return r;
}

// The states, from the starts on: one per core and part of its kernel items'
// lookaheads that `rel` keeps. `limit`, zero for no limit, bounds construction.
std::optional<Graph> split(const std::vector<Core>& cores, const Nodes& perCore, const std::vector<Bits>& rel,
            const std::vector<Start>& starts, const std::vector<std::uint32_t>& startCores,
            std::vector<std::uint32_t>& startStates, std::uint32_t limit) {
  Graph g;
  // A state's key: its core, then the words of its kernel items' parts.
  std::unordered_map<std::vector<std::uint64_t>, std::uint32_t, WordsHash> ids;
  std::vector<std::vector<Bits>> parts;
  const auto intern = [&](std::uint32_t c, std::vector<Bits>&& part) {
    std::vector<std::uint64_t> key{c};
    for (std::size_t k = 0; k < part.size(); ++k) {
      part[k] &= rel[perCore.first[c] + k];
      key.insert(key.end(), part[k].words().begin(), part[k].words().end());
    }
    const auto [at, fresh] = ids.try_emplace(std::move(key), static_cast<std::uint32_t>(parts.size()));
    if (fresh) {
      if (limit != 0 && parts.size() == limit) return kNone;
      g.coreOf.push_back(c);
      parts.push_back(std::move(part));
    }
    return at->second;
  };
  startStates.clear();
  for (std::size_t i = 0; i < starts.size(); ++i) {
    const auto start = intern(startCores[i], {starts[i].lookahead});
    if (start == kNone) return std::nullopt;
    startStates.push_back(start);
  }
  for (std::uint32_t s = 0; s < parts.size(); ++s) {
    const Core& core = cores[g.coreOf[s]];
    std::vector<std::uint32_t> to;
    for (std::size_t e = 0; e < core.edges.size(); ++e) {
      std::vector<Bits> part;
      for (const Flow& f : core.moves[e]) {
        Bits& la = part.emplace_back(f.terminals);
        for (const std::uint32_t from : f.from) la.merge(parts[s][from]);
      }
      const auto target = intern(core.edges[e].second, std::move(part));
      if (target == kNone) return std::nullopt;
      to.push_back(target);
    }
    g.to.push_back(std::move(to));
  }
  return g;
}

// What a state does, its kernel closed with its lookaheads: what it reduces
// and on what, the terminals its tests name, and the scopes its tokens are
// read under.
LrState facts(const Items& it, Closure& closure, std::span<const Position> kernel,
              std::span<const Bits> lookahead) {
  const Cfg& cfg = it.cfg;
  LrState state;
  state.scopes = 0;
  std::map<std::uint16_t, std::uint64_t> governs;
  closure.close(kernel, lookahead, [&](std::uint32_t item, const Bits& la, std::uint64_t at, std::uint64_t) {
    // The token that comes where a bar stands is read as one it refuses.
    const auto& bars = cfg.productions[it.prod[item]].bars;
    if (const auto bar = std::ranges::lower_bound(bars, it.dot[item], {}, [](const auto& b) {
          return b.first;
        });
        bar != bars.end() && bar->first == it.dot[item]) {
      state.barred = true;
      for (const std::uint16_t t : bar->second)
        if (t != kEndOfInput) state.named.push_back(t);
    }
    if (!it.complete(item)) {
      if (is_terminal(it.symbol(item))) {
        state.scopes |= at;
        governs[static_cast<std::uint16_t>(it.symbol(item))] |= at;
      }
      return;
    }
    // Behind symbols that read nothing at a scope's head, the token that
    // comes is read where the head's first one is: in the caller's gap.
    // An end written first in its scope stands behind its own.
    const Production& p = cfg.productions[it.prod[item]];
    const std::uint64_t then =
        it.blank_before(item) && (at & ~(std::uint64_t{1} << p.region)) != 0 &&
                !(p.rhs.empty() && it.ends(it.prod[item]))
            ? at
            : it.behind(it.prod[item]);
    state.scopes |= then;
    state.behind.emplace_back(it.prod[item], then);
    // A production that tests the token that comes is reduced on those the
    // test lets through: the tokens it names, which the state reached reads
    // again as its own, or all that may follow but those. The state reads
    // what follows and what the test names: the token that comes is the one
    // the parse reads there.
    if (p.tests.empty()) {
      state.reduces.emplace_back(it.prod[item], la);
      return;
    }
    Bits on(it.nterms);
    Bits through(it.nterms);
    for (const Test& test : p.tests) {
      through.clear();
      if (test.unless || test.ahead.empty()) through = la;
      if (test.condition != 0)
        it.first[cfg.productions[cfg.conditions[test.condition - 1]].lhs].each(
            [&](std::size_t t) { state.named.push_back(static_cast<std::uint16_t>(t)); });
      else if (!test.unless && std::ranges::any_of(test.ahead, [&](std::uint16_t t) {
                 return t != kEndOfInput && t != cfg.regions[p.region].end;
               }))
        la.each([&](std::size_t t) {
          if (t != kEndOfInput) state.follow.push_back(static_cast<std::uint16_t>(t));
        });
      for (const std::uint16_t t : test.ahead) {
        if (test.unless)
          through.reset(t);
        else if (t != kEndOfInput || la.test(t))
          through.set(t);
        if (t != kEndOfInput) state.named.push_back(t);
      }
      // A shadow may satisfy a positive test. A held end can also fail a
      // negative test's spelling check when another scope skipped its gap.
      for (const std::uint16_t s : test.shadows)
        if ((!test.unless || s == kEndOfInput) && la.test(s)) through.set(s);
      on.merge(through);
    }
    if (on.any()) state.reduces.emplace_back(it.prod[item], std::move(on));
  });
  std::ranges::sort(state.reduces, {}, [](const auto& r) { return r.first; });
  std::ranges::sort(state.behind);
  state.governs.assign(governs.begin(), governs.end());
  return state;
}

// Settles which operator an operand belongs to. Where a node of level `l`
// is whole and a binary or postfix operator of level `m` comes, the tighter
// level takes the operand between them; at one level its grouping tells, or
// forbids both: the state then reads the token to refuse it.
void settle_infix(const Items& it, std::vector<LrState>& states) {
  const Cfg& cfg = it.cfg;
  if (cfg.infixes.empty()) return;
  // Per production that builds a node: its table and its level.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> node(cfg.productions.size(), {kNone, kNone});
  // Per table, the terminals that begin binary or postfix operators, each
  // with the level of those operators; ascending.
  std::vector<std::vector<std::pair<std::uint16_t, std::uint32_t>>> opens(cfg.infixes.size());
  for (std::uint32_t x = 0; x < cfg.infixes.size(); ++x) {
    const Infix& table = cfg.infixes[x];
    for (std::uint32_t l = 0; l < table.levels.size(); ++l) {
      node[table.levels[l].node] = {x, l};
      if (table.levels[l].group == Association::Prefix) continue;
      it.first[table.levels[l].operators].each(
          [&](std::size_t t) { opens[x].emplace_back(static_cast<std::uint16_t>(t), l); });
    }
    std::ranges::sort(opens[x]);
  }
  // The terminals on which a state does not begin the operator that comes;
  // the copies of nodes it reduces where the operator's gap test fails.
  Bits unread(it.nterms);
  std::vector<std::tuple<std::uint32_t, std::uint16_t, std::uint32_t>> past;
  for (LrState& state : states) {
    bool emptied = false;
    past.clear();
    for (auto& [prod, on] : state.reduces) {
      const auto [x, l] = node[prod];
      if (x == kNone) continue;
      const Infix::Level& level = cfg.infixes[x].levels[l];
      unread.clear();
      // Where the token begins operators of several levels, a reading
      // stays where one of them lets it.
      on.each([&](std::size_t t) {
        const auto [from, to] = std::ranges::equal_range(
            opens[x], static_cast<std::uint16_t>(t), {}, [](const auto& o) { return o.first; });
        if (from == to) return;
        const auto [before, coming] =
            cfg.infixes[x].order(l, std::ranges::subrange(from, to) | std::views::values);
        if (!before) {
          on.reset(t);
          const auto copy = std::ranges::lower_bound(level.past, static_cast<std::uint16_t>(t), {},
                                                     [](const auto& c) { return c.first; });
          if (copy != level.past.end() && copy->first == t)
            past.emplace_back(copy->second, static_cast<std::uint16_t>(t), prod);
        }
        if (!coming) unread.set(t);
        if (!before && !coming) {
          state.barred = true;
          state.follow.push_back(static_cast<std::uint16_t>(t));
        }
      });
      if (!unread.any()) continue;
      // An operator begins with a token the state shifts, or with a symbol
      // that reads nothing, which it reduces.
      std::erase_if(state.shifts, [&](const auto& shift) { return unread.test(shift.first); });
      for (auto& [begun, with] : state.reduces)
        if (cfg.productions[begun].rhs.empty() && with.intersects(unread)) {
          with.remove(unread);
          emptied = emptied || !with.any();
        }
    }
    if (emptied)
      std::erase_if(state.reduces, [](const auto& reduce) { return !reduce.second.any(); });
    // A copy is reduced where its node is, under the node's scopes.
    for (const auto& [copy, t, original] : past) {
      auto at = std::ranges::lower_bound(state.reduces, copy, {}, [](const auto& r) { return r.first; });
      if (at == state.reduces.end() || at->first != copy) {
        at = state.reduces.emplace(at, copy, Bits(it.nterms));
        const auto first = [](const auto& b) { return b.first; };
        const auto from = std::ranges::lower_bound(state.behind, original, {}, first);
        if (from != state.behind.end() && from->first == original) {
          const std::uint64_t scopes = from->second;
          state.behind.insert(std::ranges::lower_bound(state.behind, copy, {}, first), {copy, scopes});
        }
      }
      at->second.set(t);
    }
  }
}

// The states joined wherever that changes neither a cell nor a token: the
// coarsest partition whose parts hold states of one core that decide alike
// on each terminal of their core's `cells` and move to one part on each
// symbol. A state that neither acts on such a terminal nor reads a rival of
// it decides there as the states it is grouped with do. Per state, its part;
// parts are numbered in the order of their first state.
std::vector<std::uint32_t> join_states(const Items& it, const std::vector<LrState>& states,
                                       const Graph& g, const std::vector<Bits>& cells,
                                       const std::vector<RivalRow>& takes, const Bits& always) {
  const std::size_t n = states.size();
  // What a state decides on a terminal of its core's cells: free, blocked
  // (no action, where it reads a rival), or the actions it takes, each set
  // numbered from 2 on.
  constexpr std::uint32_t kFree = 0, kBlocked = 1;
  std::map<std::vector<std::uint32_t>, std::uint32_t> actionIds;
  std::vector<std::vector<std::uint32_t>> ofCore(cells.size());
  for (std::uint32_t s = 0; s < n; ++s) ofCore[g.coreOf[s]].push_back(s);
  Bits read(it.nterms), rivals(it.nterms);
  std::vector<std::uint32_t> actions, partOf(n);
  std::uint32_t parts = 0;
  for (std::size_t c = 0; c < ofCore.size(); ++c) {
    auto& statesOf = ofCore[c];
    if (statesOf.empty()) continue;
    if (statesOf.size() == 1 || !cells[c].any()) {
      for (auto s : statesOf) partOf[s] = parts;
      ++parts;
      continue;
    }
    std::unordered_map<std::uint64_t, std::size_t> choices;
    using Decision = std::pair<std::size_t, std::size_t>;
    std::vector<std::vector<Decision>> rows;
    for (const auto s : statesOf) {
      auto& row = rows.emplace_back();
      const LrState& state = states[s];
      read = always;
      for (const auto& [terminal, to] : state.shifts) read.set(terminal);
      for (const auto& [prod, on] : state.reduces) read.merge(on);
      rivals.clear();
      read.each([&](std::size_t t) { rivals.merge(takes[t]); });
      cells[c].each([&](std::size_t t) {
        actions.clear();
        const auto shift = std::ranges::lower_bound(state.shifts, t, {},
                                                    [](const auto& e) { return e.first; });
        if (shift != state.shifts.end() && shift->first == t) actions.push_back(kNone);
        for (const auto& [prod, on] : state.reduces)
          if (on.test(t)) actions.push_back(prod);
        std::uint32_t value = rivals.test(t) ? kBlocked : kFree;
        if (!actions.empty())
          value = actionIds.try_emplace(actions, static_cast<std::uint32_t>(actionIds.size() + 2))
                      .first->second;
        if (value != kFree) {
          const auto key = std::uint64_t{t} << 32 | value;
          row.emplace_back(t, choices.try_emplace(key, choices.size()).first->second);
        }
      });
    }
    // Fill the earliest compatible groups, 64 at a time. Rejected rows can
    // never join this block as its groups only gain decisions.
    std::vector<std::uint64_t> fixed(it.nterms), same(choices.size());
    std::vector<std::uint32_t> placed;
    const auto block = [&](std::vector<std::uint32_t>& pending) {
      for (const auto i : placed)
        for (const auto [cell, choice] : rows[i]) fixed[cell] = same[choice] = 0;
      placed.clear();
      unsigned used = 0;
      std::size_t left = 0;
      for (const auto i : pending) {
        std::uint64_t fits = ~std::uint64_t{0};
        for (const auto [cell, choice] : rows[i]) {
          fits &= ~fixed[cell] | same[choice];
          if (!fits) break;
        }
        if (!fits) {
          pending[left++] = i;
          continue;
        }
        const unsigned group = std::countr_zero(fits);
        partOf[statesOf[i]] = parts + group;
        used = std::max(used, group + 1);
        const auto bit = std::uint64_t{1} << group;
        placed.push_back(i);
        for (const auto [cell, choice] : rows[i]) {
          fixed[cell] |= bit;
          same[choice] |= bit;
        }
      }
      pending.resize(left);
      parts += used;
    };
    std::vector<std::uint32_t> pending(statesOf.size());
    std::iota(pending.begin(), pending.end(), 0u);
    block(pending);
    if (pending.empty()) continue;
    // Decisions made by every remaining row partition the search: no group
    // can join across keys. Free cells still use first-compatible order.
    std::vector<std::size_t> count(it.nterms);
    for (const auto i : pending)
      for (const auto [cell, choice] : rows[i]) ++count[cell];
    std::unordered_map<std::vector<std::uint64_t>, std::vector<std::uint32_t>, WordsHash> byKey;
    std::vector<std::uint64_t> key;
    for (const auto i : pending) {
      key.clear();
      auto& row = rows[i];
      auto keep = row.begin();
      for (const auto d : row)
        if (count[d.first] == pending.size()) key.push_back(d.second);
        else *keep++ = d;
      row.erase(keep, row.end());
      byKey[key].push_back(i);
    }
    for (auto& [decisions, batch] : byKey)
      while (!batch.empty()) block(batch);
  }
  // Every transition into an LR core has the symbol before its dot.
  parts = refine_partition(partOf, parts, 1, [&](auto add) {
    for (std::uint32_t s = 0; s < n; ++s)
      for (const auto target : g.to[s]) add(s, target, 0);
  });
  // Parts numbered in the order of their first state: the start stays 0.
  std::vector<std::uint32_t> number(parts, kNone);
  std::uint32_t next = 0;
  for (auto& b : partOf) {
    if (number[b] == kNone) number[b] = next++;
    b = number[b];
  }
  return partOf;
}

}  // namespace

std::vector<std::uint32_t> left_components(const Cfg& cfg, std::span<const std::uint32_t> prefix,
                                           const std::vector<bool>& nullable) {
  std::vector<std::uint32_t> starts{0}, edges;
  for (const auto& rules : cfg.productionsOf) {
    for (const auto p : rules) {
      const auto& rhs = cfg.productions[p].rhs;
      for (auto at = prefix.empty() ? 0u : prefix[p]; at < rhs.size() && !is_terminal(rhs[at]); ++at) {
        const auto child = index_of(rhs[at]);
        edges.push_back(child);
        if (nullable.empty() || !nullable[child]) break;
      }
    }
    starts.push_back(static_cast<std::uint32_t>(edges.size()));
  }
  std::vector<std::uint32_t> group(cfg.nonterminals);
  strong_components(starts, edges, [&](auto members) {
    for (const auto nt : members) group[nt] = members.front();
  });
  return group;
}

void split_leads(Cfg& cfg) {
  auto blanks = cfg.nullable();
  const auto group = left_components(cfg, {}, blanks);
  std::vector<bool> hidden(cfg.productions.size());
  bool recursive = false;
  for (std::size_t q = 0; q < hidden.size(); ++q) {
    const Production& p = cfg.productions[q];
    for (std::size_t at = 0; at < p.rhs.size() && !is_terminal(p.rhs[at]); ++at) {
      const auto nt = index_of(p.rhs[at]);
      if (at != 0 && group[nt] == group[p.lhs]) hidden[q] = recursive = true;
      if (!blanks[nt]) break;
    }
  }
  if (!recursive && cfg.regions.size() < 2) return;
  const Items it(cfg, std::move(blanks));
  const std::vector<Production> original = cfg.productions;
  const std::vector<std::vector<std::uint32_t>> of = cfg.productionsOf;
  // The productions an infix table names stay as they are.
  std::vector<bool> fixed(cfg.nonterminals, false);
  for (const Infix& table : cfg.infixes)
    for (const Infix::Level& level : table.levels) {
      fixed[original[level.node].lhs] = true;
      fixed[level.operators] = true;
    }
  const auto nullable = [&](Symbol s) { return !is_terminal(s) && it.nullable[index_of(s)]; };
  // Per nonterminal, its copy that reads nothing and its copy that reads
  // something, made where asked.
  std::vector<std::uint32_t> empty(cfg.nonterminals, kNone), full(cfg.nonterminals, kNone);
  std::vector<std::pair<std::uint32_t, bool>> asked;
  const auto copy = [&](Symbol s, bool reads) -> Symbol {
    const std::uint32_t nt = index_of(s);
    if (!reads && it.blank[nt]) return s;
    std::uint32_t& made = (reads ? full : empty)[nt];
    if (made == kNone) {
      made = cfg.nonterminals++;
      asked.emplace_back(nt, reads);
    }
    return nonterminal(made);
  };
  // A nullable prefix has two forms: it read a token, or it stayed empty.
  // Each pair extends the preceding pair by one symbol and keeps the scopes
  // and takes of the symbols it reads.
  const auto ways = [&](const Production& p, bool blank, auto&& each) {
    const auto& rhs = p.rhs;
    Symbol read = kNone, skipped = kNone;
    std::size_t i = 0;
    for (; i + 1 < rhs.size() && nullable(rhs[i]); ++i) {
      const Symbol sym = rhs[i];
      const bool canRead = !it.blank[index_of(sym)];
      if (i == 0) {
        if (canRead) read = copy(sym, true);
        skipped = copy(sym, false);
        continue;
      }
      const auto extend = [&](Symbol lhs, Symbol before, Symbol after) {
        if (lhs == kNone) lhs = nonterminal(cfg.nonterminals++);
        Production& pair = cfg.productions.emplace_back();
        pair.lhs = index_of(lhs);
        pair.rule = p.rule;
        pair.alternative = p.alternative;
        pair.rhs = {before, after};
        pair.region = p.region;
        pair.prefixTurn = p.prefixTurn;
        if (!p.scopes.empty()) pair.scopes = {p.scope_at(0), p.scope_at(i)};
        if (!p.takes.empty()) pair.takes = {i == 1 ? p.takes[0] : kNone, p.takes[i]};
        return lhs;
      };
      Symbol next = kNone;
      if (read != kNone) next = extend(next, read, sym);
      if (canRead) next = extend(next, skipped, copy(sym, true));
      read = next;
      skipped = extend(kNone, skipped, copy(sym, false));
    }
    if (rhs.empty()) return;
    const auto finish = [&](Symbol prefix, Symbol at) {
      std::vector<Symbol> way(rhs.begin() + i, rhs.end());
      way.front() = at;
      if (prefix != kNone) way.insert(way.begin(), prefix);
      each(std::move(way), i > 1 ? i - 1 : 0);
    };
    if (read != kNone) finish(read, rhs[i]);
    if (!nullable(rhs[i])) {
      finish(skipped, rhs[i]);
    } else {
      if (!it.blank[index_of(rhs[i])]) finish(skipped, copy(rhs[i], true));
      if (blank) finish(skipped, copy(rhs[i], false));
    }
  };
  // The symbols folded into the pair go and the pair itself takes nothing;
  // the rest keep their scopes and takes.
  const auto replace = [](Production& p, std::vector<Symbol> rhs, std::size_t folded) {
    p.erase(1, folded);
    if (folded != 0 && !p.takes.empty()) p.takes[0] = kNone;
    p.rhs = std::move(rhs);
  };
  // A production that tests its scope's end reads that end after its symbols.
  for (std::uint32_t q = 0; q < original.size(); ++q) {
    const Production& p = original[q];
    if (fixed[p.lhs] || p.rhs.size() < (it.ends(q) ? 1u : 2u) || !nullable(p.rhs[0]) ||
        (!hidden[q] && (it.begins[p.lhs] & ~(std::uint64_t{1} << p.region)) == 0))
      continue;
    bool first = true;
    ways(p, true, [&](std::vector<Symbol>&& rhs, std::size_t folded) {
      if (first) {
        replace(cfg.productions[q], std::move(rhs), folded);
        first = false;
        return;
      }
      cfg.productions.push_back(p);
      replace(cfg.productions.back(), std::move(rhs), folded);
    });
  }
  while (!asked.empty()) {
    const auto [nt, reads] = asked.back();
    asked.pop_back();
    const std::uint32_t made = (reads ? full : empty)[nt];
    for (const std::uint32_t q : of[nt]) {
      const Production& p = original[q];
      const auto add = [&](std::vector<Symbol>&& rhs, std::size_t folded) {
        cfg.productions.push_back(p);
        cfg.productions.back().lhs = made;
        replace(cfg.productions.back(), std::move(rhs), folded);
      };
      if (reads) {
        ways(p, false, add);
      } else if (std::ranges::all_of(p.rhs, nullable)) {
        std::vector<Symbol> rhs;
        for (const Symbol s : p.rhs) rhs.push_back(copy(s, false));
        add(std::move(rhs), 0);
      }
    }
  }
  cfg.productionsOf.assign(cfg.nonterminals, {});
  for (std::uint32_t q = 0; q < cfg.productions.size(); ++q)
    if (cfg.productions[q].copyOf == kNone) cfg.productionsOf[cfg.productions[q].lhs].push_back(q);
}

void shadow_tests(Cfg& cfg, const std::vector<RivalRow>& reads) {
  Bits shadow(cfg.terminals.size()), trivia(cfg.terminals.size());
  for (const auto& scope : cfg.regions)
    for (const auto* list : {&scope.ignored, &scope.hidden})
      for (auto t : *list) trivia.set(t);
  std::vector<bool> narrower;
  for (Region& scope : cfg.regions) {
    shadow.clear();
    Bits foreign = trivia;
    for (const auto* list : {&scope.ignored, &scope.hidden})
      for (const std::uint16_t t : *list) {
        shadow.merge(reads[t]);
        foreign.reset(t);
      }
    narrower.push_back(foreign.any());
    shadow.each([&](std::size_t s) {
      if (cfg.terminals[s].trivia == Trivia::none)
        scope.shadows.push_back(static_cast<std::uint16_t>(s));
    });
  }
  for (Production& p : cfg.productions)
    for (Test& test : p.tests) {
      if (test.ahead.empty()) continue;
      shadow.clear();
      for (const std::uint16_t t : cfg.regions[p.region].shadows) shadow.set(t);
      for (const std::uint16_t t : test.ahead)
        if (t != kEndOfInput && !test.elected) shadow.merge(reads[t]);
      for (const std::uint16_t t : test.ahead) shadow.reset(t);
      // A held end may stand past trivia this scope would have stopped at.
      if (narrower[p.region] && std::ranges::find(test.ahead, kEndOfInput) != test.ahead.end())
        shadow.set(kEndOfInput);
      shadow.each([&](std::size_t s) {
        if (cfg.terminals[s].trivia == Trivia::none)
          test.shadows.push_back(static_cast<std::uint16_t>(s));
      });
    }
}

static bool atomic_test(const Production& q) {
  return q.rhs.empty() && q.tests.size() == 1 && !q.implied && !q.start && q.entry == kNone;
}

// Each entry to a guarded left recursion leaves its conditions on the stack.
// Its turns consult those markers before accepting a suffix.
static void guard_turns(Cfg& cfg) {
  const std::size_t n = cfg.productions.size();
  std::vector<std::uint32_t> prefix(n);
  // A choice or sequence of conditions still reads no input. A transparent
  // unary rule uses the same marker as its child.
  std::vector<Symbol> guard(cfg.nonterminals, kNone);
  std::vector<bool> writes(cfg.nonterminals), tested(cfg.nonterminals);
  std::vector<std::uint32_t> pending(cfg.nonterminals);
  std::vector<std::vector<std::uint32_t>> parents(cfg.nonterminals);
  std::vector<std::uint32_t> work;
  for (std::uint32_t nt = 0; nt < cfg.nonterminals; ++nt) {
    for (const auto p : cfg.productionsOf[nt]) {
      const auto& q = cfg.productions[p];
      writes[nt] = writes[nt] || q.entry != kNone;
      tested[nt] = tested[nt] || q.tested();
      if (q.start || q.implied || std::ranges::any_of(q.rhs, is_terminal)) {
        pending[nt] = kNone;
        break;
      }
      for (const auto s : q.rhs) {
        ++pending[nt];
        parents[index_of(s)].push_back(nt);
      }
    }
    if (pending[nt] == 0 && !cfg.productionsOf[nt].empty()) work.push_back(nt);
  }
  while (!work.empty()) {
    const auto nt = work.back();
    work.pop_back();
    guard[nt] = nonterminal(nt);
    const auto& ways = cfg.productionsOf[nt];
    if (ways.size() == 1) {
      const auto& q = cfg.productions[ways.front()];
      if (q.rhs.size() == 1 && q.entry == kNone && !q.tested() && q.takes.empty())
        guard[nt] = guard[index_of(q.rhs.front())];
    }
    for (const auto parent : parents[nt]) {
      tested[parent] = tested[parent] || tested[nt];
      writes[parent] = writes[parent] || writes[nt];
      if (pending[parent] != kNone && --pending[parent] == 0) work.push_back(parent);
    }
  }
  bool needed = false;
  for (std::uint32_t p = 0; p < n; ++p) {
    const auto& rhs = cfg.productions[p].rhs;
    auto& at = prefix[p];
    while (at < rhs.size() && !is_terminal(rhs[at]) && guard[index_of(rhs[at])] != kNone) ++at;
    needed |= at != 0 && at < rhs.size() && !is_terminal(rhs[at]);
  }
  if (!needed) return;
  const auto group = left_components(cfg, prefix);
  struct Mark {
    Symbol symbol;
    std::uint16_t scope;
    std::uint32_t take;
    auto operator<=>(const Mark&) const = default;
  };
  const auto mark_at = [&](const Production& q, std::size_t i) {
    return Mark{guard[index_of(q.rhs[i])], q.scope_at(i), q.takes.empty() ? kNone : q.takes[i]};
  };
  std::map<std::uint32_t, std::map<Mark, std::uint32_t>> marks;
  std::vector<bool> turn(n);
  for (std::uint32_t p = 0; p < n; ++p) {
    const auto& q = cfg.productions[p];
    const auto at = prefix[p];
    if (at == q.rhs.size() || is_terminal(q.rhs[at]) || group[q.lhs] != group[index_of(q.rhs[at])]) continue;
    turn[p] = true;
    for (std::uint32_t i = 0; i < at; ++i) {
      const auto nt = index_of(q.rhs[i]);
      if (tested[nt] || writes[nt]) marks[group[q.lhs]].try_emplace(mark_at(q, i), 0);
    }
  }
  std::map<Mark, std::pair<Symbol, Symbol>> answers;
  for (auto& [component, tests] : marks) {
    auto depth = static_cast<std::uint32_t>(tests.size());
    for (auto& [mark, index] : tests) {
      index = --depth;
      const auto [it, fresh] = answers.try_emplace(mark);
      if (!fresh) continue;
      auto& [yes, no] = it->second;
      yes = mark.symbol;
      no = kNone;
      const auto& ways = cfg.productionsOf[index_of(yes)];
      Production q;
      if (writes[index_of(yes)]) {
        q.lhs = cfg.nonterminals++;
        q.rhs = {yes};
        q.region = mark.scope;
        q.capture = true;
        if (mark.take != kNone) q.takes = {mark.take};
        yes = nonterminal(q.lhs);
        cfg.recordInputs.push_back({static_cast<std::uint32_t>(cfg.productions.size()), {}, {}});
        cfg.productions.push_back(std::move(q));
        if (!tested[index_of(mark.symbol)]) continue;
        q = {};
      }
      if (ways.size() == 1 && atomic_test(cfg.productions[ways.front()])) {
        q = cfg.productions[ways.front()];
      } else {
        // A compound condition uses the same subparser for both answers.
        auto& start = cfg.productions.emplace_back();
        start.lhs = cfg.nonterminals++;
        start.rhs = {mark.symbol};
        start.region = mark.scope;
        start.start = true;
        cfg.conditions.push_back(static_cast<std::uint32_t>(cfg.productions.size() - 1));
        q.region = mark.scope;
        q.tests.emplace_back().condition = static_cast<std::uint32_t>(cfg.conditions.size());
        if (yes == mark.symbol) {
          q.lhs = cfg.nonterminals++;
          yes = nonterminal(q.lhs);
          cfg.productions.push_back(q);
        }
      }
      q.lhs = cfg.nonterminals++;
      no = nonterminal(q.lhs);
      Test& test = q.tests.front();
      if (test.gap == Test::Gap::any) test.unless = !test.unless;
      else test.gapUnless = !test.gapUnless;
      cfg.productions.push_back(std::move(q));
    }
  }
  std::map<Symbol, Symbol> wrapped;
  for (std::uint32_t p = 0; p < n; ++p) {
    auto& q = cfg.productions[p];
    const auto at = turn[p] ? prefix[p] : 0;
    if (at) {
      std::vector<Cfg::Opening> opening;
      for (std::uint32_t i = 0; i < at; ++i) {
        const auto mark = mark_at(q, i);
        const auto nt = index_of(mark.symbol);
        if (tested[nt] || writes[nt])
          opening.push_back({index_of(answers.at(mark).first), marks.at(group[q.lhs]).at(mark), writes[nt]});
      }
      if (std::ranges::any_of(opening, &Cfg::Opening::writes))
        cfg.recordInputs.push_back({p, q.rhs, q.takes});
      if (!opening.empty()) {
        cfg.openings.push_back(std::move(opening));
        q.opening = static_cast<std::uint32_t>(cfg.openings.size());
      }
      q.erase(0, at);
    }
    for (std::size_t i = turn[p] ? 1 : 0; i < q.rhs.size(); ++i) {
      auto& symbol = q.rhs[i];
      if (is_terminal(symbol) || !marks.contains(group[index_of(symbol)])) continue;
      const auto [it, fresh] = wrapped.try_emplace(symbol, nonterminal(cfg.nonterminals));
      if (fresh) ++cfg.nonterminals;
      symbol = it->second;
    }
  }
  for (const auto& [nt, outer] : wrapped) {
    const auto& tests = marks.at(group[index_of(nt)]);
    Symbol target = nt;
    for (const auto& [mark, depth] : std::views::reverse(tests)) {
      const auto lhs = depth + 1 == tests.size() ? outer : nonterminal(cfg.nonterminals++);
      const auto [yes, no] = answers.at(mark);
      for (const auto answer : {yes, no}) {
        if (answer == kNone) continue;
        auto& q = cfg.productions.emplace_back();
        q.lhs = index_of(lhs);
        q.region = mark.scope;
        q.rhs = {answer, target};
      }
      target = lhs;
    }
  }
  cfg.productionsOf.resize(cfg.nonterminals);
  for (std::uint32_t p = n; p < cfg.productions.size(); ++p) cfg.productionsOf[cfg.productions[p].lhs].push_back(p);
}

void bar_tests(Cfg& cfg) {
  const auto original = cfg.productions.size();
  const auto originalNts = cfg.nonterminals;
  guard_turns(cfg);
  if (std::ranges::none_of(cfg.productions, atomic_test)) return;
  std::vector<const Test*> refuses(cfg.nonterminals, nullptr);
  const auto ends_scope = [&](std::uint16_t t) {
    return t != kEndOfInput &&
           std::ranges::any_of(cfg.regions, [&](const Region& r) { return r.end == t; });
  };
  for (std::uint32_t nt = 0; nt < originalNts; ++nt) {
    const auto& ways = cfg.productionsOf[nt];
    if (ways.size() != 1) continue;
    const auto& q = cfg.productions[ways.front()];
    if (!atomic_test(q)) continue;
    const Test& test = q.tests.front();
    if (test.unless && !test.ahead.empty() && test.condition == 0 &&
        test.gap == Test::Gap::any && test.shadows.empty() && !test.elected &&
        std::ranges::none_of(test.ahead, ends_scope))
      refuses[nt] = &test;
  }
  if (std::ranges::none_of(refuses, [](const Test* test) { return test != nullptr; })) return;
  const Items it(cfg);
  std::vector<std::size_t> barred;
  for (std::uint32_t p = 0; p < original; ++p) {
    Production& q = cfg.productions[p];
    barred.clear();
    for (std::size_t at = 0; at < q.rhs.size(); ++at) {
      const Symbol s = q.rhs[at];
      const Test* test = is_terminal(s) ? nullptr : refuses[index_of(s)];
      // The symbols after it may begin with none of what it refuses.
      if (test == nullptr || std::ranges::any_of(test->ahead, [&](std::uint16_t t) {
            return it.after[it.base[p] + at].test(t);
          }))
        continue;
      const auto place = static_cast<std::uint32_t>(at - barred.size());
      if (q.bars.empty() || q.bars.back().first != place)
        q.bars.emplace_back(place, std::vector<std::uint16_t>{});
      q.bars.back().second.insert(q.bars.back().second.end(), test->ahead.begin(),
                                  test->ahead.end());
      barred.push_back(at);
    }
    for (const std::size_t at : std::views::reverse(barred)) q.erase(at, 1);
  }
}

Bits trivia_shadows(const Cfg& cfg, std::uint64_t scopes) {
  Bits out(cfg.terminals.size());
  for (std::size_t r = 0; r < cfg.regions.size(); ++r)
    if ((scopes >> r & 1) != 0)
      for (const std::uint16_t t : cfg.regions[r].shadows) out.set(t);
  return out;
}

std::uint32_t reduced_alone(const Cfg& cfg, const LrState& state) {
  if (!state.shifts.empty() || state.reduces.size() != 1 || state.barred) return kNone;
  const std::uint32_t lone = state.reduces.front().first;
  const Production& q = cfg.productions[lone];
  const bool emptyNode = q.rhs.empty() && q.entry != kNone &&
                         cfg.rows[cfg.entries[q.entry].action]->kind == WriteKind::create;
  return lone != 0 && !q.tested() && !q.start && !emptyNode ? lone : kNone;
}

LrAutomaton build_lr1(const Cfg& cfg, const std::vector<RivalRow>& takes, const Bits& always,
                      std::uint32_t maxStates) {
  const Items it(cfg);
  const std::vector<Start> starts = start_kernels(it);
  std::vector<std::uint32_t> startCores;
  const std::vector<Core> cores = cores_of(it, starts, startCores, maxStates);
  Graph g;
  for (std::uint32_t c = 0; c < cores.size(); ++c) {
    g.coreOf.push_back(c);
    std::vector<std::uint32_t>& to = g.to.emplace_back();
    for (const auto& [symbol, target] : cores[c].edges) to.push_back(target);
  }
  Nodes nodes(cores, g);
  std::vector<Bits> cells(cores.size(), Bits(it.nterms));
  std::vector<Bits> rivalled(cores.size(), Bits(it.nterms));
  auto la = lookaheads(it, cores, g, nodes, starts, startCores);
  std::vector<std::uint32_t> startStates = startCores;
  std::vector<Bits> selected;
  // The cores whose states reduce without reading a token: no terminal
  // leaves them, and they reduce one production alone.
  std::vector<bool> alone(cores.size());
  {
    Closure closure(it);
    for (std::uint32_t c = 0; c < cores.size(); ++c) {
      const Core& core = cores[c];
      if (core.reduces.size() != 1 ||
          std::ranges::any_of(core.edges, [](const auto& edge) { return is_terminal(edge.first); }))
        continue;
      alone[c] = reduced_alone(cfg, facts(it, closure, core.kernel,
                                          std::span(la).subspan(nodes.first[c], core.kernel.size()))) != kNone;
    }
  }
  if (auto seeds = cells_of(it, cores, nodes, la, takes, always, alone, cells, rivalled);
      std::ranges::any_of(seeds, &Bits::any)) {
    la.clear();
    Relation flow = dependencies(cores, g, nodes);
    auto full = maxStates ? seeds : std::move(seeds);
    spread(flow, full);
    if (auto made = split(cores, nodes, full, starts, startCores, startStates, maxStates)) {
      g = std::move(*made);
    } else {
      // Choices rank by reductions, rivals and incoming contexts per estimated
      // propagation work. Only context-dependent choices need refinement.
      struct Hot {
        std::uint32_t core;
        std::uint16_t terminal;
        double score;
      };
      std::vector<Hot> hot;
      std::vector<std::uint32_t> incoming(cores.size(), 0);
      std::vector<std::size_t> cost(it.nterms, 0), uses(it.nterms, 0);
      for (const Bits& bits : full) bits.each([&](std::size_t t) { ++cost[t]; });
      for (const Core& core : cores)
        for (const auto& [symbol, target] : core.edges) ++incoming[target];
      Bits varying(it.nterms);
      for (std::uint32_t c = 0; c < cores.size(); ++c) {
        varying.clear();
        for (std::uint32_t k = nodes.first[c]; k != nodes.first[c + 1]; ++k)
          seeds[k].each([&](std::size_t t) {
            if (uses[t]++ == 0) varying.set(t);
          });
        varying.each([&](std::size_t t) {
          const double score =
              static_cast<double>(cores[c].reduces.size() + 1 + rivalled[c].test(t)) *
              std::max(incoming[c], 1u) / (static_cast<double>(cost[t]) * uses[t]);
          hot.push_back({c, static_cast<std::uint16_t>(t), score});
          uses[t] = 0;
        });
      }
      std::ranges::sort(hot, [](const auto& a, const auto& b) {
        return a.score != b.score ? a.score > b.score
                                 : std::pair{a.core, a.terminal} < std::pair{b.core, b.terminal};
      });
      const auto select = [&](std::size_t keep) {
        selected.assign(cores.size(), Bits(it.nterms));
        for (std::size_t i = 0; i < keep; ++i) selected[hot[i].core].set(hot[i].terminal);
      };
      const auto refine = [&](std::size_t keep, std::vector<std::uint32_t>& startsOf) {
        select(keep);
        std::vector<Bits> rel = seeds;
        for (std::uint32_t c = 0; c < cores.size(); ++c)
          for (std::uint32_t k = nodes.first[c]; k != nodes.first[c + 1]; ++k)
            rel[k] &= selected[c];
        spread(flow, rel);
        return split(cores, nodes, rel, starts, startCores, startsOf, maxStates);
      };
      full.clear();
      // More seeds can only split states. Find the largest prefix that fits.
      std::size_t fits = 0, fails = hot.size();
      Graph best = std::move(g);
      std::vector<std::uint32_t> bestStarts = startCores;
      while (fits + 1 < fails) {
        const std::size_t mid = fits + (fails - fits) / 2;
        std::vector<std::uint32_t> trialStarts;
        if (auto trial = refine(mid, trialStarts)) {
          fits = mid;
          best = std::move(*trial);
          bestStarts = std::move(trialStarts);
        } else {
          fails = mid;
        }
      }
      select(fits);
      g = std::move(best);
      startStates = std::move(bestStarts);
    }
    nodes = Nodes(cores, g);
    la = lookaheads(it, cores, g, nodes, starts, startStates);
  }
  Closure closure(it);
  std::vector<LrState> states;
  states.reserve(g.coreOf.size());
  for (std::uint32_t s = 0; s < g.coreOf.size(); ++s) {
    const Core& core = cores[g.coreOf[s]];
    const auto lookahead = std::span(la).subspan(nodes.first[s], core.kernel.size());
    states.push_back(facts(it, closure, core.kernel, lookahead));
    for (std::size_t e = 0; e < core.edges.size(); ++e) {
      const Symbol symbol = core.edges[e].first;
      if (is_terminal(symbol))
        states.back().shifts.emplace_back(static_cast<std::uint16_t>(symbol), g.to[s][e]);
      else
        states.back().gotos.emplace_back(index_of(symbol), g.to[s][e]);
    }
  }
  pass_trivia(it, states);
  settle_infix(it, states);
  const auto part = join_states(it, states, g, cells, takes, always);
  // A part keeps its first state's edges and kernel and all its reductions and test tokens.
  std::vector<std::vector<std::uint32_t>> groups;
  for (std::uint32_t s = 0; s < states.size(); ++s) {
    if (part[s] == groups.size()) groups.emplace_back();
    groups[part[s]].push_back(s);
  }
  LrAutomaton out;
  out.states.reserve(groups.size());
  for (const auto& group : groups) {
    const auto first = group.front(), core = g.coreOf[first];
    LrState& state = out.states.emplace_back(std::move(states[first]));
    for (auto& [terminal, to] : state.shifts) to = part[to];
    for (auto& [nt, to] : state.gotos) to = part[to];
    for (const auto& pos : cores[core].kernel)
      state.kernel.emplace_back(it.prod[pos.item], it.dot[pos.item]);
    if (!selected.empty())
      rivalled[core].each([&](std::size_t t) {
        if (!selected[core].test(t)) state.doubted.push_back(static_cast<std::uint16_t>(t));
      });
    if (group.size() > 1) {
      std::map<std::uint32_t, Bits> reduces;
      for (const auto s : group) {
        auto& from = s == first ? state : states[s];
        for (auto& [prod, on] : from.reduces) {
          const auto [at, fresh] = reduces.try_emplace(prod, std::move(on));
          if (!fresh) at->second.merge(on);
        }
        if (s == first) continue;
        state.named.insert(state.named.end(), from.named.begin(), from.named.end());
        state.follow.insert(state.follow.end(), from.follow.begin(), from.follow.end());
        state.barred = state.barred || from.barred;
      }
      state.reduces.assign(std::make_move_iterator(reduces.begin()),
                           std::make_move_iterator(reduces.end()));
    }
    for (std::vector<std::uint16_t>* list : {&state.named, &state.follow}) {
      std::ranges::sort(*list);
      list->erase(std::unique(list->begin(), list->end()), list->end());
    }
  }
  for (std::size_t i = 1; i < startStates.size(); ++i)
    out.conditions.push_back(part[startStates[i]]);
  return out;
}

}  // namespace cursiv::engine::gen
