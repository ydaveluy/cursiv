// The chain a nonterminal hands back to its caller, as a fixpoint over the
// productions.
#include "cursiv/engines/lr/gen/lr_build_fold.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/table/LrFusedRow.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

// Which chains a nonterminal hands back, on a record: a directive over a call
// renames it, so each pair (what the record states, what the site adds) gets
// an id before the parse begins.
bool lr_fold_chains(LrBuildTable& A, const LrLowered& low) {
  auto& g = A.cfg;
  const unsigned nN = g.nnts;
  const unsigned nP = g.nprods;
  // A cycle of unit productions makes the unit step diverge: a faulted
  // lowering skips it.
  const bool sound = !low.fault;
  LrLowProd* const wprods = g.prods.row(0, nP);

  A.soleRow.assign(nN, kNoRow);
  std::vector<DynSet> rec(nN);
  // A turn or a leaf without a row can leave rec incomplete through a unit.
  std::vector<bool> unsafe(nN, false);
  for (unsigned p = 0; p < nP; ++p)
    if (g.prodBuild[p].turnNode != kNoRow) unsafe[wprods[p].lhs] = true;
  const auto mark = [&](unsigned B, unsigned e) {
    if (rec[B].test(e)) return false;
    rec[B].set(e);
    return true;
  };
  // The callback may grow this row: address it afresh at every word.
  const auto each = [&](unsigned B, auto&& f) {
    for (std::size_t k = 0; k < rec[B].w.size(); ++k) {
      std::uint64_t bits = rec[B].w[k];
      while (bits) {
        f(static_cast<ElementId>(
            (k << 6) | static_cast<unsigned>(std::countr_zero(bits))));
        bits &= bits - 1;
      }
    }
  };
  // One id per run, grown one chain at a time: `fold` names the pair (what
  // the record states so far, what the site states over it).
  bool full = false;
  // The pairs already minted, by their halves.
  std::unordered_map<std::uint32_t, std::uint16_t> fx;
  const auto fold = [&](ElementId r, ElementId over) -> ElementId {
    if (over == kNoRow || r == kNoRow) return over;
    const std::uint32_t key = (std::uint32_t{r} << 16) | over;
    if (const auto it = fx.find(key); it != fx.end())
      return static_cast<ElementId>(A.writes.nchains + it->second);
    if (A.writes.nchains + A.nfused + 1u >= kNoElement) {
      full = true;
      return over;
    }
    const ElementId id = static_cast<ElementId>(A.writes.nchains + A.nfused);
    fx.emplace(key, A.nfused);
    A.fused[A.nfused++] = LrFusedRow{r, over};
    return id;
  };

  for (bool grew = true; grew && !full;) {
    grew = false;
    for (unsigned p = 0; p < nP && !full; ++p) {
      const LrLowProd& pr = wprods[p];
      // A call site states over the run the callee came back with.
      for (unsigned i = 0; i < pr.len; ++i) {
        const std::uint16_t site = g.row_at(pr, i);
        if (site == kNoRow) continue;
        const std::uint16_t sym = g.sym_at(pr, i);
        if (is_term(sym))
          fold(g.terms[sym].element, site);
        else
          each(nt_index(sym), [&](ElementId r) { fold(r, site); });
      }
      // A chain of its own also stands alone where no position returns one.
      if (pr.node != kNoRow) {
        if (mark(pr.lhs, pr.node)) grew = true;
        if (!sound) continue;
      }
      if (pr.len != 1) continue;
      const std::uint16_t site = g.row_at(pr, 0);
      const std::uint16_t sym = g.sym_at(pr, 0);
      const bool risk = is_term(sym)
                            ? g.terms[sym].element == kNoRow &&
                                  ((pr.node == kNoRow && site == kNoRow) ||
                                   (pr.node != kNoRow && site != kNoRow))
                            : unsafe[nt_index(sym)];
      if (risk && !unsafe[pr.lhs]) {
        unsafe[pr.lhs] = true;
        grew = true;
      }
      // A bare pass-through is a set union, with no chain to compose.
      if (pr.node == kNoRow && site == kNoRow && !is_term(sym)) {
        if (rec[pr.lhs].add(rec[nt_index(sym)])) grew = true;
        continue;
      }
      const auto onto = [&](ElementId r) {
        if (pr.node != kNoRow && r == kNoRow) return;
        ElementId back = site == kNoRow ? r : fold(r, site);
        if (pr.node != kNoRow) back = fold(back, pr.node);
        if (back != kNoRow && mark(pr.lhs, back)) grew = true;
      };
      if (is_term(sym)) {
        onto(g.terms[sym].element);
      } else {
        bool any = false;
        each(nt_index(sym), [&](ElementId r) {
          any = true;
          onto(r);
        });
        if (pr.node == kNoRow && !any && mark(pr.lhs, site)) grew = true;
      }
    }
  }
  if (full) {
    A.cap_hit("its rules expand to more than 65 535 elements");
    return false;
  }

  // Which reduces ever meet a chain: only a production that lays nothing and
  // whose one symbol brings nothing back reads no row.
  for (unsigned p = 0; p < nP; ++p) {
    LrLowProd& pr = wprods[p];
    const std::uint16_t sym = pr.len == 1 ? g.sym_at(pr, 0) : 0;
    pr.folds = pr.node != kNoRow ||
               (pr.len == 1 && (g.row_at(pr, 0) != kNoRow ||
                                (is_term(sym) ? g.terms[sym].element != kNoRow
                                              : rec[nt_index(sym)].any())));
  }
  if (sound)
    for (unsigned B = 0; B < nN; ++B)
      if (!unsafe[B] && !rec[B].more_than_one())
        each(B, [&](ElementId r) { A.soleRow[B] = r; });
  return true;
}

