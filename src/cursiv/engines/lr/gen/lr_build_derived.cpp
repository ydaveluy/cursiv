// The tables a reader takes over a built one, derived once the automaton has
// stopped moving.
#include "cursiv/engines/lr/gen/lr_build_derived.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrLexView.hpp"
#include "cursiv/engines/lr/gen/LrLowProd.hpp"
#include "cursiv/engines/lr/gen/lr_build_fold.hpp"
#include "cursiv/engines/lr/gen/lr_build_lex.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/gen/lr_nullable.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/gen/LexFirstByte.hpp"
#include "cursiv/engines/shared/gen/LexOwedRows.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

namespace {

struct LrDeriving {
  const LrBuildTable& a;
  const std::span<const ElementId> sites;

  void repair_tables(LrDerivedRows& rows) const {
    const std::size_t n = a.ndfa;
    LexOwedRows owed = lex_owed_rows(
        LexOwedView{LrLexView{a}, a.ndfa, rows.typoSuccBase, rows.typoSucc});
    rows.lexBodies = std::move(owed.bodies);
    rows.lexOwed = std::move(owed.owed);
    rows.lexOpeners = std::move(owed.openers);
    const auto& into = owed.into;
    std::vector<DynSet> reach(n), after(a.cfg.nterms), want(a.nmodes);
    for (std::size_t s = 0; s < n; ++s)
      reach[s] = a.dfa_acc_set(static_cast<std::uint16_t>(s));
    std::vector<std::uint8_t> queued(n, 0);
    std::vector<std::uint16_t> todo;
    const auto queue = [&](std::uint16_t s) {
      if (queued[s] == 0) {
        queued[s] = 1;
        todo.push_back(s);
      }
    };
    for (std::size_t s = 0; s < n; ++s)
      if (reach[s].any()) queue(static_cast<std::uint16_t>(s));
    while (!todo.empty()) {
      const auto s = todo.back();
      todo.pop_back();
      queued[s] = 0;
      for (const auto from : into[s])
        if (reach[from].add(reach[s])) queue(from);
    }

    for (std::uint32_t s = 0; s < a.nstates; ++s) {
      want[a.mode[s]] |= a.follow[s];
      for (const LrAction& x : a.action_row(s)) {
        // The runtime reader visits only action cells in this state's follow.
        if (!a.follow[s].test(x.term)) continue;
        if (x.kind == LrActionKind::shift) after[x.term] |= a.follow[x.arg];
        // These are the destinations emitted in guardOps after the conditions.
        if (x.kind == LrActionKind::guard)
          for (const auto to : a.guards[x.arg].to)
            if (to != 0xffffu) after[x.term] |= a.follow[to];
      }
    }
    rows.typoReach = SetPool(reach.data(), static_cast<unsigned>(n));
    rows.after = SetPool(after.data(), a.cfg.nterms);
    rows.modeWant = SetPool(want.data(), a.nmodes);
  }

  ElementId canonical(ElementId row) const {
    return row == kNoElement ? kNoElement : sites[row];
  }

  // Unvisited is not a bare caller: once reached, a missing member agrees or
  // disagrees like a named one, and disagreement sticks.
  struct Agreement {
    enum Kind : std::uint8_t { unseen, unique, ambiguous } kind = unseen;
    ElementId site = kNoElement;

    bool add(const Agreement& from) {
      if (from.kind == unseen || kind == ambiguous) return false;
      if (kind == unseen) {
        *this = from;
        return true;
      }
      if (from.kind == unique && from.site == site) return false;
      kind = ambiguous;
      site = kNoElement;
      return true;
    }
    ElementId agreed() const { return kind == unique ? site : kNoElement; }
  };

  Agreement at(ElementId row) const {
    return {Agreement::unique, canonical(row)};
  }

  // The nonterminals a laid row's record may reduce to. A production that may
  // start with a nullable symbol leaves its row unnamed: its record sits on
  // an empty frame, and the goto would leave from it.
  void row_lhs(LrDerivedRows& rows, std::size_t nrows) const {
    const auto& c = a.cfg;
    const std::vector<std::uint8_t>& nullable = a.nullable;
    std::vector<std::vector<std::uint16_t>> named(nrows);
    std::vector<std::uint8_t> refused(nrows, 0);
    for (unsigned p = 0; p < c.nprods; ++p) {
      const auto& q = c.prods[p];
      if (q.node == kNoRow || q.node >= nrows) continue;
      const auto first = q.len == 0 ? 0u : c.sym_at(q, 0);
      if (q.len != 0 && !is_term(first) && nullable[nt_index(first)])
        refused[q.node] = 1;
      auto& lhs = named[q.node];
      if (std::find(lhs.begin(), lhs.end(), q.lhs) == lhs.end())
        lhs.push_back(q.lhs);
    }
    for (unsigned k = 0; k < a.nfused; ++k) {
      const auto node = a.fused[k].node;
      named[a.writes.nchains + k] = named[node];
      refused[a.writes.nchains + k] = refused[node];
    }
    for (std::size_t r = 0; r < nrows; ++r) {
      rows.rowLhsBase.push_back(static_cast<std::uint32_t>(rows.rowLhs.size()));
      if (refused[r]) continue;
      for (const auto nt : named[r]) rows.rowLhs.push_back(nt);
    }
    rows.rowLhsBase.push_back(static_cast<std::uint32_t>(rows.rowLhs.size()));
  }

