// The passes over the lowered CFG -- auxiliaries interned, twins dropped,
// cycles refused, records ridden down -- then the infix tables, the walk that
// drives it all, and the sentence a refusal is told in.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuckets.hpp"
#include "cursiv/engines/lr/gen/LrFirstSets.hpp"
#include "cursiv/engines/lr/gen/lr_copy_prod.hpp"
#include "cursiv/engines/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/engines/lr/gen/lr_nullable.hpp"
#include "cursiv/engines/lr/gen/lr_split_loops.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/InfixOp.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/OperatorWords.hpp"
#include "cursiv/engines/shared/gen/StrongComponents.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

// Productions `drop` accepts are removed in place, `startProd` recalled onto
// its survivor, and the rows past the count truncated.
template <class Cfg, class Pred>
void lr_compact_prods(Cfg& c, Pred&& drop) {
  std::uint16_t n = 0;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    if (drop(k)) continue;
    if (k == c.startProd) c.startProd = n;
    c.prodBuild[n] = c.prodBuild[k];
    c.prods[n++] = c.prods[k];
  }
  c.prods.truncate(n);
  c.prodBuild.truncate(n);
  c.nprods = n;
}

// Two auxiliaries that say the same thing are one. The self reference reads
// as a sentinel, and merging one auxiliary can make its caller identical: a
// fixpoint.
template <class Cfg>
void lr_intern_aux(Cfg& c) {
  // The auxiliaries are the last nonterminals minted.
  const std::uint16_t first = static_cast<std::uint16_t>(c.nnts - c.auxCount);
  if (c.auxCount < 2) return;

  const auto ix = lr_by_lhs(c);
  // What decides a nonterminal, its productions in order: each opens on its
  // length, so the words read back one way.
  const auto shape = [&](std::uint16_t a) {
    std::vector<std::uint32_t> key;
    for (const std::uint16_t k : ix.of(a)) {
      const LrLowProd& x = c.prods[k];
      const LrProdBuild& b = c.prodBuild[k];
      key.insert(key.end(), {x.len, x.node, x.gap, b.prec, b.endOnly, b.tailCfg,
                             b.closesRegion, b.priorityAlt, b.prioritySubset,
                             b.turn, b.decideLen, b.stop});
      key.insert(key.end(), c.decisions.v.begin() + b.decideAt,
                 c.decisions.v.begin() + b.decideAt + b.decideLen);
      for (unsigned i = 0; i < x.len; ++i) {
        const std::uint16_t s = c.sym_at(x, i);
        key.insert(key.end(), {s == nt_sym(a) ? 0x10000u : s, c.row_at(x, i),
                               c.cfg_at(k, i)});
      }
    }
    return key;
  };

  std::vector<bool> gone(c.nnts, false);
  std::vector<std::uint16_t> repl(c.nnts);
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t i = 0; i < c.nnts; ++i) repl[i] = i;
    std::map<std::vector<std::uint32_t>, std::uint16_t> alike;
    for (std::uint16_t a = first; a < c.nnts; ++a) {
      if (gone[a] || ix.of(a).empty()) continue;
      const auto [twin, fresh] = alike.try_emplace(shape(a), a);
      if (fresh) continue;
      repl[a] = twin->second;
      gone[a] = true;
      moved = true;
    }
    if (!moved) break;
    for (std::uint16_t k = 0; k < c.nprods; ++k) {
      LrLowProd& q = c.prods[k];
      for (unsigned i = 0; i < q.len; ++i) {
        const std::uint16_t sym = c.sym_at(q, i);
        if (is_term(sym)) continue;
        const std::uint16_t nt = nt_index(sym);
        if (repl[nt] != nt) c.slotSym[q.at + i] = nt_sym(repl[nt]);
      }
    }
  }

  // A merged nonterminal keeps its index; its productions go.
  lr_compact_prods(c, [&](std::uint16_t k) { return gone[c.prods[k].lhs]; });
}

