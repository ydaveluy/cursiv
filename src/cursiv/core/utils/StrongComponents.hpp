// Iterative Tarjan over a graph in compressed rows: the edges of vertex `v`
// are `edges[starts[v]]` up to `edges[starts[v + 1]]`, and an edge to no
// vertex is none. Components are numbered in the order they close.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace cursiv {

// `N`, the vertex count where it is a constant: the answer is then arrays,
// and a constant expression may hold it.
template <std::size_t N = std::dynamic_extent>
struct StrongComponents {
  template <class T>
  using Row = std::conditional_t<N == std::dynamic_extent, std::vector<T>,
                                 std::array<T, N>>;
  Row<std::uint32_t> component{};
  Row<bool> cyclic{};

  static constexpr StrongComponents build(
      std::span<const std::uint32_t> starts,
      std::span<const std::uint32_t> edges) {
    constexpr auto none = ~std::uint32_t{};
    const std::size_t n = starts.empty() ? 0 : starts.size() - 1;
    StrongComponents out;
    Row<std::uint32_t> found{}, low{}, stack{}, path{}, next{};
    Row<bool> active{};
    if constexpr (N == std::dynamic_extent) {
      for (auto* row : {&out.component, &found, &low, &stack, &path, &next})
        row->assign(n, 0);
      out.cyclic.assign(n, false);
      active.assign(n, false);
    }
    for (std::size_t v = 0; v < n; ++v) found[v] = none;
    std::uint32_t clock = 0, used = 0, depth = 0, closed = 0;
    const auto enter = [&](std::uint32_t v) {
      found[v] = low[v] = clock++;
      active[v] = true;
      stack[used++] = v;
      path[depth] = v;
      next[depth++] = starts[v];
    };
    for (std::uint32_t root = 0; root < n; ++root) {
      if (found[root] != none) continue;
      enter(root);
      while (depth != 0) {
        const auto v = path[depth - 1];
        if (next[depth - 1] != starts[v + 1]) {
          const auto w = edges[next[depth - 1]++];
          if (w >= n) continue;
          if (w == v) out.cyclic[v] = true;
          if (found[w] == none)
            enter(w);
          else if (active[w] && found[w] < low[v])
            low[v] = found[w];
          continue;
        }
        --depth;
        if (depth != 0 && low[v] < low[path[depth - 1]])
          low[path[depth - 1]] = low[v];
        if (low[v] != found[v]) continue;
        const auto end = used;
        std::uint32_t w;
        do {
          w = stack[--used];
          active[w] = false;
          out.component[w] = closed;
        } while (w != v);
        ++closed;
        if (end - used > 1)
          for (auto i = used; i < end; ++i) out.cyclic[stack[i]] = true;
      }
    }
    return out;
  }

  // The same over adjacency lists.
  template <class T>
  static StrongComponents of(const std::vector<std::vector<T>>& adj)
    requires(N == std::dynamic_extent)
  {
    std::vector<std::uint32_t> starts{0}, edges;
    for (const auto& out : adj) {
      edges.insert(edges.end(), out.begin(), out.end());
      starts.push_back(static_cast<std::uint32_t>(edges.size()));
    }
    return build(starts, edges);
  }

  // The shortest walk of one edge or more from `from` to `to` inside their
  // component, `to` left out; `next(u, f)` calls `f` on each successor of `u`.
  template <class Next>
  std::vector<std::uint32_t> walk(std::uint32_t from, std::uint32_t to,
                                  Next&& next) const
    requires(N == std::dynamic_extent)
  {
    constexpr auto none = ~std::uint32_t{};
    const auto n = static_cast<std::uint32_t>(component.size());
    std::vector<std::uint32_t> back(n, none), queue{from};
    auto last = none;
    for (std::size_t i = 0; i < queue.size() && last == none; ++i) {
      const auto u = queue[i];
      next(u, [&](std::uint32_t w) {
        if (last != none || w >= n || component[w] != component[from]) return;
        if (w == to) {
          last = u;
        } else if (w != from && back[w] == none) {
          back[w] = u;
          queue.push_back(w);
        }
      });
    }
    std::vector<std::uint32_t> path;
    if (last == none) return path;
    for (auto u = last; u != from; u = back[u]) path.push_back(u);
    path.push_back(from);
    std::ranges::reverse(path);
    return path;
  }
};

}  // namespace cursiv
