// The context-free core while it is lowered: the cells a frozen table keeps,
// and beside them what only construction reads.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/LrLowSlot.hpp"
#include "cursiv/engines/lr/gen/LrNtInfo.hpp"
#include "cursiv/engines/lr/gen/LrProdBuild.hpp"
#include "cursiv/engines/lr/table/LrLowFacts.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

struct LrBuildCfg : LrLowFacts, LowerPools {
  // What only construction reads: never frozen, so no reader takes a verdict
  // at its default for an answer.
  std::uint32_t nslots = 0;
  std::uint16_t entryNt = 0;
  std::uint16_t startProd = 0;  // $accept -> entry ; reducing it is acceptance
  std::uint16_t auxCount = 0;

  Vec<LrNtInfo> nts{};
  Vec<std::uint32_t> priorityNext{};
  Vec<LrLowProd> prods{};
  Vec<LrLowSlot> slots{};
  // For a section's own configuration, its entry's reading.
  Vec<std::uint16_t> cfgEntry{};

  Vec<LrProdBuild> prodBuild{};
  // The tokens each commit exit is refused on; a production's `stop` names one.
  std::vector<DynSet> stopCuts{};
  Vec<std::uint32_t> decisions{};
  // The left recursion each nonterminal stands in, `kNoRow` outside any.
  Vec<std::uint16_t> leftLoop{};
  // A position's symbol, the configuration the boundary in front of it is
  // read under, and the condition it is passed under plus one
  // (`lr_guard_positions`).
  Vec<std::uint16_t> slotSym{};
  Vec<std::uint8_t> slotCfg{};
  Vec<std::uint8_t> slotGuard{};

  std::uint16_t sym_at(const LrLowProd& p, unsigned i) const {
    return slotSym[p.at + i];
  }
  std::uint16_t row_at(const LrLowProd& p, unsigned i) const {
    return slots[p.at + i].row;
  }
  // The nonterminal after the dot of production `p`'s item at `dot`, `kNoRow`
  // where none stands there.
  std::uint16_t nt_after(unsigned p, unsigned dot) const {
    const LrLowProd& q = prods[p];
    return dot < q.len && !is_term(sym_at(q, dot)) ? nt_index(sym_at(q, dot))
                                                  : std::uint16_t{kNoRow};
  }
  // Each nonterminal `p` reads, `f(position, nonterminal)`.
  template <class F>
  void each_nt(const LrLowProd& p, F&& f) const {
    for (unsigned i = 0; i < p.len; ++i)
      if (const std::uint16_t y = sym_at(p, i); !is_term(y)) f(i, nt_index(y));
  }
  // `i == len` is the boundary after the last symbol, which the production
  // states itself.
  std::uint8_t cfg_at(std::uint16_t p, unsigned i) const {
    const LrLowProd& q = prods[p];
    return i == q.len ? prodBuild[p].tailCfg : slotCfg[q.at + i];
  }
  std::uint8_t home_at(std::uint8_t k) const {
    return k < cfgHome.size() ? cfgHome[k] : std::uint8_t{0};
  }
  // `i == len` is the reduction's own guard.
  std::uint8_t guard_at(const LrLowProd& p, unsigned i) const {
    return i == p.len ? p.gap : slotGuard[p.at + i];
  }
  // `leftLoop`, read past its end as `kNoRow`.
  std::uint16_t loop_of(unsigned nt) const {
    return nt < leftLoop.size() ? leftLoop[nt] : std::uint16_t{kNoRow};
  }
  // Whether production `p` goes on from a left operand of its own recursion.
  bool turns(unsigned p) const {
    const std::uint16_t lhs = prods[p].lhs, b = nt_after(p, 0);
    return b != kNoRow && loop_of(lhs) != kNoRow && loop_of(b) == loop_of(lhs);
  }

  std::string_view nt_name(std::uint16_t n) const {
    return name_view(nts[n].nameOff, nts[n].nameLen);
  }
  // `lhs -> a b c`, as a diagnostic spells a production.
  std::string prod_text(const LrLowProd& p) const {
    std::string out(nt_name(p.lhs));
    out += " ->";
    for (unsigned i = 0; i < p.len; ++i) {
      const std::uint16_t y = sym_at(p, i);
      out += ' ';
      out += is_term(y) ? term_name(y) : nt_name(nt_index(y));
    }
    return out;
  }
};

}  // namespace cursiv