  void repair_states(LrDerivedRows& rows) const {
    rows.soleReduce.assign(a.nstates, kLrNoProd);
    rows.readsToken.resize(a.nstates);
    for (std::uint32_t s = 0; s < a.nstates; ++s) {
      auto& sole = rows.soleReduce[s];
      bool multiple = false;
      bool reads = a.actionCount[s] == 0;
      for (const LrAction& x : a.action_row(s)) {
        reads = reads || lr_takes_token(x.kind);
        if (x.kind != LrActionKind::reduce) continue;
        if (sole == kLrNoProd)
          sole = x.arg;
        else if (sole != x.arg)
          multiple = true;
      }
      if (multiple) sole = kLrNoProd;
      rows.readsToken[s] = reads;
    }
  }

  // One class per distinct row of gotos and held conditions: what a reduction
  // reads of the frame under the symbols it pops.
  void goto_classes(LrDerivedRows& rows) const {
    std::map<std::vector<std::uint32_t>, std::uint16_t> classes;
    std::vector<std::uint32_t> row;
    rows.gotoClass.resize(a.nstates);
    for (std::uint32_t s = 0; s < a.nstates; ++s) {
      row.clear();
      for (const auto e : a.gotoRows[s])
        row.insert(row.end(), {e.nt, e.to, e.guard});
      if (s < a.tagKnown.size())
        for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
          row.push_back(!a.tagKnown[s].test(k) || a.tagTruth[s].test(k));
      rows.gotoClass[s] = classes
                              .try_emplace(row, static_cast<std::uint16_t>(
                                                    classes.size()))
                              .first->second;
    }
  }

  // A call without a row inherits its caller's agreement; an explicit row
  // answers for itself.
  std::vector<Agreement> rule_sites() const {
    const auto& c = a.cfg;
    std::vector<Agreement> found(c.nnts);
    found[0] = at(kNoElement);
    for (bool moved = true; moved;) {
      moved = false;
      for (std::uint16_t k = 0; k < c.nprods; ++k) {
        const LrLowProd& q = c.prods[k];
        for (unsigned i = 0; i < q.len; ++i) {
          const std::uint16_t x = c.sym_at(q, i);
          if (is_term(x)) continue;
          const unsigned into = nt_index(x);
          const ElementId row = c.row_at(q, i);
          const Agreement source = row != kNoElement ? at(row)
              : q.len == 1 && q.node != kNoRow ? at(q.node) : found[q.lhs];
          moved = found[into].add(source) || moved;
        }
      }
    }
    return found;
  }

  struct TerminalSites {
    std::vector<Agreement> own;
    std::vector<Agreement> open;
  };

  TerminalSites terminal_sites() const {
    const auto& c = a.cfg;
    const auto callers = rule_sites();
    // Skip nullable calls to reach the first direct terminal. An EOF-only
    // reduction makes no call nullable here.
    const auto nullable = lr_nullable(c, true);
    TerminalSites found{std::vector<Agreement>(c.nterms),
                        std::vector<Agreement>(c.nterms)};
    for (std::uint16_t k = 0; k < c.nprods; ++k) {
      const auto& q = c.prods[k];
      for (unsigned i = 0; i < q.len; ++i) {
        const auto sym = c.sym_at(q, i);
        if (is_term(sym)) {
          found.own[sym].add(at(c.row_at(q, i)));
          // An unknown caller must not borrow another head's member.
          found.open[sym].add(callers[q.lhs].kind == Agreement::unseen
                                  ? at(kNoElement)
                                  : callers[q.lhs]);
          break;
        }
        if (!nullable[nt_index(sym)]) break;
      }
    }
    return found;
  }

