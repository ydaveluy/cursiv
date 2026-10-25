// FIRST and nullability shared by lowering and automaton construction.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/lr_nullable.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// FIRST over the nullable prefix of a body, and the nullable set.
struct LrFirstSets {
  const LrBuildCfg* g = nullptr;
  std::vector<std::uint8_t> nullable;
  std::vector<DynSet> first;

  void add(DynSet& set, std::uint16_t sym) const {
    if (is_term(sym))
      set.set(sym);
    else
      set |= first[nt_index(sym)];
  }
  // What a run of positions may start with, and `la` where it may be empty.
  DynSet after(unsigned prod, unsigned from, const DynSet& la) const {
    const auto& p = g->prods[prod];
    DynSet s;
    for (unsigned i = from; i < p.len; ++i) {
      const std::uint16_t y = g->sym_at(p, i);
      add(s, y);
      if (is_term(y) || !nullable[nt_index(y)]) return s;
    }
    if (g->prodBuild[prod].endOnly)
      s.set(kEofSymbol);
    else
      s |= la;
    return s;
  }
};

// FIRST reads a nullable prefix; LAST reads the same fixpoint from the tail.
// Only FIRST includes the end assertion of an otherwise empty production.
template <bool Last = false>
inline std::vector<DynSet> lr_first(const LrBuildCfg& g,
                                    std::span<const std::uint8_t> nullable) {
  std::vector<DynSet> first(g.nnts);
  for (bool changed = true; changed;) {
    changed = false;
    for (unsigned p = 0; p < g.nprods; ++p) {
      const auto& q = g.prods[p];
      DynSet& into = first[q.lhs];
      unsigned i = 0;
      for (; i < q.len; ++i) {
        const auto sym = g.sym_at(q, Last ? q.len - 1u - i : i);
        if (is_term(sym)) {
          changed = !into.test(sym) || changed;
          into.set(sym);
          break;
        }
        changed = into.add(first[nt_index(sym)]) || changed;
        if (!nullable[nt_index(sym)]) break;
      }
      if constexpr (!Last)
        if (i == q.len && g.prodBuild[p].endOnly) {
          changed = !into.test(kEofSymbol) || changed;
          into.set(kEofSymbol);
        }
    }
  }
  return first;
}

// FOLLOW reads the same suffixes for conflict ranking.
inline std::vector<DynSet> lr_follow(
    const LrBuildCfg& g, std::span<const std::uint8_t> nullable,
    std::span<const DynSet> first) {
  std::vector<DynSet> follow(g.nnts);
  follow[0].set(kEofSymbol);
  for (bool changed = true; changed;) {
    changed = false;
    for (unsigned p = 0; p < g.nprods; ++p) {
      const auto& q = g.prods[p];
      DynSet suffix;
      if (g.prodBuild[p].endOnly)
        suffix.set(kEofSymbol);
      else
        suffix = follow[q.lhs];
      for (unsigned i = q.len; i != 0;) {
        const auto sym = g.sym_at(q, --i);
        if (is_term(sym)) {
          suffix.w.clear();
          suffix.set(sym);
        } else {
          const auto nt = nt_index(sym);
          changed = follow[nt].add(suffix) || changed;
          if (!nullable[nt]) suffix.w.clear();
          suffix |= first[nt];
        }
      }
    }
  }
  return follow;
}

// The terminals each terminal may be followed by: what may start the rest of a
// body after a symbol that ends in it, or what follows the rule where nothing
// of the body is left to read.
inline std::vector<DynSet> lr_terminal_follow(const LrBuildCfg& g,
                                              const LrFirstSets& fs) {
  const std::vector<DynSet> follow = lr_follow(g, fs.nullable, fs.first);
  const std::vector<DynSet> last = lr_first<true>(g, fs.nullable);
  std::vector<DynSet> out(g.nterms);
  for (unsigned p = 0; p < g.nprods; ++p) {
    const auto& q = g.prods[p];
    DynSet next = follow[q.lhs];
    for (unsigned i = q.len; i-- > 0;) {
      const auto sym = g.sym_at(q, i);
      if (is_term(sym)) {
        if (sym != kEofSymbol) out[sym] |= next;
      } else {
        for (unsigned b = 1; b < g.nterms; ++b)
          if (last[nt_index(sym)].test(b)) out[b] |= next;
      }
      if (is_term(sym) || !fs.nullable[nt_index(sym)]) next.w.clear();
      fs.add(next, sym);
    }
  }
  return out;
}

inline LrFirstSets lr_first_sets(const LrBuildCfg& g) {
  LrFirstSets fs{&g, lr_nullable(g, true), {}};
  fs.first = lr_first(g, fs.nullable);
  return fs;
}

}  // namespace cursiv
