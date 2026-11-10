// The bytes a terminal reads as its own words' that the pass class of a word
// it refuses at its head leaves out: read where its body begins and again past
// the word, they split the word off as a token of its own.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/gen/lex_prefix_overlap.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

// Whether each refusal stands at its token's head: its terminal's entry
// reaches it reading nothing, the refusals in front of it included.
inline std::vector<bool> lex_guard_heads(const LexNfaPool& n,
                                         const std::vector<int>& startOf) {
  std::vector<bool> head(n.guards.size());
  std::vector<std::uint16_t> terms;
  for (const LexNfaGuard& gd : n.guards)
    if (std::ranges::find(terms, gd.term) == terms.end())
      terms.push_back(gd.term);
  std::vector<char> reached(n.s.size());
  std::vector<int> todo;
  for (const std::uint16_t term : terms) {
    std::ranges::fill(reached, 0);
    todo.assign(1, startOf[term]);
    while (!todo.empty()) {
      const int x = todo.back();
      todo.pop_back();
      if (x < 0 || reached[static_cast<std::size_t>(x)]) continue;
      reached[static_cast<std::size_t>(x)] = 1;
      const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
      const auto k = static_cast<std::size_t>(st.guard);
      if (st.guard >= 0 && n.guards[k].term == term) head[k] = true;
      if (st.set >= 0) continue;
      todo.push_back(st.t1);
      todo.push_back(st.t2);
      if (st.guard >= 0) todo.push_back(n.guards[k].out);
    }
  }
  return head;
}

// Each head refusal whose word leaves such bytes, handed to `miss` with them.
template <class Cfg, class Miss>
void lex_boundary_misses(const LexNfaPool& n, const Cfg& g,
                         const std::vector<bool>& head, Miss&& miss) {
  const ByteSet* const sets = g.sets.data();
  for (std::size_t k = 0; k < n.guards.size(); ++k) {
    const LexNfaGuard& gd = n.guards[k];
    const LexNode e = g.lex[gd.node];
    if (!head[k] || e.a == kNoSet) continue;
    std::vector<int> cur = n.eclose(gd.out);
    const ByteSet begins = lex_subset_reads(n, sets, cur);
    for (unsigned i = 0; i < e.min && !cur.empty(); ++i)
      cur = lex_nfa_step_any(n, sets, cur, sets[g.lexChild[e.b + i]]);
    ByteSet out = lex_subset_reads(n, sets, cur) & begins;
    out.andnot(sets[e.a]).andnot(lex_decode_bytes(g, e.a));
    if (out.any()) miss(k, out);
  }
}

}  // namespace cursiv::engine