// Every nonterminal the start reaches derives some finite text: a rule with
// no production, or none that ends in terminals, can never reduce. Nor can a
// production naming a terminal its reading skips.
inline void lr_no_rule_without_production(LrLowered& o) {
  LrBuildCfg& c = o.cfg;
  const auto ix = lr_by_lhs(c);
  const std::vector<bool> live = lr_reachable(c, ix);
  // A region that keeps a terminal names its own copy of it, so a rule only
  // that region reaches is not read in the reading that skips it.
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    if (!live[q.lhs]) continue;
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t sym = c.sym_at(q, i);
      if (!is_term(sym) || !c.terms[sym].trivia) continue;
      o.fault.raise(GrammarFaultCode::skippedTerminal, c.prodBuild[k].owner);
      return;
    }
  }
  std::vector<std::uint16_t> owed(c.nprods, 0);
  std::vector<std::vector<std::uint16_t>> users(c.nnts);
  std::vector<bool> productive(c.nnts, false);
  std::vector<std::uint16_t> work;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t sym = c.sym_at(q, i);
      if (is_term(sym)) continue;
      ++owed[k];
      users[nt_index(sym)].push_back(k);
    }
    if (owed[k] == 0 && !productive[q.lhs]) {
      productive[q.lhs] = true;
      work.push_back(q.lhs);
    }
  }
  while (!work.empty()) {
    const std::uint16_t nt = work.back();
    work.pop_back();
    for (const std::uint16_t k : users[nt]) {
      const std::uint16_t lhs = c.prods[k].lhs;
      if (--owed[k] == 0 && !productive[lhs]) {
        productive[lhs] = true;
        work.push_back(lhs);
      }
    }
  }
  // The rule to name is the one the others wait on: it has no production, or
  // the walk through what blocks it comes back on itself.
  for (std::uint16_t i = 0; i < c.nnts; ++i) {
    if (!live[i] || productive[i]) continue;
    std::vector<bool> seen(c.nnts, false);
    std::uint16_t nt = i;
    for (;;) {
      seen[nt] = true;
      std::uint16_t next = kNoRow;
      for (const std::uint16_t k : ix.of(nt)) {
        const LrLowProd& q = c.prods[k];
        for (unsigned j = 0; j < q.len && next == kNoRow; ++j) {
          const std::uint16_t sym = c.sym_at(q, j);
          if (!is_term(sym) && !productive[nt_index(sym)]) next = nt_index(sym);
        }
        if (next != kNoRow) break;
      }
      if (next == kNoRow || seen[next]) break;
      nt = next;
    }
    o.fault.raise(GrammarFaultCode::ruleWithoutProduction, nt);
    return;
  }
}

// Two alternatives spelling the same symbols: identical ones are one, and
// ones that write different things are a reduce nobody can arbitrate.
inline void lr_drop_twins(LrLowered& o) {
  LrBuildCfg& c = o.cfg;
  std::vector<bool> gone(c.nprods, false);
  // Twins share a left-hand side.
  const auto ix = lr_by_lhs(c);
  for (std::uint16_t g = 0; g < c.nnts; ++g) {
    const std::span<const std::uint16_t> group = ix.of(g);
    for (std::size_t ia = 0; ia < group.size(); ++ia) {
      const std::uint16_t a = group[ia];
      if (gone[a]) continue;
      for (std::size_t ib = ia + 1; ib < group.size(); ++ib) {
        const std::uint16_t b = group[ib];
        if (gone[b]) continue;
        const LrLowProd& x = c.prods[a];
        const LrLowProd& y = c.prods[b];
        const LrProdBuild& bx = c.prodBuild[a];
        const LrProdBuild& by = c.prodBuild[b];
        if (x.len != y.len || x.gap != y.gap || bx.endOnly != by.endOnly ||
            bx.prec != by.prec || bx.tailCfg != by.tailCfg ||
            bx.closesRegion != by.closesRegion || bx.turn != by.turn ||
            bx.stop != by.stop)
          continue;
        bool same = true;
        for (unsigned i = 0; i < x.len; ++i)
          if (c.sym_at(x, i) != c.sym_at(y, i) ||
              c.cfg_at(a, i) != c.cfg_at(b, i))
            same = false;
        if (!same) continue;
        bool writes = x.node != y.node;
        for (unsigned i = 0; i < x.len; ++i)
          writes = writes || c.row_at(x, i) != c.row_at(y, i);
        if (writes && bx.permuted && by.permuted) {
          gone[b] = true;
          continue;
        }
        if (writes) {
          o.fault.raise(GrammarFaultCode::twinProductions, bx.owner);
          continue;
        }
        // Source priority decides which duplicate stays.
        const bool preferB = by.priorityAlt < bx.priorityAlt ||
                             (by.priorityAlt == bx.priorityAlt &&
                              by.prioritySubset > bx.prioritySubset);
        gone[preferB ? a : b] = true;
        if (preferB) break;
      }
    }
  }
  lr_compact_prods(c, [&](std::uint16_t k) { return gone[k]; });
}

