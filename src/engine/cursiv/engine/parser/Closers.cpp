#include "cursiv/engine/parser/Closers.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <utility>

namespace cursiv::engine::gen {

Closers closers(const Cfg& cfg, const LrAutomaton& automaton) {
  const std::vector<Production>& prods = cfg.productions;
  const std::size_t nterms = cfg.terminals.size();
  const std::size_t nnts = cfg.nonterminals;
  const std::size_t nprods = prods.size();
  constexpr std::uint64_t kInf = ~std::uint64_t{0};
  const auto add = [](std::uint64_t a, std::uint64_t b) { return a > kInf - b ? kInf : a + b; };

  // The fewest tokens each nonterminal reads. The cheapest one not priced yet
  // gets no cheaper, so a production is read once, when the last nonterminal
  // it names is priced. An implied token costs the token it stands for: its
  // condition is the text's to meet, not a repair's.
  std::vector<std::uint64_t> yield(nnts, kInf);
  std::vector<std::vector<std::uint32_t>> readers(nnts);
  std::vector<std::uint32_t> unpriced(nprods, 0);
  for (std::uint32_t p = 0; p < nprods; ++p)
    for (const Symbol s : prods[p].rhs)
      if (!is_terminal(s)) {
        readers[index_of(s)].push_back(p);
        ++unpriced[p];
      }
  const auto yield_of = [&](Symbol s) {
    return is_terminal(s) ? std::uint64_t{1} : yield[index_of(s)];
  };
  using Cost = std::pair<std::uint64_t, std::uint32_t>;
  std::priority_queue<Cost, std::vector<Cost>, std::greater<>> cheapest;
  const auto price = [&](std::uint32_t p) {
    if (prods[p].implied) return;
    std::uint64_t cost = 0;
    for (const Symbol s : prods[p].rhs) cost = add(cost, yield_of(s));
    if (cost != kInf) cheapest.emplace(cost, prods[p].lhs);
  };
  for (std::uint32_t p = 0; p < nprods; ++p)
    if (unpriced[p] == 0) price(p);
  while (!cheapest.empty()) {
    const auto [cost, nt] = cheapest.top();
    cheapest.pop();
    if (yield[nt] != kInf) continue;
    yield[nt] = cost;
    for (const std::uint32_t reader : readers[nt])
      if (--unpriced[reader] == 0) price(reader);
  }

  // What each item still reads at least.
  std::vector<std::uint32_t> itemAt(nprods + 1, 0);
  for (std::uint32_t p = 0; p < nprods; ++p)
    itemAt[p + 1] = itemAt[p] + static_cast<std::uint32_t>(prods[p].rhs.size()) + 1;
  std::vector<std::uint64_t> rest(itemAt[nprods], 0);
  for (std::uint32_t p = 0; p < nprods; ++p)
    for (std::size_t i = prods[p].rhs.size(); i-- > 0;)
      rest[itemAt[p] + i] = add(yield_of(prods[p].rhs[i]), rest[itemAt[p] + i + 1]);
  const auto rest_of = [&](std::uint32_t p, std::size_t dot) { return rest[itemAt[p] + dot]; };

  // The first terminals of each nonterminal's shortest readings: those of
  // the first symbol that reads anything in each. A set only grows.
  std::vector<Bits> first(nnts, Bits(nterms));
  const auto first_from = [&](std::uint32_t p, std::size_t dot, Bits& out) {
    for (; dot < prods[p].rhs.size(); ++dot) {
      const Symbol s = prods[p].rhs[dot];
      if (yield_of(s) == 0) continue;
      if (!is_terminal(s)) return out.merge(first[index_of(s)]);
      const bool fresh = !out.test(s);
      out.set(s);
      return fresh;
    }
    return false;
  };
  {
    std::vector<std::uint32_t> work;
    std::vector<bool> queued(nprods, false);
    for (std::uint32_t p = 0; p < nprods; ++p)
      if (yield[prods[p].lhs] != kInf && rest_of(p, 0) == yield[prods[p].lhs]) {
        work.push_back(p);
        queued[p] = true;
      }
    std::vector<bool> shortest = queued;
    while (!work.empty()) {
      const std::uint32_t p = work.back();
      work.pop_back();
      queued[p] = false;
      if (!first_from(p, 0, first[prods[p].lhs])) continue;
      for (const std::uint32_t reader : readers[prods[p].lhs])
        if (shortest[reader] && !queued[reader]) {
          queued[reader] = true;
          work.push_back(reader);
        }
    }
  }

  Closers out;

  // What an item needs: nothing counted for one that may have read nothing
  // yet, the options it begins with left out, nor for a list's continuation.
  // A list read stands before the dot even where it read no item.
  const auto component = left_components(cfg);
  out.turns.resize(nprods);
  for (std::size_t p = 0; p < nprods; ++p) {
    const auto& q = prods[p];
    out.turns[p] = !q.rhs.empty() && !is_terminal(q.rhs[0]) && component[q.lhs] == component[index_of(q.rhs[0])];
  }
  out.nullable = cfg.nullable();
  const std::vector<bool>& turns = out.turns;
  std::vector<bool> list(nnts, false);
  for (std::uint32_t p = 0; p < nprods; ++p)
    if (turns[p]) list[prods[p].lhs] = true;
  std::vector<std::size_t> skippable(nprods, 0);
  for (std::uint32_t p = 1; p < nprods; ++p)
    for (const Symbol s : prods[p].rhs) {
      if (is_terminal(s) || !out.nullable[index_of(s)] || list[index_of(s)]) break;
      ++skippable[p];
    }
  struct Item {
    std::uint32_t prod;
    std::uint32_t dot;
  };
  const auto needed = [&](Item it) {
    if (it.prod != 0 && it.dot <= skippable[it.prod]) return kInf;
    if (it.dot == 1 && turns[it.prod]) return kInf;
    return rest_of(it.prod, it.dot);
  };
  const std::vector<std::vector<std::uint32_t>>& of = cfg.productionsOf;
  std::vector<bool> opened(nnts, false);
  std::vector<std::uint32_t> open;
  std::vector<Item> items;
  out.rows.assign(automaton.states.size(), Bits(nterms));
  for (std::size_t s = 0; s < automaton.states.size(); ++s) {
    // The kernel and what its closure adds.
    items.clear();
    for (const auto& [prod, dot] : automaton.states[s].kernel) items.push_back({prod, dot});
    for (std::size_t at = 0; at < items.size(); ++at) {
      const std::vector<Symbol>& rhs = prods[items[at].prod].rhs;
      if (items[at].dot >= rhs.size() || is_terminal(rhs[items[at].dot])) continue;
      const std::uint32_t nt = index_of(rhs[items[at].dot]);
      if (opened[nt]) continue;
      opened[nt] = true;
      open.push_back(nt);
      for (const std::uint32_t p : of[nt]) items.push_back({p, 0});
    }
    for (const std::uint32_t nt : open) opened[nt] = false;
    open.clear();

    std::uint64_t need = kInf;
    for (const Item it : items) need = std::min(need, needed(it));
    Bits& row = out.rows[s];
    if (need != 0 && need != kInf)
      for (const Item it : items)
        if (needed(it) == need) first_from(it.prod, it.dot, row);
    // An item set aside counts where it shifts one of them.
    const Bits due = row;
    const auto counted = [&](Item it) {
      const std::uint64_t own = needed(it);
      const std::vector<Symbol>& rhs = prods[it.prod].rhs;
      if (own != kInf || it.dot >= rhs.size()) return own;
      const Symbol t = rhs[it.dot];
      return is_terminal(t) && due.test(t) ? rest_of(it.prod, it.dot) : kInf;
    };
    std::uint64_t least = need;
    for (const Item it : items) least = std::min(least, counted(it));
    if (least < need) {
      row.clear();
      for (const Item it : items)
        if (counted(it) == least) row.set(prods[it.prod].rhs[it.dot]);
    }
  }

  // Each state's kernel, and the nonterminals each nonterminal's derivations
  // begin with, cheapest first: the fewest tokens past them complete it.
  const auto capped = [](std::uint64_t n) {
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(n, KernelCost::kNever));
  };
  for (const auto& state : automaton.states) {
    std::vector<std::uint64_t>& kernel = out.kernels.emplace_back();
    for (const auto& [prod, dot] : state.kernel) {
      const std::vector<Symbol>& rhs = prods[prod].rhs;
      const bool reads = dot < rhs.size() && !is_terminal(rhs[dot]);
      kernel.push_back(
          KernelCost{.lhs = dot == 0 ? KernelCost::kAccept : static_cast<std::uint16_t>(prods[prod].lhs),
                     .symbol = reads ? static_cast<std::uint16_t>(index_of(rhs[dot])) : KernelCost::kNone,
                     .dot = static_cast<std::uint16_t>(dot),
                     .rest = capped(rest_of(prod, dot)),
                     .after = capped(dot < rhs.size() ? rest_of(prod, dot + 1) : 0)}
              .packed());
    }
  }
  std::vector<std::vector<std::pair<std::uint32_t, std::uint64_t>>> begins(nnts);
  for (std::uint32_t p = 0; p < nprods; ++p)
    if (const auto& rhs = prods[p].rhs; !rhs.empty() && !is_terminal(rhs[0]) && rest_of(p, 1) != kInf)
      begins[prods[p].lhs].emplace_back(index_of(rhs[0]), rest_of(p, 1));
  std::vector<std::uint64_t> distance(nnts, kInf);
  std::vector<std::uint32_t> reached;
  out.cornerAt.push_back(0);
  for (std::uint32_t nt = 0; nt < nnts; ++nt) {
    distance[nt] = 0;
    reached.push_back(nt);
    cheapest.emplace(0, nt);
    while (!cheapest.empty()) {
      const auto [cost, at] = cheapest.top();
      cheapest.pop();
      if (cost != distance[at]) continue;
      for (const auto& [next, more] : begins[at]) {
        const std::uint64_t to = add(cost, more);
        if (to >= distance[next]) continue;
        if (distance[next] == kInf) reached.push_back(next);
        distance[next] = to;
        cheapest.emplace(to, next);
      }
    }
    std::ranges::sort(reached);
    for (const std::uint32_t corner : reached) {
      if (corner != nt) out.corners.push_back(corner | std::uint32_t{capped(distance[corner])} << 16);
      distance[corner] = kInf;
    }
    reached.clear();
    out.cornerAt.push_back(static_cast<std::uint32_t>(out.corners.size()));
  }
  return out;
}

}  // namespace cursiv::engine::gen