void lr_derive_reductions(const LrBuildTable& a, LrDerivedRows& rows) {
  rows.reductions.clear();
  rows.reduceNames.clear();
  rows.reductions.reserve(a.cfg.nprods);
  const auto named_row = [&](ElementId held, ElementId site) {
    if (held == kNoElement) return site;
    for (unsigned i = 0; i < a.nfused; ++i)
      if (a.fused[i].node == held && a.fused[i].site == site)
        return static_cast<ElementId>(a.writes.nchains + i);
    return site;
  };
  std::vector<bool> opens(a.cfg.nnts, false);
  for (unsigned i = 0; i < a.cfg.nprods; ++i)
    if (const auto b = a.cfg.nt_after(i, 0); b != kNoRow) opens[b] = true;
  // A section's entry opens its grammar's text, as the document's does.
  for (std::size_t k = 1; k < a.cfg.cfgHome.size(); ++k)
    if (a.cfg.cfgHome[k] == k) opens[a.cfg.cfgEntry[k]] = true;
  for (unsigned i = 0; i < a.cfg.nprods; ++i) {
    const auto& p = a.cfg.prods[i];
    LrReducePlan plan{.names = static_cast<std::uint32_t>(rows.reduceNames.size())};
    for (unsigned k = 0; k < p.len; ++k) {
      const auto site = a.cfg.row_at(p, k);
      if (site == kNoRow) continue;
      const auto sym = a.cfg.sym_at(p, k);
      ElementId held = kNoRow;
      if (is_term(sym))
        held = a.cfg.terms[sym].element;
      else if (nt_index(sym) < a.soleRow.size())
        held = a.soleRow[nt_index(sym)];
      const bool fold = !is_term(sym) && held == kNoRow;
      const auto row = fold ? site : named_row(held, site);
      rows.reduceNames.push_back({static_cast<std::uint16_t>(k), row, fold});
      ++plan.count;
    }
    if (p.len == 1 && p.node == kNoRow && plan.count == 0)
      plan.form = LrReduceForm::through;
    else if (p.node != kNoRow)
      plan.form = p.len == 1 ? LrReduceForm::foldClose : LrReduceForm::close;
    else if (p.folds)
      plan.form = LrReduceForm::carry;
    plan.flushes =
        (p.len == 0 && opens[p.lhs]) ||
        (a.cfg.prodBuild[i].endOnly && a.cfg.prodBuild[i].tailCfg != 0);
    if (plan.flushes && plan.form == LrReduceForm::through)
      plan.form = LrReduceForm::carry;
    plan.turnRow = a.cfg.prodBuild[i].turnNode;
    rows.reductions.push_back(plan);
  }
  // The automaton may substitute any production with the same reduction key.
  std::map<std::vector<std::uint32_t>, std::pair<unsigned, bool>> classes;
  std::vector<std::uint32_t> key;
  for (unsigned p = 0; p < a.cfg.nprods; ++p) {
    lr_reduce_key(a.cfg, p, key);
    auto& [leader, mixed] = classes.try_emplace(key, p, false).first->second;
    const auto& one = rows.reductions[leader];
    const auto& plan = rows.reductions[p];
    for (unsigned k = 0; k < plan.count; ++k)
      if (rows.reduceNames[one.names + k] != rows.reduceNames[plan.names + k])
        mixed = true;
  }
  for (unsigned p = 0; p < a.cfg.nprods; ++p) {
    lr_reduce_key(a.cfg, p, key);
    if (!classes.at(key).second) continue;
    const auto& plan = rows.reductions[p];
    for (unsigned k = 0; k < plan.count; ++k) {
      auto& n = rows.reduceNames[plan.names + k];
      if (!is_term(a.cfg.sym_at(a.cfg.prods[p], n.slot))) {
        n.row = a.cfg.row_at(a.cfg.prods[p], n.slot);
        n.fold = true;
      }
    }
  }
}

}  // namespace cursiv