// A loop's exit that every choice took in is a rule nobody reads.
inline void lr_drop_unread_stops(LrLowered& o) {
  LrBuildCfg& c = o.cfg;
  std::vector<bool> read(c.nnts, false);
  for (std::uint16_t k = 0; k < c.nprods; ++k)
    for (unsigned i = 0; i < c.prods[k].len; ++i)
      if (const std::uint16_t sym = c.sym_at(c.prods[k], i); !is_term(sym))
        read[nt_index(sym)] = true;
  const auto ix = lr_by_lhs(c);
  lr_compact_prods(c, [&](std::uint16_t k) {
    const std::uint16_t lhs = c.prods[k].lhs;
    return c.prodBuild[k].stop != 0 && !read[lhs] && ix.of(lhs).size() == 1;
  });
}

// A chain that opens a node: only two creations refuse each other, since a
// close rides down and adopts.
inline bool lr_chain_creates(const LrLowered& o, std::uint16_t r) {
  return r != kNoRow && o.writes.chainBuild[r].creates;
}

// `A -> B` closes a cycle of reductions, and so does `A -> X B Y` where
// everything beside `B` derives nothing. At most one position may consume.
inline void lr_no_unit_cycle(LrLowered& o,
                             const std::vector<std::uint8_t>& empty) {
  const LrBuildCfg& c = o.cfg;
  std::vector<std::vector<std::uint16_t>> adj(c.nnts);
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    unsigned required = 0;
    for (unsigned i = 0; i < q.len && required < 2; ++i) {
      const std::uint16_t s = c.sym_at(q, i);
      required += is_term(s) || empty[nt_index(s)] == 0;
    }
    if (required > 1) continue;
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t s = c.sym_at(q, i);
      if (!is_term(s) && (required == 0 || empty[nt_index(s)] == 0))
        adj[q.lhs].push_back(nt_index(s));
    }
  }
  const auto scc = StrongComponents<>::of(adj);
  for (std::uint16_t i = 0; i < c.nnts; ++i)
    if (scc.cyclic[i]) {
      if (!o.fault)
        o.faultCycle = scc.walk(i, i, [&](std::uint32_t u, auto&& to) {
          for (const auto x : adj[u]) to(x);
        });
      o.fault.raise(GrammarFaultCode::unitProductionCycle, i);
      return;
    }
}

// `A -> B A x` where `B` may match nothing: the parser cannot know how many
// empty `B`s to reduce. Plain left recursion stays, its loops recorded.
inline void lr_no_hidden_left_recursion(
    LrLowered& o, const std::vector<std::uint8_t>& empty) {
  LrBuildCfg& c = o.cfg;
  std::vector<std::vector<std::uint16_t>> corner(c.nnts);
  struct Hidden {
    std::uint16_t lhs, sym;
    bool gated;
  };
  std::vector<Hidden> hidden;
  const auto is_gated = [&](std::uint16_t nt) {
    return std::find(o.gated.begin(), o.gated.end(), nt) != o.gated.end();
  };
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    bool gated = true;
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t s = c.sym_at(q, i);
      if (is_term(s)) break;
      corner[q.lhs].push_back(nt_index(s));
      if (i > 0) hidden.push_back({q.lhs, nt_index(s), gated});
      gated = gated && is_gated(nt_index(s));
      if (empty[nt_index(s)] == 0) break;
    }
  }
  const auto scc = StrongComponents<>::of(corner);
  for (const auto& [a, b, gated] : hidden)
    if (scc.component[a] == scc.component[b]) {
      // The hidden step first, then the way back.
      if (!o.fault) {
        o.faultCycle = scc.walk(b, a, [&](std::uint32_t u, auto&& to) {
          for (const auto x : corner[u]) to(x);
        });
        if (b != a) o.faultCycle.insert(o.faultCycle.begin(), a);
      }
      o.fault.raise(GrammarFaultCode::hiddenLeftRecursion, a, gated,
                    o.optionalGate);
      return;
    }
  for (std::uint16_t a = 0; a < c.nnts; ++a)
    c.leftLoop[a] =
        scc.cyclic[a] ? static_cast<std::uint16_t>(scc.component[a]) : kNoRow;
}

