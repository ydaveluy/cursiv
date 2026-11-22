// The vertices of a graph that lie on a cycle, by an iterative Tarjan. The
// graph is in compressed rows: the edges of vertex `v` are `edges[starts[v]]`
// up to `edges[starts[v + 1]]`, and an edge to no vertex is none.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cursiv::engine {

// Visit each component after every component it depends on.
template <class Visit>
constexpr void strong_components(std::span<const std::uint32_t> starts,
                                 std::span<const std::uint32_t> edges, Visit visit) {
  constexpr auto none = ~std::uint32_t{};
  const std::size_t n = starts.empty() ? 0 : starts.size() - 1;
  std::vector<std::uint32_t> found(n, none), low(n, none);
  std::vector<std::uint32_t> stack, path, next;
  std::uint32_t clock = 0;
  const auto enter = [&](std::uint32_t v) {
    found[v] = low[v] = clock++;
    stack.push_back(v);
    path.push_back(v);
    next.push_back(starts[v]);
  };
  for (std::uint32_t root = 0; root < n; ++root) {
    if (found[root] != none) continue;
    enter(root);
    while (!path.empty()) {
      const std::uint32_t v = path.back();
      if (next.back() != starts[v + 1]) {
        const std::uint32_t w = edges[next.back()++];
        if (w >= n) continue;
        if (found[w] == none)
          enter(w);
        else if (low[w] != none && found[w] < low[v])
          low[v] = found[w];
        continue;
      }
      path.pop_back();
      next.pop_back();
      if (!path.empty() && low[v] < low[path.back()]) low[path.back()] = low[v];
      if (low[v] != found[v]) continue;
      auto first = stack.size();
      do { low[stack[--first]] = none; } while (stack[first] != v);
      visit(std::span<const std::uint32_t>(stack).subspan(first));
      stack.resize(first);
    }
  }
}

template <std::size_t N>
struct StrongComponents {
  std::array<bool, N> cyclic{};

  static constexpr StrongComponents build(
      std::span<const std::uint32_t> starts, std::span<const std::uint32_t> edges) {
    StrongComponents out;
    strong_components(starts, edges, [&](auto group) {
      for (const auto v : group) {
        out.cyclic[v] = group.size() > 1;
        for (auto k = starts[v]; !out.cyclic[v] && k < starts[v + 1]; ++k)
          out.cyclic[v] = edges[k] == v;
      }
    });
    return out;
  }
};

}  // namespace cursiv::engine
