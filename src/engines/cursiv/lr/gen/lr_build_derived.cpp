// The tables a reader takes over a built one, derived once the automaton has
// stopped moving.
#include "cursiv/lr/gen/lr_build_derived.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/lr/gen/LrLexView.hpp"
#include "cursiv/lr/gen/LrLowProd.hpp"
#include "cursiv/lr/gen/lr_build_fold.hpp"
#include "cursiv/lr/gen/lr_build_lex.hpp"
#include "cursiv/lr/gen/lr_build_passes.hpp"
#include "cursiv/lr/table/LrAction.hpp"
#include "cursiv/lr/table/LrProduction.hpp"
#include "cursiv/shared/gen/KeepFirst.hpp"
#include "cursiv/shared/gen/LexBodyRows.hpp"
#include "cursiv/shared/gen/LexFirstByte.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/SetPool.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

namespace {

struct LrDeriving {
  const LrBuildTable& a;
  const std::span<const ElementId> sites;

  void repair_tables(LrDerivedRows& rows) const {
    const std::size_t n = a.ndfa;
    LexBodyRows bodyRows = lex_body_rows(
        LexBodyView{LrLexView{a}, a.ndfa, rows.typoSuccBase, rows.typoSucc});
    rows.lexBodies = std::move(bodyRows.bodies);
    rows.lexBodyTerms = std::move(bodyRows.terms);
    rows.lexOpeners = std::move(bodyRows.openers);
    const auto& into = bodyRows.into;
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
      for (const LrAction& x : a.action_row(s))
        a.each_target(x,
                      [&](std::uint16_t to) { after[x.term] |= a.follow[to]; });
    }
    rows.typoReach = SetPool(reach.data(), static_cast<unsigned>(n));
    rows.after = SetPool(after.data(), a.cfg.nterms);
    rows.modeWant = SetPool(want.data(), a.nmodes);
  }

  ElementId canonical(ElementId row) const {
    return row == kNoElement ? kNoElement : sites[row];
  }

  // The nonterminals a written row's record may reduce to. A production that
  // may start with a nullable symbol leaves its row unnamed: its record sits on
  // an empty frame, and the goto would leave from it.
  // Per state, the one state whose shifts and gotos reach it, or `kDeadState`;
  // `kDeadState` too where the move may leave a level or a bracket open.
  void preds(LrDerivedRows& rows) const {
    const auto& g = a.cfg;
    const auto levels = [&](std::uint16_t t) {
      return g.terms[t].synthetic == kSyntheticIndent   ? 1
             : g.terms[t].synthetic == kSyntheticDedent ? -1
                                                        : 0;
    };
    // What every text of a rule does to the levels and the brackets: what it
    // adds, and the lowest it reaches, unknown where two texts differ.
    struct Moves {
      int level = 0, lowLevel = 0, depth = 0, lowDepth = 0;
      bool operator==(const Moves&) const = default;
    };
    enum class Known : std::uint8_t { no, yes, mixed };
    std::vector<Known> known(g.nnts, Known::no);
    std::vector<Moves> moves(g.nnts);
    for (bool changed = true; changed;) {
      changed = false;
      for (unsigned p = 0; p < g.nprods; ++p) {
        const LrLowProd& q = g.prods[p];
        if (known[q.lhs] == Known::mixed) continue;
        Moves m;
        Known at = Known::yes;
        for (unsigned i = 0; i < q.len && at == Known::yes; ++i) {
          const std::uint16_t y = g.sym_at(q, i);
          Moves by;
          if (!is_term(y)) {
            at = known[nt_index(y)];
            by = moves[nt_index(y)];
          } else {
            by.level = levels(y);
            by.depth = g.terms[y].delim;
            by.lowLevel = std::min(by.level, 0);
            by.lowDepth = std::min(by.depth, 0);
          }
          m.lowLevel = std::min(m.lowLevel, m.level + by.lowLevel);
          m.lowDepth = std::min(m.lowDepth, m.depth + by.lowDepth);
          m.level += by.level;
          m.depth += by.depth;
        }
        if (at == Known::no) continue;
        const Known was = known[q.lhs];
        if (at == Known::mixed || (was == Known::yes && moves[q.lhs] != m))
          known[q.lhs] = Known::mixed;
        else if (was == Known::no)
          known[q.lhs] = Known::yes, moves[q.lhs] = m;
        changed = changed || known[q.lhs] != was;
      }
    }
    // A rule whose every text closes what it opens and no other.
    const auto closed = [&](std::uint16_t nt) {
      return known[nt] == Known::yes && moves[nt] == Moves{};
    };
    rows.preds.assign(a.nstates, kDeadState);
    std::vector<bool> reached(a.nstates, false), open(a.nstates, false);
    for (std::uint32_t s = 0; s < a.nstates; ++s) {
      const auto from = [&](bool opens) {
        return [&, opens](std::uint16_t to) {
          open[to] = open[to] || opens;
          if (!reached[to]) {
            reached[to] = true;
            rows.preds[to] = static_cast<std::uint16_t>(s);
          } else if (rows.preds[to] != s) {
            rows.preds[to] = kDeadState;
          }
        };
      };
      for (const LrAction& x : a.action_row(s))
        a.each_target(x,
                      from(levels(x.term) != 0 || g.terms[x.term].delim != 0));
      for (const LrGotoRows::Edge& e : a.gotoRows[s])
        a.each_goto_target(e, from(!closed(e.nt)));
    }
    for (std::uint32_t s = 0; s < a.nstates; ++s)
      if (open[s]) rows.preds[s] = kDeadState;
  }