// A directive needs a span to ride: a called production that lays no record
// and hands none back (`'(' Expr ')'`) lays a bare one.
inline void lr_lay_bare_records(LrLowered& o) {
  LrBuildCfg& c = o.cfg;
  std::vector<bool> called(c.nnts, false);
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const LrLowProd& q = c.prods[k];
    for (unsigned i = 0; i < q.len; ++i) {
      const std::uint16_t sym = c.sym_at(q, i);
      if (c.row_at(q, i) != kNoRow && !is_term(sym))
        called[nt_index(sym)] = true;
    }
  }
  // A pass-through hands the demand down to its symbol.
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t k = 0; k < c.nprods; ++k) {
      const LrLowProd& q = c.prods[k];
      if (!called[q.lhs] || q.node != kNoRow || q.len != 1) continue;
      const std::uint16_t sym = c.sym_at(q, 0);
      if (is_term(sym)) continue;
      if (const std::uint16_t nt = nt_index(sym); !called[nt])
        called[nt] = moved = true;
    }
  }
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    LrLowProd& q = c.prods[k];
    if (called[q.lhs] && q.node == kNoRow && q.len != 1)
      q.node = o.writes.bare_chain();
  }
}

// Which nonterminals hand a record back: a greatest fixpoint, since one does
// only if every production of it does.
inline std::vector<bool> lr_hands_a_record(const LrBuildCfg& c) {
  std::vector<bool> cov(c.nnts, true);
  const auto hands = [&](const LrLowProd& q) {
    if (q.node != kNoRow) return true;
    if (q.len != 1) return false;
    const std::uint16_t sym = c.sym_at(q, 0);
    return is_term(sym) || cov[nt_index(sym)];
  };
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t k = 0; k < c.nprods; ++k) {
      const LrLowProd& q = c.prods[k];
      if (cov[q.lhs] && !hands(q)) {
        cov[q.lhs] = false;
        moved = true;
      }
    }
  }
  return cov;
}

// ...and which come back with a record already open: a chain that opens one
// may not ride down onto another. Least fixpoint.
inline std::vector<bool> lr_opens_a_record(const LrLowered& o) {
  const LrBuildCfg& c = o.cfg;
  std::vector<bool> opens(c.nnts, false);
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t k = 0; k < c.nprods; ++k) {
      const LrLowProd& q = c.prods[k];
      bool op = lr_chain_creates(o, q.node);
      if (!op && q.len == 1) {
        const std::uint16_t sym = c.sym_at(q, 0);
        op = lr_chain_creates(o, c.row_at(q, 0)) ||
             (!is_term(sym) && opens[nt_index(sym)]);
      }
      if (op && !opens[q.lhs]) {
        opens[q.lhs] = true;
        moved = true;
      }
    }
  }
  return opens;
}

// A unit production's chain rides down onto its position, before what the
// call site adds there.
inline void lr_ride_chains_down(LrLowered& o, const std::vector<bool>& hands,
                                const std::vector<bool>& opens) {
  LrBuildCfg& c = o.cfg;
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    LrLowProd& q = c.prods[k];
    // A turn lays its own record: it reaches back past its position.
    if (q.node == kNoRow || q.len != 1 || c.prodBuild[k].turn) continue;
    const std::uint16_t sym = c.sym_at(q, 0);
    if (!is_term(sym) && !hands[nt_index(sym)]) continue;
    if (lr_chain_creates(o, q.node) &&
        (lr_chain_creates(o, c.row_at(q, 0)) ||
         (!is_term(sym) && opens[nt_index(sym)])))
      continue;
    c.slots[q.at].row = o.writes.concat(q.node, c.row_at(q, 0));
    q.node = kNoRow;
  }
}

