// Gotos have one authoritative row per source state. A column index names
// those same edges without copying their destinations or guards.
#pragma once

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace cursiv {

struct LrGotoRows {
  // `guard` is the program that picks the landing, plus one.
  struct Edge {
    std::uint16_t nt = 0, to = 0;
    std::uint32_t guard = 0;
    bool operator==(const Edge&) const = default;
  };
  std::vector<std::uint32_t> base{0};
  std::vector<Edge> edges;

  std::span<const Edge> operator[](std::uint32_t s) const {
    return {edges.data() + base[s], base[s + 1] - base[s]};
  }
  void close_row() { base.push_back(static_cast<std::uint32_t>(edges.size())); }
  void push_row(std::span<const Edge> row) {
    edges.insert(edges.end(), row.begin(), row.end());
    close_row();
  }

  // Rows ascend by nonterminal; equal keys keep their order. Each column
  // still has to fit the table's construction limit.
  bool canonicalize(unsigned nnts) {
    std::vector<std::uint32_t> count(nnts, 0);
    for (const Edge& e : edges) ++count[e.nt];
    for (std::uint32_t s = 0; s + 1 < base.size(); ++s) {
      auto row = std::span(edges).subspan(base[s], base[s + 1] - base[s]);
      if (!std::ranges::is_sorted(row, {}, &Edge::nt))
        std::ranges::stable_sort(row, {}, &Edge::nt);
    }
    return std::ranges::all_of(count, [](auto n) { return n <= 0xffffu; });
  }
};

class LrGotoColumns {
 public:
  struct Edge {
    std::uint16_t from, to;
    std::uint32_t guard, index;
  };

  explicit LrGotoColumns(const LrGotoRows& rows, unsigned nnts)
      : rows(rows), base(nnts + 1, 0), links(rows.edges.size()) {
    for (const auto& e : rows.edges) ++base[e.nt + 1];
    for (unsigned nt = 0; nt < nnts; ++nt) base[nt + 1] += base[nt];
    auto at = base;
    for (std::uint32_t s = 0; s + 1 < rows.base.size(); ++s)
      for (std::uint32_t i = rows.base[s]; i < rows.base[s + 1]; ++i)
        links[at[rows.edges[i].nt]++] = {static_cast<std::uint16_t>(s), i};
  }

  // Each column ascends in `from`. Indices remain valid if the caller appends
  // rows; an edge is read through the store after any such reallocation.
  auto operator[](unsigned nt) const {
    return std::span(links.data() + base[nt], base[nt + 1] - base[nt]) |
           std::views::transform([this](const auto& link) {
             const auto e = rows.edges[link.index];
             return Edge{link.from, e.to, e.guard, link.index};
           });
  }

 private:
  struct Link {
    std::uint16_t from;
    std::uint32_t index;
  };
  const LrGotoRows& rows;
  std::vector<std::uint32_t> base;
  std::vector<Link> links;
};

}  // namespace cursiv