  void row_lhs(LrDerivedRows& rows, std::size_t nrows) const {
    const auto& c = a.cfg;
    const std::vector<std::uint8_t>& nullable = a.nullable;
    std::vector<std::vector<std::uint16_t>> named(nrows);
    std::vector<std::uint8_t> refused(nrows, 0);
    for (unsigned p = 0; p < c.nprods; ++p) {
      const auto& q = c.prods[p];
      if (q.node == kNoRow) continue;
      if (const auto b = c.nt_after(p, 0); b != kNoRow && nullable[b])
        refused[q.node] = 1;
      auto& lhs = named[q.node];
      if (std::ranges::find(lhs, q.lhs) == lhs.end()) lhs.push_back(q.lhs);
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
      for (unsigned k = 0; k < a.cfg.ngapConds; ++k)
        row.push_back(!a.tagKnown[s].test(k) || a.tagTruth[s].test(k));
      rows.gotoClass[s] = classes
                              .try_emplace(row, static_cast<std::uint16_t>(
                                                    classes.size()))
                              .first->second;
    }
  }

  // What the readings of a token say to a caret: each site once, and no site only
  // where none of them names one.
  std::vector<LrCaretSite> sites_read(std::uint32_t alts) const {
    std::vector<LrCaretSite> out;
    for (const LrCaretSite& reading : a.siteSets[alts]) {
      const ElementId site = canonical(reading.site);
      if (site == kNoElement) continue;
      const auto known = std::ranges::find(out, site, &LrCaretSite::site);
      if (known != out.end())
        known->entered = known->entered && reading.entered;
      else
        out.push_back({site, reading.entered});
    }
    if (out.empty()) out.push_back({kNoElement, false});
    return out;
  }

  // The continuations of each state by terminal, as its shifts stand, less
  // end of input, trivia and the tokens no author types.
  void expect(LrDerivedRows& rows) const {
    // Distinct pairs in first appearance; a state lists each once.
    std::map<std::array<std::uint16_t, 4>, std::uint16_t> pooled;
    KeepFirst keep(a.work != nullptr ? &a.work->expects : nullptr);
    for (std::uint16_t s = 0; s < a.nstates; ++s) {
      const auto first = rows.expectItems.size();
      rows.expectBase.push_back(static_cast<std::uint32_t>(first));
      for (const LrAction& act : a.action_row(s)) {
        if (!lr_takes_token(act.kind)) continue;
        if (act.term == 0) continue;
        if (a.cfg.terms[act.term].trivia ||
            a.cfg.terms[act.term].synthetic != 0)
          continue;
        const auto item = [&](ElementId site, ElementId toState, bool entered) {
          const auto key = std::array<std::uint16_t, 4>{act.term, site, toState,
                                                        std::uint16_t{entered}};
          if (rows.expectPool.size() > UINT16_MAX && !pooled.contains(key))
            throw LowerRefused(LowerFail::tableTooLarge);
          const auto [at, fresh] = pooled.try_emplace(
              key, static_cast<std::uint16_t>(rows.expectPool.size()));
          if (fresh)
            rows.expectPool.push_back({act.term, site, toState, entered});
          rows.expectItems.push_back(at->second);
        };
        if (act.kind != LrActionKind::guard) {
          for (const LrCaretSite& read : sites_read(act.alts))
            item(read.site, kNoElement, read.entered);
          continue;
        }
        // Targets that write one member are one entry; otherwise each names
        // the state it goes to.
        const LrGuard& guard = a.guards[act.arg];
        struct Outcome {
          ElementId site, to;
          bool entered;
        };
        std::vector<Outcome> outcomes;
        for (std::size_t v = 0; v < guard.to.size(); ++v)
          if (guard.to[v] != LrGuard::kNoState)
            for (const LrCaretSite& read : sites_read(guard.alts[v]))
              outcomes.push_back({read.site, guard.to[v], read.entered});
        const bool oneSite = std::ranges::all_of(outcomes, [&](const auto& l) {
          return l.site == outcomes.front().site;
        });
        for (const Outcome& l : outcomes)
          item(l.site, oneSite ? kNoElement : l.to, l.entered);
      }
      rows.expectItems.resize(
          first + keep(std::span(rows.expectItems).subspan(first)));
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
  d.preds(r);
  r.shiftTerm = lr_shift_terms(a);
  d.expect(r);
  LexSkipRuns blanks = lex_skip_runs(LrLexView{a});
  r.skipSet = std::move(blanks.skip);
  r.skipStop = std::move(blanks.stop);
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
  // A state past the last row needs nothing.
  a.closePool = SetPool(a.closeRow.row(0, a.nstates), a.nstates);
}

}  // namespace cursiv::engine