inline void lr_no_non_consuming(LrLowered& o, const LrLowerPool& p) {
  if (p.repeatedOperands.empty()) return;
  const std::vector<std::uint8_t> empty = lr_nullable(o.cfg, false, true);
  for (const auto& [at, owner] : p.repeatedOperands)
    if (lr_shape_nullable(p, at, o.cfg, empty)) {
      o.fault.raise(GrammarFaultCode::nullableRepetition, owner);
      return;
    }
}

inline void lr_settle(LrLowered& o) {
  const std::vector<std::uint8_t> empty = lr_nullable(o.cfg, false);
  lr_no_unit_cycle(o, empty);
  lr_no_hidden_left_recursion(o, empty);
  lr_lay_bare_records(o);
  const std::vector<bool> hands = lr_hands_a_record(o.cfg);
  const std::vector<bool> opens = lr_opens_a_record(o);
  lr_ride_chains_down(o, hands, opens);
  if (!o.fault && !o.cfg.lowerFail()) lr_split_loops(o);
}

// One step and one associativity per token an operator opens on.
inline void lr_bind_step(LrLowered& o, std::uint16_t term, std::uint8_t prec,
                         Association group, std::uint16_t owner) {
  TermBuild& t = o.cfg.termBuild[term];
  if (t.prec != 0 && (t.prec != prec || t.assoc != group))
    o.fault.raise(GrammarFaultCode::precedenceClash, owner);
  t.prec = prec;
  t.assoc = group;
}

// The strings of tokens a symbol can spell, read off its productions.
inline OperatorWords lr_operator_words(const LrBuildCfg& c,
                                       const LrFirstSets& fl,
                                       const LrBuckets<std::uint16_t>& ix,
                                       std::uint16_t sym,
                                       std::vector<std::uint16_t>& walking) {
  if (is_term(sym)) return OperatorWords::token(sym);
  const std::uint16_t nt = nt_index(sym);
  if (std::ranges::count(walking, nt) != 0) {
    std::vector<std::uint16_t> opening;
    for (std::uint16_t t = 0; t < c.nterms; ++t)
      if (fl.first[nt].test(t)) opening.push_back(t);
    return OperatorWords::unlisted(opening);
  }
  walking.push_back(nt);
  OperatorWords out = OperatorWords::none();
  for (const std::uint16_t k : ix.of(nt)) {
    OperatorWords spelt = OperatorWords::nothing();
    for (unsigned i = 0; i < c.prods[k].len; ++i)
      spelt = spelt.then(
          lr_operator_words(c, fl, ix, c.sym_at(c.prods[k], i), walking));
    out.unite(spelt);
  }
  walking.pop_back();
  return out;
}

// A terminal that lexes as `t` does, standing for it where an operator needs
// a step of its own.
inline std::uint16_t lr_clone_term(LrBuildCfg& c, std::uint16_t t) {
  if (c.nterms >= kNtBase) {
    lower_refuse(c, LowerFail::tooManyTerminals);
    return t;
  }
  const TermInfo info = c.terms[t];
  const TermBuild build = c.termBuild[t];
  const std::uint16_t to = c.nterms++;
  c.terms[to] = info;
  c.termBuild[to] = build;
  return to;
}

// A copy of `nt` that reads the tokens of `swap` as their replacements where
// it opens on them, under the name of the rule it copies.
inline std::uint16_t lr_clone_opening(
    LrBuildCfg& c, const LrFirstSets& fl, const LrBuckets<std::uint16_t>& ix,
    std::uint16_t nt, const std::map<std::uint16_t, std::uint16_t>& swap,
    const DynSet& keys, std::map<std::uint16_t, std::uint16_t>& made) {
  if (const auto at = made.find(nt); at != made.end()) return at->second;
  const std::uint16_t to = lr_new_aux(c, nt);
  made[nt] = to;
  for (const std::uint16_t k : ix.of(nt)) {
    const std::uint16_t copy = lr_copy_prod(c, k, to);
    if (copy == kNoRow) return to;
    for (unsigned i = 0; i < c.prods[copy].len; ++i) {
      const std::uint16_t sym = c.sym_at(c.prods[copy], i);
      const std::size_t slot = c.prods[copy].at + i;
      if (is_term(sym)) {
        if (const auto s = swap.find(sym); s != swap.end())
          c.slotSym[slot] = s->second;
        break;
      }
      const std::uint16_t inner = nt_index(sym);
      if (fl.first[inner].intersects(keys))
        c.slotSym[slot] =
            nt_sym(lr_clone_opening(c, fl, ix, inner, swap, keys, made));
      if (!fl.nullable[inner]) break;
    }
  }
  return to;
}

