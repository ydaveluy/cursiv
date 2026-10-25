// Items bucketed on a key by counting, each bucket in item order.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

// Classes numbered at their first live state; `pick` keeps that state's id.
// The returned map is indexed by class, not state.
template <class Index = std::uint32_t, class Group, class Representative>
std::vector<Index> lr_representatives(
    const std::vector<Group>& group, std::vector<Representative>& pick,
    std::span<const std::uint8_t> live = {}) {
  constexpr Index none = std::numeric_limits<Index>::max();
  std::size_t n = 0;
  for (const Group id : group) n = std::max(n, std::size_t{id} + 1u);
  std::vector<Index> number(n, none);
  for (std::size_t s = 0; s < group.size(); ++s) {
    if (!live.empty() && !live[s]) continue;
    Index& id = number[group[s]];
    if (id != none) continue;
    id = static_cast<Index>(pick.size());
    pick.push_back(static_cast<Representative>(s));
  }
  return number;
}

template <class Item = std::uint32_t>
struct LrBuckets {
  std::vector<std::uint32_t> base;
  std::vector<Item> item;

  LrBuckets() = default;
  // Items `0..n` and `key(i) < nkeys`.
  LrBuckets(std::uint32_t nkeys, std::uint32_t n, auto&& key)
      : base(nkeys + 1, 0), item(n) {
    for (std::uint32_t i = 0; i < n; ++i) ++base[key(i) + 1];
    for (std::uint32_t k = 0; k < nkeys; ++k) base[k + 1] += base[k];
    std::vector<std::uint32_t> at(base.begin(), base.end() - 1);
    for (std::uint32_t i = 0; i < n; ++i)
      item[at[key(i)]++] = static_cast<Item>(i);
  }

  std::span<const Item> of(std::uint32_t k) const {
    return {item.data() + base[k], base[k + 1] - base[k]};
  }
};

// Every production by its left-hand side.
inline LrBuckets<std::uint16_t> lr_by_lhs(const LrBuildCfg& c) {
  return {c.nnts, c.nprods, [&](std::uint32_t k) { return c.prods[k].lhs; }};
}

// The nonterminals the start reaches.
inline std::vector<bool> lr_reachable(const LrBuildCfg& c,
                                      const LrBuckets<std::uint16_t>& ix) {
  std::vector<bool> live(c.nnts, false);
  std::vector<std::uint16_t> work{0};
  live[0] = true;
  while (!work.empty()) {
    const std::uint16_t lhs = work.back();
    work.pop_back();
    for (const std::uint16_t k : ix.of(lhs)) {
      const LrLowProd& q = c.prods[k];
      for (unsigned i = 0; i < q.len; ++i) {
        const std::uint16_t sym = c.sym_at(q, i);
        if (is_term(sym)) continue;
        const std::uint16_t nt = nt_index(sym);
        if (!live[nt]) {
          live[nt] = true;
          work.push_back(nt);
        }
      }
    }
  }
  return live;
}

}  // namespace cursiv