  ElementId feature_site(std::uint16_t term, ElementId raw, ElementId outer,
                         const TerminalSites& terms) const {
    ElementId site = canonical(raw);
    if (site == kNoElement) site = terms.own[term].agreed();
    if (site == kNoElement) site = terms.open[term].agreed();
    if (site == kNoElement) site = canonical(outer);
    return site;
  }

  // The continuations of each state, read off its shifts, less end of input,
  // trivia and the tokens no author types.
  void expect(const TerminalSites& terms, LrDerivedRows& rows) const {
    // Distinct pairs in first appearance; a state lists each once.
    std::map<std::array<std::uint16_t, 3>, std::uint16_t> pooled;
    for (std::uint16_t s = 0; s < a.nstates; ++s) {
      const auto first = rows.expectItems.size();
      rows.expectBase.push_back(static_cast<std::uint32_t>(first));
      for (const LrAction& act : a.action_row(s)) {
        if (!lr_takes_token(act.kind)) continue;
        if (act.term == 0 || act.term >= a.cfg.nterms) continue;
        if (a.cfg.terms[act.term].trivia || a.cfg.terms[act.term].laid != 0)
          continue;
        const auto item = [&](ElementId site, ElementId landing) {
          const auto key =
              std::array<std::uint16_t, 3>{act.term, site, landing};
          if (rows.expectPool.size() > UINT16_MAX && !pooled.contains(key))
            throw LowerRefused(LowerFail::tableTooLarge);
          const auto [at, fresh] = pooled.try_emplace(
              key, static_cast<std::uint16_t>(rows.expectPool.size()));
          if (fresh) rows.expectPool.push_back({act.term, site, landing});
          const auto begin = rows.expectItems.begin() + first;
          if (std::find(begin, rows.expectItems.end(), at->second) ==
              rows.expectItems.end())
            rows.expectItems.push_back(at->second);
        };
        if (act.kind != LrActionKind::guard) {
          item(feature_site(act.term, act.site, act.outer, terms), kNoElement);
          continue;
        }
        // Landings that write one member are one entry; otherwise each lands
        // on its own.
        const LrGuard& guard = a.guards[act.arg];
        std::vector<std::pair<ElementId, ElementId>> landed;
        for (std::size_t v = 0; v < guard.to.size(); ++v)
          if (guard.to[v] != LrGuard::kNoState)
            landed.emplace_back(
                feature_site(act.term, guard.site[v], guard.outer[v], terms),
                guard.to[v]);
        const bool alike = std::ranges::all_of(landed, [&](const auto& l) {
          return l.first == landed.front().first;
        });
        for (const auto& [site, to] : landed)
          item(site, alike ? kNoElement : to);
      }
      // By terminal, so a completion finds one candidate's entries by search.
      std::stable_sort(rows.expectItems.begin() + first,
                       rows.expectItems.end(),
                       [&](std::uint16_t x, std::uint16_t y) {
                         return rows.expectPool[x].feature <
                                rows.expectPool[y].feature;
                       });
    }
    rows.expectBase.push_back(
        static_cast<std::uint32_t>(rows.expectItems.size()));
  }
};

}  // namespace

void lr_derive_tables(LrBuildTable& a, std::span<const ElementId> sites) {
  if (sites.size() != std::size_t{a.writes.nchains} + a.nfused)
    throw std::logic_error("LR: completion sites do not cover the record rows");
  const LrDeriving d{a, sites};
  LrDerivedRows r;
  d.repair_states(r);
  if (a.nconflicts != 0 || a.multiCand) d.goto_classes(r);
  d.row_lhs(r, sites.size());
  r.shiftTerm = lr_shift_terms(a);
  d.expect(d.terminal_sites(), r);
  r.skipSet = lex_skip_set(LrLexView{a});
  lr_derive_lexical(a, r);
  d.repair_tables(r);
  lr_derive_reductions(a, r);
  static_cast<LrDerivedRows&>(a) = std::move(r);
  a.modeWinPool = SetPool(a.modeWin.data(), a.nmodes);
  if (a.modeFlag.size() != 0)
    a.modeFlagPool = SetPool(a.modeFlag.row(0, a.nmodes), a.nmodes);
  a.dfaAccPool = SetPool(a.dfaAcc.data(), a.ndfa);
  a.dfaResPool = SetPool(a.dfaRes.data(), a.ndfa);
  a.dfaRelayResPool = SetPool(a.dfaRelayRes.row(0, a.ndfa), a.ndfa);
  a.followPool = SetPool(a.follow.row(0, a.nstates), a.nstates);
  // A state past the last row owes nothing.
  a.closePool = SetPool(a.closeRow.row(0, a.nstates), a.nstates);
}

}  // namespace cursiv