// Two operators on different steps open on one token only where the text
// after it tells them apart. Each then reads a token of its own, spelled as
// the other is: the lexer offers both where the state takes either, and the
// reading the next token refutes dies within the operator's length.
inline void lr_settle_infix_ops(LrLowered& o) {
  if (o.infixOps.empty()) return;
  LrBuildCfg& c = o.cfg;
  const LrFirstSets fl = lr_first_sets(c);
  const LrBuckets<std::uint16_t> ix = lr_by_lhs(c);
  const auto spells = [&](const InfixOp& op) {
    return c.sym_at(c.prods[op.prod], op.slot);
  };
  const auto opens = [&](std::uint16_t sym) {
    if (!is_term(sym)) return fl.first[nt_index(sym)];
    DynSet one;
    one.set(sym);
    return one;
  };
  const std::size_t n = o.infixOps.size();
  std::vector<OperatorWords> words;
  std::vector<std::uint16_t> walking;
  for (std::size_t i = 0; i < n; ++i) {
    const InfixOp& op = o.infixOps[i];
    words.push_back(lr_operator_words(c, fl, ix, spells(op), walking));
    for (std::size_t j = 0; j < i; ++j) {
      const InfixOp& was = o.infixOps[j];
      if ((was.prec != op.prec || was.group != op.group) &&
          words[j].meets(words[i])) {
        o.fault.raise(GrammarFaultCode::precedenceClash, op.owner);
        return;
      }
    }
  }
  // A token keeps the step of its first operator that is a rule's, else of its
  // first: a terminal is the cheaper to give a token of its own.
  using Step = std::pair<std::uint8_t, Association>;
  std::vector<std::optional<Step>> kept(c.nterms);
  for (const bool byRule : {true, false})
    for (const InfixOp& op : o.infixOps) {
      const std::uint16_t sym = spells(op);
      if (is_term(sym) == byRule) continue;
      const DynSet at = opens(sym);
      for (std::uint16_t t = 0; t < kept.size(); ++t)
        if (at.test(t) && !kept[t]) kept[t] = Step{op.prec, op.group};
    }
  std::map<std::pair<std::uint16_t, Step>, std::uint16_t> given;
  bool rewrote = false;
  for (const InfixOp& op : o.infixOps) {
    const std::uint16_t sym = spells(op);
    const Step step{op.prec, op.group};
    const DynSet at = opens(sym);
    std::map<std::uint16_t, std::uint16_t> swap;
    for (std::uint16_t t = 0; t < kept.size(); ++t) {
      if (!at.test(t) || *kept[t] == step) continue;
      const auto [own, fresh] = given.try_emplace(std::make_pair(t, step), t);
      if (fresh) own->second = lr_clone_term(c, t);
      swap[t] = own->second;
    }
    if (swap.empty()) continue;
    rewrote = true;
    const std::size_t slot = c.prods[op.prod].at + op.slot;
    if (is_term(sym)) {
      c.slotSym[slot] = swap.begin()->second;
    } else {
      std::map<std::uint16_t, std::uint16_t> made;
      DynSet keys;
      for (const auto& [t, unused] : swap) keys.set(t);
      c.slotSym[slot] =
          nt_sym(lr_clone_opening(c, fl, ix, nt_index(sym), swap, keys, made));
    }
  }
  std::optional<LrFirstSets> rebuilt;
  if (rewrote) rebuilt = lr_first_sets(c);
  const LrFirstSets& bound = rewrote ? *rebuilt : fl;
  for (const InfixOp& op : o.infixOps) {
    const std::uint16_t sym = spells(op);
    if (is_term(sym)) {
      lr_bind_step(o, sym, op.prec, op.group, op.owner);
      continue;
    }
    for (std::uint16_t t = 0; t < c.nterms; ++t)
      if (bound.first[nt_index(sym)].test(t))
        lr_bind_step(o, t, op.prec, op.group, op.owner);
  }
}

}  // namespace cursiv
