#include "cursiv/engine/parser/InsertOrder.hpp"

#include <algorithm>
#include <ranges>


namespace cursiv::engine::gen {

namespace {

constexpr std::uint64_t kUnread = ~std::uint64_t{0};
constexpr std::uint64_t kStays = std::uint64_t{1} << 32;
constexpr std::uint32_t kNoList = ~std::uint32_t{0};

void sort_unique(auto& v) {
  std::ranges::sort(v);
  v.erase(std::unique(v.begin(), v.end()), v.end());
}

}  // namespace

InsertOrder::InsertOrder(const Cfg& cfg, const LrAutomaton& automaton, const Closers& needs)
    : _cfg(cfg), _lr(automaton), _needs(needs), _returnsOf(automaton.states.size(), kNoList) {
  const std::vector<Production>& prods = cfg.productions;
  for (const Production& p : prods) {
    std::size_t from = p.rhs.size();
    while (from > 0 && !is_terminal(p.rhs[from - 1]) && _needs.nullable[index_of(p.rhs[from - 1])]) --from;
    _nullFrom.push_back(static_cast<std::uint32_t>(from));
    _base.push_back(_base.back() + static_cast<std::uint32_t>(p.rhs.size()) + 1);
  }
  _preds.resize(automaton.states.size());
  for (std::uint32_t s = 0; s < automaton.states.size(); ++s) {
    for (const auto& [terminal, to] : automaton.states[s].shifts) _preds[to].push_back(s);
    for (const auto& [nt, to] : automaton.states[s].gotos) _preds[to].push_back(s);
  }
  for (auto& preds : _preds) preds.erase(std::unique(preds.begin(), preds.end()), preds.end());
  rank_productions();
}

// Each production's place in a reading of the grammar from its start, a
// nonterminal opened where it is first met; a turn of a left recursion ranks
// before the rules that read what it grew.
void InsertOrder::rank_productions() {
  const std::vector<Production>& prods = _cfg.productions;
  _rank.assign(prods.size(), kUnread);
  std::vector<bool> opened(_cfg.nonterminals, false);
  std::vector<std::pair<std::uint32_t, std::size_t>> todo{{0, 0}};
  std::uint64_t count = 0;
  while (!todo.empty()) {
    auto [p, i] = todo.back();
    todo.pop_back();
    const Production& q = prods[p];
    if (i == 0) _rank[p] = count++;
    for (; i < q.rhs.size(); ++i) {
      const Symbol y = q.rhs[i];
      if (is_terminal(y) || opened[index_of(y)]) continue;
      opened[index_of(y)] = true;
      todo.emplace_back(p, i + 1);
      for (const std::uint32_t arm : std::views::reverse(_cfg.productionsOf[index_of(y)])) todo.emplace_back(arm, 0);
      break;
    }
  }
  for (std::uint32_t p = 0; p < prods.size(); ++p)
    if (_rank[p] == kUnread) _rank[p] = count++;
  for (std::uint32_t p = 0; p < prods.size(); ++p)
    if (!_needs.turns[p]) _rank[p] |= kStays;
}

std::uint32_t InsertOrder::list_of(std::vector<std::uint32_t> states) {
  sort_unique(states);
  const auto [at, fresh] =
      _listIds.try_emplace(std::move(states), static_cast<std::uint32_t>(_lists.size()));
  if (fresh) _lists.push_back({&at->first});
  return at->second;
}

const InsertOrder::Listed& InsertOrder::filled(std::uint32_t id) {
  if (!std::exchange(_lists[id].filled, true)) {
    Listed& list = _lists[id];
    for (const std::uint32_t s : *list.states) {
      const auto& kernel = _lr.states[s].kernel;
      list.items.insert(list.items.end(), kernel.begin(), kernel.end());
      list.returns.push_back(returns(s));
    }
    sort_unique(list.items);
    sort_unique(list.returns);
  }
  return _lists[id];
}

// Reads `_picked` in order: a terminal of `row` is met, a nonterminal opens
// its alternatives depth first, once, and one that may read nothing lets the
// item go on.
void InsertOrder::read(const Bits& row, std::size_t want, std::vector<std::uint16_t>& out) {
  _todo.assign(_picked.rbegin(), _picked.rend());
  while (!_todo.empty() && out.size() < want) {
    auto [p, j] = _todo.back();
    _todo.pop_back();
    const Production& q = _cfg.productions[p];
    for (; j < q.rhs.size(); ++j) {
      const Symbol y = q.rhs[j];
      if (is_terminal(y)) {
        if (_met.first(y) && row.test(y)) out.push_back(static_cast<std::uint16_t>(y));
        break;
      }
      const std::uint32_t b = index_of(y);
      if (_seen.first(b)) {
        if (_needs.nullable[b]) _todo.emplace_back(p, j + 1);
        for (const std::uint32_t arm : std::views::reverse(_cfg.productionsOf[b])) _todo.emplace_back(arm, 0);
        break;
      }
      if (!_needs.nullable[b]) break;
    }
  }
}

// The list of states the rules of the kernel items of `state` that can end
// return to: the goto on the left side of each from the states under its
// symbols, walked down once for all items by depth.
std::uint32_t InsertOrder::returns(std::uint32_t state) {
  if (_returnsOf[state] != kNoList) return _returnsOf[state];
  std::vector<std::pair<std::uint32_t, std::uint32_t>> asked;
  for (const auto& [prod, dot] : _lr.states[state].kernel)
    if (dot >= _nullFrom[prod]) asked.emplace_back(dot, _cfg.productions[prod].lhs);
  sort_unique(asked);
  std::vector<std::uint32_t> out, here{state}, next;
  std::uint32_t depth = 0;
  for (const auto& [dot, lhs] : asked) {
    for (; depth < dot; ++depth) {
      next.clear();
      _under.next();
      for (const std::uint32_t x : here)
        for (const std::uint32_t y : _preds[x])
          if (_under.first(y)) next.push_back(y);
      here.swap(next);
    }
    for (const std::uint32_t below : here) {
      const auto& gotos = _lr.states[below].gotos;
      const auto at = std::ranges::lower_bound(gotos, lhs, {}, [](const auto& g) { return g.first; });
      if (at != gotos.end() && at->first == lhs) out.push_back(at->second);
    }
  }
  const std::uint32_t list = list_of(std::move(out));
  return _returnsOf[state] = list;
}

void InsertOrder::of(std::uint32_t state, const Bits& row, std::size_t want,
                     std::vector<std::uint16_t>& out) {
  out.clear();
  _seen.next();
  _met.next();
  _walked.next();
  _level.assign(1, list_of({state}));
  _walked.first(_level[0]);
  while (!_level.empty() && out.size() < want) {
    _placed.next();
    _picked.clear();
    _next.clear();
    for (const std::uint32_t id : _level) {
      const Listed& list = filled(id);
      for (const auto& item : list.items)
        if (_placed.first(_base[item.first] + item.second)) _picked.push_back(item);
      for (const std::uint32_t to : list.returns)
        if (_walked.first(to)) _next.push_back(to);
    }
    std::ranges::sort(_picked, {}, [&](const Item& item) {
      return std::pair{item.second, _rank[item.first]};
    });
    read(row, want, out);
    _level.swap(_next);
  }
}

}  // namespace cursiv::engine::gen
