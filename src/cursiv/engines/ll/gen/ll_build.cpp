// The construction over a lowered program: what every position may begin and
// be followed by, the graph a prediction walks, and the verdict of every
// choice. One translation unit, no grammar type -- a tree with seventy
// languages compiles this once.
#include "cursiv/engines/ll/gen/ll_build.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlRecordPlan.hpp"
#include "cursiv/engines/ll/gen/ll_lex.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeSim.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/OperatorWords.hpp"
#include "cursiv/engines/shared/gen/PredicateModes.hpp"
#include "cursiv/core/utils/StrongComponents.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {
namespace {

using Built = LlBuiltProgram;
using Cfg = LlLowCfg;

// Nothing at all is a reading of this position. A repetition that admits zero
// turns is, and so is a group every member of which is.
bool node_nullable(const Cfg& c, std::uint16_t id,
                   std::span<const std::uint8_t> nullable,
                   std::span<const std::uint8_t> ruleNullable,
                   bool endMarkNullable = false) {
  const LlNode& n = c.nodes[id];
  switch (n.kind) {
    case LlShape::term:
      return false;
    // The assertion matches no token and still refuses every input that has
    // one: for a lookahead it begins with `$end`, so it is not nothing.
    case LlShape::endMark:
      return endMarkNullable;
    case LlShape::empty:
    case LlShape::condition:
    case LlShape::implied:
      return true;
    case LlShape::call:
      return ruleNullable[n.sym] != 0;
    case LlShape::seq:
      return std::ranges::all_of(c.children(n),
                                 [&](auto k) { return nullable[k] != 0; });
    case LlShape::alt:
      return std::ranges::any_of(c.children(n),
                                 [&](auto k) { return nullable[k] != 0; });
    case LlShape::rep:
      return n.min == 0 || nullable[n.sym] != 0;
  }
  return false;
}

void settle_nullable(const Cfg& c, std::vector<std::uint8_t>& nullable,
                     bool endMarkNullable = false) {
  nullable.assign(c.nnodes, 0);
  std::vector<std::uint8_t> ruleNullable(c.nrules, 0);
  detail::settle_empty([&](auto mark) {
    for (std::uint32_t id = 0; id < c.nnodes; ++id) {
      const std::uint8_t v =
          node_nullable(c, static_cast<std::uint16_t>(id), nullable,
                        ruleNullable, endMarkNullable);
      mark(nullable[id], v);
    }
    for (std::uint16_t r = 1; r < c.nrules; ++r) {
      const LlRuleInfo& u = c.rules[r];
      // A climb with no operator in the text is its operand alone, and an
      // operand that is no rule is a terminal.
      std::uint8_t v = 0;
      if (u.climb != kNoRow) {
        if (const auto primary = c.climbs[u.climb].primary; primary != kNoRow)
          v = ruleNullable[primary];
      } else if (u.body != kNoRow) {
        v = nullable[u.body];
      }
      mark(ruleNullable[r], v);
    }
  });
}

// What may stand at the head of this position.
bool node_first(const Cfg& c, const std::vector<std::uint8_t>& nullable,
                std::vector<DynSet>& first,
                const std::vector<DynSet>& ruleFirst,
                const std::vector<DynSet>& impliedHeads, std::uint16_t id) {
  const LlNode& n = c.nodes[id];
  DynSet& out = first[id];
  switch (n.kind) {
    case LlShape::term: {
      DynSet one;
      one.set(n.sym);
      return out.add(one);
    }
    case LlShape::endMark: {
      DynSet one;
      one.set(kEofSymbol);
      one.set(n.sym);
      return out.add(one);
    }
    case LlShape::empty:
    case LlShape::condition:
      return false;
    case LlShape::implied:
      return out.add(impliedHeads[n.min]);
    case LlShape::call:
      return out.add(ruleFirst[n.sym]);
    case LlShape::seq:
    case LlShape::alt: {
      bool grew = false;
      for (const auto k : c.children(n)) {
        grew = out.add(first[k]) || grew;
        if (n.kind == LlShape::seq && !nullable[k]) break;
      }
      return grew;
    }
    case LlShape::rep:
      return out.add(first[n.sym]);
  }
  return false;
}

void settle_first(const Cfg& c, const std::vector<std::uint8_t>& nullable,
                  std::vector<DynSet>& first, std::vector<DynSet>& ruleFirst) {
  first.assign(c.nnodes, DynSet{});
  ruleFirst.assign(c.nrules, DynSet{});
  // What can start an implied token's condition: the tokens it is laid on.
  std::vector<DynSet> impliedHeads(c.ngapConds);
  const std::vector<DynSet> firsts = condition_firsts(c);
  for (unsigned k = 0; k < c.ngapConds; ++k)
    for (unsigned t = 1; t < c.nterms; ++t) {
      if (!firsts[k].test(t)) continue;
      if (c.termBuild[t].predicateOnly)
        impliedHeads[k] |= spelled_twins(c, t);
      else
        impliedHeads[k].set(t);
    }
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint32_t id = 0; id < c.nnodes; ++id)
      moved = node_first(c, nullable, first, ruleFirst, impliedHeads,
                         static_cast<std::uint16_t>(id)) ||
              moved;
    for (std::uint16_t r = 1; r < c.nrules; ++r) {
      const LlRuleInfo& u = c.rules[r];
      if (u.climb != kNoRow) {
        const LlClimb& cl = c.climbs[u.climb];
        if (cl.primary != kNoRow)
          moved = ruleFirst[r].add(ruleFirst[cl.primary]) || moved;
        else
          moved = ruleFirst[r].add(first[cl.primaryNode]) || moved;
      } else if (u.body != kNoRow)
        moved = ruleFirst[r].add(first[u.body]) || moved;
    }
  }
}

// The rules this position may call before anything is consumed. A cycle over
// this relation is a descent that calls itself for ever.
void left_calls(const Built& b, const std::vector<std::uint8_t>& empty,
                std::uint16_t id, std::vector<bool>& seen,
                std::vector<std::uint16_t>& out) {
  const LlNode& n = b.cfg.nodes[id];
  if (n.kind == LlShape::call && !seen[n.sym]) {
    seen[n.sym] = true;
    out.push_back(n.sym);
  }
  for (const auto k : b.cfg.children(n)) {
    left_calls(b, empty, k, seen, out);
    if (n.kind == LlShape::seq && !empty[k]) break;
  }
}

// A call of `r`'s own cycle that a repetition holds or an optional one
// precedes: the lowering spells such alternatives out up to the optional gate,
// so the refusal names that limit.
bool repeated_lead(const Built& b, const std::vector<std::uint8_t>& empty,
                   std::uint16_t r, std::span<const std::uint32_t> component) {
  const Cfg& c = b.cfg;
  struct At {
    std::uint16_t id;
    bool repeated;
  };
  std::vector<At> todo;
  if (c.rules[r].body != kNoRow) todo.push_back({c.rules[r].body, false});
  while (!todo.empty()) {
    auto [id, repeated] = todo.back();
    todo.pop_back();
    const LlNode& n = c.nodes[id];
    if (n.kind == LlShape::call) {
      if (repeated && component[n.sym] == component[r]) return true;
      continue;
    }
    repeated = repeated || n.kind == LlShape::rep;
    for (const auto k : c.children(n)) {
      todo.push_back({k, repeated});
      if (n.kind != LlShape::seq) continue;
      if (!empty[k]) break;
      repeated = repeated || c.nodes[k].kind == LlShape::rep;
    }
  }
  return false;
}

bool refuse_left_recursion(Built& b, const std::vector<std::uint8_t>& empty) {
  const std::uint16_t n = b.cfg.nrules;
  std::vector<std::vector<std::uint16_t>> edge(n);
  std::vector<bool> seen(n, false);
  for (std::uint16_t r = 1; r < n; ++r) {
    const LlRuleInfo& u = b.cfg.rules[r];
    if (u.climb != kNoRow && b.cfg.climbs[u.climb].primary != kNoRow)
      edge[r].push_back(b.cfg.climbs[u.climb].primary);
    else if (u.body != kNoRow)
      left_calls(b, empty, u.body, seen, edge[r]);
    for (const auto q : edge[r]) seen[q] = false;
  }
  const auto scc = StrongComponents<>::of(edge);
  const auto calls = [&](std::uint32_t q, auto&& to) {
    for (const auto x : edge[q]) to(x);
  };
  bool cyclic = false;
  for (std::uint16_t r = 1; r < n; ++r) {
    if (!scc.cyclic[r]) continue;
    if (!b.fault) {
      b.faultCycle = scc.walk(r, r, calls);
      const bool repeated = repeated_lead(b, empty, r, scc.component);
      b.fault.raise(GrammarFaultCode::leftRecursion, r, repeated,
                    repeated ? std::uint16_t{kOptionalGate} : std::uint16_t{0});
    }
    cyclic = true;
  }
  return cyclic;
}

std::vector<std::uint8_t> refuse_non_consuming(Built& b) {
  std::vector<std::uint8_t> empty;
  // EOF constrains FIRST but still makes no progress through the input.
  settle_nullable(b.cfg, empty, true);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    if (!empty[id]) continue;
    const LlNodeBuild& n = b.cfg.nodeBuild[id];
    if (n.repeatOwner != kNoRow)
      b.fault.raise(GrammarFaultCode::nullableRepetition, n.repeatOwner);
  }
  return empty;
}

std::vector<bool> reached_from(const Cfg& c, std::uint16_t from) {
  std::vector<bool> seen(c.nrules, false);
  if (from >= c.nrules) return seen;
  std::vector<std::uint16_t> rules{from}, nodes;
  seen[from] = true;
  const auto rule = [&](std::uint16_t r) {
    if (r < c.nrules && !seen[r]) seen[r] = true, rules.push_back(r);
  };
  while (!rules.empty()) {
    const LlRuleInfo& u = c.rules[rules.back()];
    rules.pop_back();
    if (u.body != kNoRow) nodes.push_back(u.body);
    if (u.climb != kNoRow) {
      const LlClimb& cl = c.climbs[u.climb];
      rule(cl.primary);
      if (cl.primaryNode != kNoRow) nodes.push_back(cl.primaryNode);
      if (cl.selectNode != kNoRow) nodes.push_back(cl.selectNode);
    }
    while (!nodes.empty()) {
      const LlNode& n = c.nodes[nodes.back()];
      nodes.pop_back();
      if (n.kind == LlShape::call) rule(n.sym);
      for (const auto k : c.children(n)) nodes.push_back(k);
    }
  }
  return seen;
}

// A position of a live rule names a terminal its reading skips: the lexer
// never hands it over there. Walked without recursion: a counted repetition
// nests as deep as its bound.
bool names_skipped(const Cfg& c, std::uint16_t id) {
  std::vector<std::uint16_t> nodes{id};
  while (!nodes.empty()) {
    const LlNode& n = c.nodes[nodes.back()];
    nodes.pop_back();
    if (n.kind == LlShape::term) {
      if (c.terms[n.sym].trivia) return true;
      continue;
    }
    for (const auto k : c.children(n)) nodes.push_back(k);
  }
  return false;
}

void refuse_skipped_terminal(Built& b) {
  const Cfg& c = b.cfg;
  for (std::uint16_t i = 1; i < c.nrules; ++i) {
    if (!b.liveRules[i]) continue;
    const LlRuleInfo& u = c.rules[i];
    bool named = u.body != kNoRow && names_skipped(c, u.body);
    if (u.climb != kNoRow) {
      const LlClimb& cl = c.climbs[u.climb];
      named = named ||
              (cl.primaryNode != kNoRow && names_skipped(c, cl.primaryNode));
      named =
          named || (cl.selectNode != kNoRow && names_skipped(c, cl.selectNode));
    }
    if (named) b.fault.raise(GrammarFaultCode::skippedTerminal, i);
  }
}

// A reading that asserts the end of input and reads a token after it is
// impossible; a rule every reading of which is, or waits on one that is,
// derives no finite text.
void refuse_unproductive(Built& b) {
  const Cfg& c = b.cfg;
  std::vector<std::uint8_t> node(c.nnodes, 0), rule(c.nrules, 0);
  const auto productive = [&](const LlNode& n) {
    switch (n.kind) {
      case LlShape::call:
        return rule[n.sym] != 0;
      case LlShape::seq: {
        const auto k = c.children(n);
        bool ended = false;
        for (unsigned i = 0; i < k.size(); ++i) {
          const LlShape s = c.nodes[k[i]].kind;
          if (!node[k[i]] ||
              (ended && (s == LlShape::term || s == LlShape::implied)))
            return false;
          ended = ended || s == LlShape::endMark;
        }
        return true;
      }
      case LlShape::alt:
        return std::ranges::any_of(c.children(n),
                                   [&](auto k) { return node[k] != 0; });
      case LlShape::rep:
        return n.min == 0 || node[n.sym] != 0;
      default:
        return true;
    }
  };
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint32_t id = 0; id < c.nnodes; ++id)
      if (!node[id] && productive(c.nodes[id])) node[id] = 1, moved = true;
    for (std::uint16_t r = 1; r < c.nrules; ++r) {
      const LlRuleInfo& u = c.rules[r];
      bool p = u.body != kNoRow && node[u.body];
      if (u.climb != kNoRow) {
        const LlClimb& cl = c.climbs[u.climb];
        p = cl.primary != kNoRow ? rule[cl.primary] != 0
                                 : node[cl.primaryNode] != 0;
      }
      if (p && !rule[r]) rule[r] = 1, moved = true;
    }
  }
  // The rule to name is the one the others wait on.
  const auto waits_on = [&](std::uint16_t r) -> std::uint16_t {
    const LlRuleInfo& u = c.rules[r];
    std::vector<std::uint16_t> nodes;
    if (u.climb != kNoRow) {
      const LlClimb& cl = c.climbs[u.climb];
      if (cl.primary != kNoRow) return rule[cl.primary] ? kNoRow : cl.primary;
      nodes.push_back(cl.primaryNode);
    } else if (u.body != kNoRow) {
      nodes.push_back(u.body);
    }
    while (!nodes.empty()) {
      const LlNode& n = c.nodes[nodes.back()];
      nodes.pop_back();
      if (n.kind == LlShape::call && !rule[n.sym]) return n.sym;
      const auto kids = c.children(n);
      nodes.insert(nodes.end(), kids.rbegin(), kids.rend());
    }
    return kNoRow;
  };
  for (std::uint16_t r = 1; r < c.nrules; ++r) {
    if (!b.liveRules[r] || rule[r]) continue;
    std::vector<bool> seen(c.nrules, false);
    std::uint16_t at = r;
    for (;;) {
      seen[at] = true;
      const std::uint16_t next = waits_on(at);
      if (next == kNoRow || seen[next]) break;
      at = next;
    }
    b.fault.raise(GrammarFaultCode::ruleWithoutProduction, at);
    return;
  }
}

std::vector<std::uint16_t> opening_of(const Built& b, const DynSet& set) {
  std::vector<std::uint16_t> out;
  for (std::uint16_t t = 0; t < b.cfg.nterms; ++t)
    if (set.test(t)) out.push_back(t);
  return out;
}

// Conditions and marks spell nothing: they never tell two operators apart.
OperatorWords words_of(const Built& b, std::uint16_t id,
                       std::vector<std::uint16_t>& calling) {
  const LlNode& n = b.cfg.nodes[id];
  switch (n.kind) {
    case LlShape::term:
      return OperatorWords::token(n.sym);
    case LlShape::seq: {
      OperatorWords out = OperatorWords::nothing();
      for (const auto k : b.cfg.children(n))
        out = out.then(words_of(b, k, calling));
      return out;
    }
    case LlShape::alt: {
      OperatorWords out = OperatorWords::none();
      for (const auto k : b.cfg.children(n)) out.unite(words_of(b, k, calling));
      return out;
    }
    case LlShape::rep: {
      OperatorWords out =
          n.min == 0 ? OperatorWords::nothing() : OperatorWords::none();
      out.unite(n.max == kEndless
                    ? OperatorWords::unlisted(opening_of(b, b.first[n.sym]))
                    : words_of(b, n.sym, calling));
      return out;
    }
    case LlShape::call: {
      const LlRuleInfo& u = b.cfg.rules[n.sym];
      if (u.body == kNoRow || std::ranges::count(calling, n.sym) != 0)
        return OperatorWords::unlisted(opening_of(b, b.ruleFirst[n.sym]));
      calling.push_back(n.sym);
      OperatorWords out = words_of(b, u.body, calling);
      calling.pop_back();
      return out;
    }
    default:
      return OperatorWords::nothing();
  }
}

// Two operators of one table on different steps open on one token only where
// the text after it tells them apart: no string is both of theirs. Each table
// climbs over its own operators, so two tables never clash.
void refuse_precedence_clash(Built& b) {
  struct Spelt {
    OperatorWords words;
    std::uint8_t prec;
    Association group;
  };
  std::vector<Spelt> spelt;
  std::vector<std::uint16_t> calling;
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    if (b.cfg.rules[r].climb == kNoRow) continue;
    const LlClimb& cl = b.cfg.climbs[b.cfg.rules[r].climb];
    spelt.clear();
    for (std::uint16_t i = 0; i < cl.opCount; ++i) {
      const LlClimbOp& op = b.cfg.climbOps[cl.opBase + i];
      OperatorWords words = words_of(b, op.body, calling);
      for (const Spelt& s : spelt)
        if ((s.prec != op.prec || s.group != op.assoc) && s.words.meets(words))
          b.fault.raise(GrammarFaultCode::precedenceClash, r);
      spelt.push_back({std::move(words), op.prec, op.assoc});
    }
  }
}

// The skip configuration the trivia before each token is read under. The gap
// between two tokens is read under the configuration of the group that holds
// both -- the sequence two of whose members they end and start, the loop two
// of whose turns -- and it is read by the first point that peeks once the
// first token is consumed. That point lexes under one configuration, so every
// gap it may read must agree. Sets of configurations, one bit each.
struct Gaps {
  const Built& b;
  std::vector<std::uint8_t> silent{}, consumes{}, freshOut{};
  std::vector<std::uint8_t> ruleSilent{}, ruleConsumes{}, ruleFreshOut{};
  // Per position: the gaps before its first token from a token that nothing
  // peeked after, the gaps across it where it reads nothing, and the gaps
  // after its last token.
  std::vector<DynSet> pre{}, span{}, after{};
  std::vector<DynSet> rulePre{}, ruleSpan{}, ruleAfter{};
  // Whether a callee's first positions take the gaps of its callers.
  bool entries = true;
  // Per sequence, one past its last member that consumes and one past its last
  // that may not be passed. Fixed once the shape is.
  std::vector<std::uint32_t> consumeEnd{}, passEnd{};

  static DynSet bit(unsigned k) {
    DynSet s;
    s.set(k);
    return s;
  }
  unsigned cfg(std::uint16_t id) const { return b.cfg.nodeBuild[id].cfg; }
  unsigned section(std::uint16_t id) const {
    return b.cfg.nodeBuild[id].section;
  }
  unsigned rule_cfg(std::uint16_t r) const { return b.cfg.ruleCfg[r]; }
  const LlNode& node(std::uint16_t id) const { return b.cfg.nodes[id]; }
  std::uint16_t kid(const LlNode& n, unsigned i) const {
    return b.cfg.kids[n.sym + i];
  }

  // Whether a position may pass without peeking, may consume, and may end
  // on a token with no peek after it: least fixpoints, a rule reaching itself
  // before it consumes being refused.
  bool settle_shape() {
    bool moved = false;
    const auto raise = [&](std::vector<std::uint8_t>& v, std::size_t i,
                           bool x) {
      if (x && !v[i]) v[i] = 1, moved = true;
    };
    for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
      const LlNode& n = node(static_cast<std::uint16_t>(id));
      bool si = false, co = false, fr = false;
      switch (n.kind) {
        case LlShape::term:
          co = fr = true;
          break;
        case LlShape::empty:
          si = true;
          break;
        case LlShape::endMark:
          // Zero width, yet the table reads its gap as a token's.
          co = true;
          break;
        case LlShape::condition:
        case LlShape::implied:
          break;
        case LlShape::call:
          si = ruleSilent[n.sym];
          co = ruleConsumes[n.sym];
          fr = ruleFreshOut[n.sym];
          break;
        case LlShape::seq: {
          si = true;
          std::uint16_t last = kNoRow;
          for (unsigned i = 0; i < n.count; ++i) {
            const std::uint16_t k = kid(n, i);
            si = si && silent[k];
            co = co || consumes[k];
            if (!silent[k]) last = k;
          }
          fr = last != kNoRow && freshOut[last];
          break;
        }
        case LlShape::alt:
          for (unsigned i = 0; i < n.count; ++i) {
            co = co || consumes[kid(n, i)];
            fr = fr || freshOut[kid(n, i)];
          }
          break;
        case LlShape::rep:
          si = n.min != 0 && silent[n.sym];
          co = consumes[n.sym];
          fr = n.max != kEndless && freshOut[n.sym];
          break;
      }
      raise(silent, id, si);
      raise(consumes, id, co);
      raise(freshOut, id, fr);
    }
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
      const LlRuleInfo& u = b.cfg.rules[r];
      if (u.climb != kNoRow) {
        raise(ruleConsumes, r, true);
      } else if (u.body != kNoRow) {
        raise(ruleSilent, r, silent[u.body]);
        raise(ruleConsumes, r, consumes[u.body]);
        raise(ruleFreshOut, r, freshOut[u.body]);
      }
    }
    return moved;
  }

  void note_later() {
    consumeEnd.assign(b.cfg.nnodes, 0);
    passEnd.assign(b.cfg.nnodes, 0);
    for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
      const LlNode& n = node(static_cast<std::uint16_t>(id));
      if (n.kind != LlShape::seq) continue;
      for (unsigned i = 0; i < n.count; ++i) {
        const std::uint16_t k = kid(n, i);
        if (consumes[k]) consumeEnd[id] = i + 1;
        if (!b.nullable[k]) passEnd[id] = i + 1;
      }
    }
  }

  bool settle_sets() {
    bool moved = false;
    const auto add = [&](DynSet& to, const DynSet& x) {
      if (to.add(x)) moved = true;
    };
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
      const LlRuleInfo& u = b.cfg.rules[r];
      if (u.climb != kNoRow) {
        // An operand stands after the operator it follows, and before the
        // operator or the end of the climb.
        const LlClimb& cl = b.cfg.climbs[u.climb];
        const DynSet own = bit(rule_cfg(r));
        const DynSet in = rulePre[r] | own;
        const DynSet out = ruleAfter[r] | own;
        if (cl.primary != kNoRow) {
          add(rulePre[cl.primary], in);
          add(ruleAfter[cl.primary], out);
        } else {
          add(pre[cl.primaryNode], in);
          add(after[cl.primaryNode], out);
        }
        if (cl.selectNode != kNoRow) {
          add(pre[cl.selectNode], out);
          add(span[cl.selectNode], own);
          add(after[cl.selectNode], own);
        }
      } else if (u.body != kNoRow) {
        add(pre[u.body], rulePre[r]);
        add(span[u.body], ruleSpan[r]);
        add(after[u.body], ruleAfter[r]);
      }
    }
    // A group is laid after its members: the ids walked down read parents
    // first.
    for (std::uint32_t at = b.cfg.nnodes; at-- > 0;) {
      const auto id = static_cast<std::uint16_t>(at);
      const LlNode& n = node(id);
      switch (n.kind) {
        case LlShape::call:
          if (entries) {
            add(rulePre[n.sym], pre[id]);
            add(ruleSpan[n.sym], span[id]);
          }
          add(ruleAfter[n.sym], after[id]);
          break;
        case LlShape::alt:
          // The gap in front of an arm's first token is the choice's.
          for (unsigned i = 0; i < n.count; ++i) {
            add(pre[kid(n, i)], pre[id]);
            add(span[kid(n, i)], span[id]);
            add(after[kid(n, i)], after[id]);
          }
          break;
        case LlShape::rep: {
          const DynSet own = bit(cfg(id));
          const std::uint16_t k = n.sym;
          if (n.min >= 1) {
            add(pre[k], pre[id]);
            add(span[k], span[id]);
          }
          add(after[k], (n.max > 1 ? own : DynSet{}) | after[id]);
          break;
        }
        case LlShape::seq: {
          const DynSet own = bit(cfg(id));
          // The gap in front of member `i`: a section's own on both its
          // sides.
          const auto gap = [&](unsigned i) {
            if (i < n.count && section(kid(n, i)) != 0)
              return bit(section(kid(n, i)) - 1u);
            if (i > 0 && section(kid(n, i - 1)) != 0)
              return bit(section(kid(n, i - 1)) - 1u);
            return own;
          };
          bool fresh = false, above = true;
          for (unsigned i = 0; i < n.count; ++i) {
            const std::uint16_t k = kid(n, i);
            const bool in = consumeEnd[id] > i + 1, out = passEnd[id] <= i + 1;
            if (fresh) {
              add(pre[k], gap(i));
              add(span[k], (in ? gap(i) : DynSet{}) |
                               (out ? after[id] : DynSet{}));
            }
            if (above) {
              add(pre[k], pre[id]);
              add(span[k], (in ? pre[id] : DynSet{}) |
                               (out ? span[id] : DynSet{}));
            }
            add(after[k], (in ? gap(i + 1) : DynSet{}) |
                              (out ? after[id] : DynSet{}));
            if (!silent[k]) {
              fresh = freshOut[k] != 0;
              above = false;
            }
          }
          break;
        }
        default:
          break;
      }
    }
    return moved;
  }
};

void settle_gaps(Built& b) {
  const std::uint32_t nn = b.cfg.nnodes;
  const std::uint16_t nr = b.cfg.nrules;
  b.gapCfg.assign(nn, 0);
  b.headCfg.assign(nn, 0);
  b.climbCfg.assign(b.cfg.nclimbs, 0);
  for (std::uint32_t id = 0; id < nn; ++id)
    b.gapCfg[id] = b.headCfg[id] = b.cfg.nodeBuild[id].cfg;
  for (std::uint16_t r = 1; r < nr; ++r)
    if (b.cfg.rules[r].climb != kNoRow)
      b.climbCfg[b.cfg.rules[r].climb] = b.cfg.ruleCfg[r];
  if (b.cfg.cfgTriviaAt.size() <= 2) return;
  Gaps g{b};
  g.silent = g.consumes = g.freshOut = std::vector<std::uint8_t>(nn, 0);
  g.ruleSilent = g.ruleConsumes = g.ruleFreshOut =
      std::vector<std::uint8_t>(nr, 0);
  while (g.settle_shape()) {
  }
  // The document opens and ends under the grammar's own configuration.
  const auto settle = [&](Gaps& x) {
    x.pre.assign(nn, DynSet{});
    x.span.assign(nn, DynSet{});
    x.after.assign(nn, DynSet{});
    x.rulePre.assign(nr, DynSet{});
    x.ruleSpan.assign(nr, DynSet{});
    x.ruleAfter.assign(nr, DynSet{});
    x.rulePre[b.cfg.entryRule] = x.ruleSpan[b.cfg.entryRule] =
        x.ruleAfter[b.cfg.entryRule] = Gaps::bit(0);
    x.note_later();
    while (x.settle_sets()) {
    }
  };
  settle(g);
  // A call site peeks its callee's first token: callers that cross the gap
  // in front of it under different configurations share no point inside it.
  Gaps local{.b = b, .silent = g.silent, .consumes = g.consumes,
             .freshOut = g.freshOut, .entries = false};
  settle(local);

  const auto lowest = [](const DynSet& s) {
    std::uint32_t w = 0;
    while (s.word(w) == 0) ++w;
    return w * 64u + static_cast<unsigned>(std::countr_zero(s.word(w)));
  };
  const auto one = [&](const DynSet& set, const DynSet& own,
                       std::uint8_t& out) {
    if (set.none()) return;
    const DynSet* at = &set;
    if (set.more_than_one()) {
      const auto home = [&](unsigned k) {
        return k < b.cfg.cfgHome.size() ? b.cfg.cfgHome[k] : 0;
      };
      DynSet rest = set;
      rest.reset(lowest(set));
      for (; rest.any(); rest.reset(lowest(rest)))
        if (home(lowest(set)) != home(lowest(rest))) {
          lower_refuse(b.cfg, LowerFail::sectionPointShared);
          return;
        }
      if (own.more_than_one())
        lower_refuse(b.cfg, LowerFail::regionBoundaryShared);
      if (own.none()) return;
      at = &own;
    }
    out = static_cast<std::uint8_t>(lowest(*at));
  };
  // A position that may read nothing peeks the token behind it too.
  const auto peeked = [&](const Gaps& x, std::uint16_t id) {
    return x.pre[id] | (b.nullable[id] ? x.span[id] : DynSet{});
  };
  for (std::uint32_t at = 0; at < nn; ++at) {
    const auto id = static_cast<std::uint16_t>(at);
    const LlNode& n = b.cfg.nodes[id];
    switch (n.kind) {
      case LlShape::term:
      case LlShape::call:
      case LlShape::alt:
        one(peeked(g, id), peeked(local, id), b.gapCfg[id]);
        break;
      case LlShape::endMark:
        break;
      case LlShape::condition:
      case LlShape::implied:
        one(g.span[id], local.span[id], b.gapCfg[id]);
        break;
      case LlShape::rep:
        one(peeked(g, id), peeked(local, id), b.headCfg[id]);
        if (n.max > 1 && g.freshOut[n.sym])
          one(Gaps::bit(g.cfg(id)) | g.after[id],
              Gaps::bit(g.cfg(id)) | local.after[id], b.gapCfg[id]);
        break;
      default:
        break;
    }
  }
  for (std::uint16_t r = 1; r < nr; ++r) {
    const LlRuleInfo& u = b.cfg.rules[r];
    if (u.climb == kNoRow) continue;
    const LlClimb& cl = b.cfg.climbs[u.climb];
    const bool fresh =
        cl.primary != kNoRow ? g.ruleFreshOut[cl.primary] != 0 : true;
    if (fresh)
      one(Gaps::bit(g.rule_cfg(r)) | g.ruleAfter[r],
          Gaps::bit(g.rule_cfg(r)) | local.ruleAfter[r], b.climbCfg[u.climb]);
  }
  b.afterCfg.assign(nn, kNoRow);
  for (std::uint32_t id = 0; id < nn; ++id) {
    const DynSet& set = g.after[id];
    if (b.cfg.nodes[id].kind == LlShape::term && set.any() &&
        !set.more_than_one())
      b.afterCfg[id] = static_cast<std::uint16_t>(lowest(set));
  }
}

// A choice read past its first token, where an arm may open on the token in
// front of a section: its lookahead would read the section's first token
// across the caller's trivia.
void refuse_section_lookahead(Built& b) {
  DynSet opens;
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::seq) continue;
    for (unsigned i = 1; i < n.count; ++i)
      if (b.cfg.nodeBuild[b.cfg.kids[n.sym + i]].section != 0)
        opens.set(b.cfg.nodes[b.cfg.kids[n.sym + i - 1]].sym);
  }
  if (opens.none()) return;
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    const LlDecision& dec = b.cfg.decisions[d];
    if (dec.how == LlPredict::single) continue;
    for (unsigned a = 0; a < b.cfg.decisionBuild[d].nalts; ++a)
      if (b.laSet[dec.set + a].intersects(opens))
        lower_refuse(b.cfg, LowerFail::sectionPointShared);
  }
}

// What follows every position, to a fixpoint: a rule's own follow feeds the
// positions that call it, and those feed it back. Only `ruleFollow` feeds the
// propagation, so only its growth asks for another pass.
struct Follow {
  const Cfg& c;
  const std::vector<std::uint8_t>& nullable;
  const std::vector<DynSet>& first;
  std::vector<DynSet>& after;
  std::vector<DynSet>& ruleFollow;
  bool moved = false;

  void walk(std::uint16_t id, const DynSet& set) {
    struct Visit {
      std::uint16_t id;
      DynSet follow;
    };
    std::vector<Visit> pending{{id, set}};
    while (!pending.empty()) {
      Visit visit = std::move(pending.back());
      pending.pop_back();
      after[visit.id].add(visit.follow);
      const LlNode& n = c.nodes[visit.id];
      switch (n.kind) {
        case LlShape::call:
          moved = ruleFollow[n.sym].add(visit.follow) || moved;
          break;
        case LlShape::seq: {
          const auto k = c.children(n);
          DynSet acc = std::move(visit.follow);
          const auto begin = pending.size();
          for (unsigned i = k.size(); i-- > 0;) {
            pending.push_back({k[i], acc});
            if (!nullable[k[i]]) acc = DynSet{};
            acc |= first[k[i]];
          }
          // Keep the recursive walk's right-to-left sequence order.
          std::reverse(pending.begin() + begin, pending.end());
          break;
        }
        case LlShape::alt:
        case LlShape::rep: {
          // A turn is followed by the next one, or whatever follows the loop.
          DynSet inner = std::move(visit.follow);
          if (n.kind == LlShape::rep && n.max > 1) inner |= first[n.sym];
          const auto children = c.children(n);
          for (auto k = children.rbegin(); k != children.rend(); ++k)
            pending.push_back({*k, inner});
          break;
        }
        default:
          break;
      }
    }
  }
};

void settle_after(const Cfg& c, const std::vector<std::uint8_t>& nullable,
                  const std::vector<DynSet>& first,
                  const std::vector<DynSet>& ruleFirst,
                  std::vector<DynSet>& after, std::vector<DynSet>& ruleFollow) {
  after.assign(c.nnodes, DynSet{});
  ruleFollow.assign(c.nrules, DynSet{});
  // The document ends after the entry, and nothing else may.
  ruleFollow[c.entryRule].set(kEofSymbol);
  Follow f{c, nullable, first, after, ruleFollow};
  for (f.moved = true; f.moved;) {
    f.moved = false;
    for (std::uint16_t r = 1; r < c.nrules; ++r) {
      const LlRuleInfo& u = c.rules[r];
      if (u.climb != kNoRow) {
        // An operand is followed by an operator of the table, or by whatever
        // follows the climb.
        const LlClimb& cl = c.climbs[u.climb];
        DynSet ops = ruleFollow[r];
        for (unsigned i = 0; i < cl.opCount; ++i)
          ops |= first[c.climbOps[cl.opBase + i].body];
        if (cl.primary != kNoRow)
          f.moved = ruleFollow[cl.primary].add(ops) || f.moved;
        else
          f.walk(cl.primaryNode, ops);
        if (cl.selectNode != kNoRow) {
          after[cl.selectNode].add(ruleFollow[r]);
          const auto& select = c.nodes[cl.selectNode];
          const DynSet operand = cl.primary != kNoRow ? ruleFirst[cl.primary]
                                                      : first[cl.primaryNode];
          for (unsigned i = 0; i < cl.opCount; ++i)
            f.walk(c.kids[select.sym + i], operand);
          f.walk(c.kids[select.sym + cl.opCount], ruleFollow[r]);
        }
      } else if (u.body != kNoRow) {
        f.walk(u.body, ruleFollow[r]);
      }
    }
  }
}

// The graph, built from the program: one state per boundary of a body, one
// edge per way out of it. A decision's own state leaves exactly one edge per
// arm, in the arms' order, so a closure names the arm by the edge it took.
struct AtnGen {
  Built& b;
  std::vector<std::vector<LlAtnEdge>> out{};
  std::vector<std::vector<std::uint16_t>> owner{};
  // The states a call already returns into, so a second call to the same place
  // is the only one that costs a state.
  std::vector<std::uint8_t> claimed{};

  std::uint32_t add(LlAtnKind k, std::uint16_t rule,
                    std::uint16_t dec = kNoRow) {
    const auto id = static_cast<std::uint32_t>(out.size());
    out.emplace_back();
    owner.emplace_back();
    claimed.push_back(0);
    b.returnNode.push_back(kNoRow);
    b.states[id] = LlAtnState{.kind = k, .rule = rule};
    b.nstates = id + 1;
    if (dec != kNoRow) choose(id, dec);
    return id;
  }
  // The decision's own state, which is what a simulation starts from. A
  // choice leaves from the position it stands on, which nothing leaves yet:
  // a walk then crosses no state that only passes it on.
  void choose(std::uint32_t at, std::uint16_t dec) {
    b.states[at].decision = dec;
    b.cfg.decisions[dec].state = at;
  }
  void eps(std::uint32_t from, std::uint32_t to) {
    out[from].push_back(LlAtnEdge{.kind = LlEdgeKind::epsilon, .target = to});
    owner[from].push_back(kNoRow);
  }
  void atom(std::uint32_t from, std::uint16_t t, std::uint32_t to,
            std::uint16_t node = kNoRow) {
    out[from].push_back(
        LlAtnEdge{.kind = LlEdgeKind::atom, .label = t, .target = to});
    owner[from].push_back(node);
  }
  // A return state no other call shares, and the position that called kept
  // beside the edge: two arms of one rule resume at one point, so the state a
  // call comes back to would not say which call it was -- and a caret standing
  // under the callee reads the member THIS call writes. The first claim keeps
  // the state it was given; only a second pays an epsilon of its own, which
  // the closure walks through like any other.
  void call(std::uint32_t from, std::uint16_t r, std::uint32_t to,
            std::uint16_t node = kNoRow) {
    std::uint32_t back = to;
    if (claimed[to] != 0) {
      back = add(LlAtnKind::basic, b.states[from].rule);
      eps(back, to);
      claimed[back] = 1;
    } else {
      claimed[to] = 1;
    }
    out[from].push_back(LlAtnEdge{.kind = LlEdgeKind::rule,
                                  .label = r,
                                  .target = b.ruleStart[r],
                                  .follow = back});
    owner[from].push_back(node);
    b.returnNode[back] = node;
  }

  void build(std::uint16_t id, std::uint32_t from, std::uint32_t to,
             std::uint16_t rule);
};

void AtnGen::build(std::uint16_t id, std::uint32_t from, std::uint32_t to,
                   std::uint16_t rule) {
  struct Visit {
    std::uint16_t id;
    std::uint32_t from, to;
    unsigned child = 0;
  };
  // Counted options form deep ladders. Keep children and continuations on the
  // heap, preserving the recursive walk's state, edge and call allocation order.
  // A visit with no node adds the epsilon that follows a completed child.
  std::vector<Visit> pending{{id, from, to}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    id = visit.id;
    from = visit.from;
    to = visit.to;
    if (id == kNoRow) {
      eps(from, to);
      continue;
    }
    const LlNode& n = b.cfg.nodes[id];
    switch (n.kind) {
      case LlShape::term:
        atom(from, n.sym, to, id);
        break;
      // The assertion consumes nothing and stands only where the text ends,
      // which is what `$end` in front of it says -- or in a section, the token
      // that closes it. Its edges name the assertion, which is no token's.
      case LlShape::endMark:
        atom(from, kEofSymbol, to, id);
        if (n.sym != kEofSymbol) atom(from, n.sym, to, id);
        break;
      case LlShape::empty:
        eps(from, to);
        break;
      case LlShape::condition:
      case LlShape::implied:
        out[from].push_back(
            LlAtnEdge{.kind = LlEdgeKind::condition,
                      .label = static_cast<std::uint16_t>(
                          n.kind == LlShape::condition ? n.sym : n.min),
                      .target = to});
        owner[from].push_back(id);
        break;
      case LlShape::call:
        b.nodeFollow[id] = to;
        call(from, n.sym, to, id);
        break;
      case LlShape::seq: {
        const auto k = b.cfg.children(n);
        if (k.empty()) {
          eps(from, to);
        } else {
          const unsigned i = visit.child;
          const std::uint32_t next =
              i + 1 == k.size() ? to : add(LlAtnKind::basic, rule);
          if (i + 1 < k.size()) pending.push_back({id, next, to, i + 1});
          pending.push_back({k[i], from, next});
        }
        break;
      }
      case LlShape::alt: {
        const auto k = b.cfg.children(n);
        choose(from, n.decision);
        const std::uint32_t tail = add(LlAtnKind::basic, rule);
        pending.push_back({kNoRow, tail, to});
        const auto begin = pending.size();
        for (unsigned i = 0; i < k.size(); ++i) {
          const auto arm = add(LlAtnKind::basic, rule);
          eps(from, arm);
          pending.push_back({k[i], arm, tail});
        }
        std::reverse(pending.begin() + begin, pending.end());
        break;
      }
      case LlShape::rep: {
        // Each counted occurrence already has its own position. Options have
        // no back edge; an unbounded repetition returns to its choice instead.
        if (n.min == 0) choose(from, n.decision);
        const auto body = add(LlAtnKind::basic, rule);
        const auto back = add(LlAtnKind::basic, rule,
                              n.min == 0 ? kNoRow : n.decision);
        if (n.min == 0) {
          eps(from, body);
          eps(from, to);
          pending.push_back({kNoRow, back, n.max == 1 ? to : from});
        } else {
          // One turn is owed; arm 0 takes another after the body completes.
          eps(from, body);
          pending.push_back({kNoRow, back, to});
          pending.push_back({kNoRow, back, body});
        }
        pending.push_back({n.sym, body, back});
        break;
      }
    }
  }
}

// A climb is `primary (op primary)*`, and the loop is decided by the operator
// alone: the step it sits on is the descent's business, not the graph's.
void build_climb(AtnGen& g, std::uint16_t rule, LlClimb& cl, std::uint32_t from,
                 std::uint32_t to) {
  Built& b = g.b;
  if (cl.opCount != 0 && cl.selectNode == kNoRow)
    throw std::logic_error(
        "cursiv: an LL climb's operators have no select node");
  const auto decision =
      cl.selectNode == kNoRow ? kNoRow : b.cfg.nodes[cl.selectNode].decision;
  const std::uint32_t entry = g.add(LlAtnKind::basic, rule, decision);
  cl.atnEntry = entry;
  const auto operand = [&](std::uint32_t from2) {
    if (cl.primary != kNoRow)
      g.call(from2, cl.primary, entry);
    else
      g.atom(from2, b.cfg.nodes[cl.primaryNode].sym, entry, cl.primaryNode);
  };
  operand(from);
  for (unsigned i = 0; i < cl.opCount; ++i) {
    const std::uint32_t afterOp = g.add(LlAtnKind::basic, rule);
    const std::uint32_t before = g.add(LlAtnKind::basic, rule);
    g.eps(entry, before);
    const auto& select = b.cfg.nodes[cl.selectNode];
    g.build(b.cfg.kids[select.sym + i], before, afterOp, rule);
    operand(afterOp);
  }
  g.eps(entry, to);
}

void build_atn(Built& b) {
  b.returnNode.clear();
  AtnGen g{b};
  b.nodeFollow.assign(b.cfg.nnodes, 0);
  b.ruleStart.assign(b.cfg.nrules, 0);
  std::vector<std::uint32_t> ruleStop(b.cfg.nrules, 0);
  // A rule the entry never reaches has no states: its start reads as the
  // first state, like the document's own, and no closure ever enters it.
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    if (!b.liveRules[r]) continue;
    b.ruleStart[r] = g.add(LlAtnKind::basic, r);
    ruleStop[r] = g.add(LlAtnKind::ruleStop, r);
  }
  // The document, as a caller of the entry: without it a closure reaching the
  // entry's end has nowhere to go, and `$end` would be unreachable.
  const std::uint32_t entry = g.add(LlAtnKind::basic, 0);
  const std::uint32_t afterEntry = g.add(LlAtnKind::basic, 0);
  const std::uint32_t done = g.add(LlAtnKind::ruleStop, 0);
  g.call(entry, b.cfg.entryRule, afterEntry);
  g.atom(afterEntry, kEofSymbol, done);
  b.atnEntry = entry;
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
    if (!b.liveRules[r]) continue;
    const LlRuleInfo& u = b.cfg.rules[r];
    if (u.climb != kNoRow)
      build_climb(g, r, b.cfg.climbs[u.climb], b.ruleStart[r], ruleStop[r]);
    else if (u.body != kNoRow)
      g.build(u.body, b.ruleStart[r], ruleStop[r], r);
    else
      g.eps(b.ruleStart[r], ruleStop[r]);
  }
  // The edges go to one pool, each state keeping where its run begins.
  b.nedges = 0;
  b.edgeNode.clear();
  for (std::uint32_t s = 0; s < b.nstates; ++s) {
    b.states[s].edge = b.nedges;
    b.states[s].nedges = static_cast<std::uint32_t>(g.out[s].size());
    for (std::size_t k = 0; k < g.out[s].size(); ++k) {
      b.edges[b.nedges++] = g.out[s][k];
      b.edgeNode.push_back(g.owner[s][k]);
    }
  }
  // ...and every state a call of one rule returns to, which is where a
  // closure that reached the end of a rule carries on when its caller is not
  // known.
  std::vector<std::vector<std::uint32_t>> back(b.cfg.nrules);
  for (std::uint32_t e = 0; e < b.nedges; ++e)
    if (b.edges[e].kind == LlEdgeKind::rule)
      back[b.edges[e].label].push_back(b.edges[e].follow);
  b.callerBase.assign(b.cfg.nrules + 1, 0);
  for (std::uint16_t r = 0; r < b.cfg.nrules; ++r)
    b.callerBase[r + 1] =
        b.callerBase[r] + static_cast<std::uint32_t>(back[r].size());
  b.callerAt.clear();
  for (std::uint16_t r = 0; r < b.cfg.nrules; ++r)
    b.callerAt.insert(b.callerAt.end(), back[r].begin(), back[r].end());
  b.returnClass.resize(b.nstates);
  for (std::uint32_t s = 0; s < b.nstates; ++s) b.returnClass[s] = s;
  std::map<std::vector<std::uint32_t>, std::uint32_t> byStops;
  std::vector<std::uint8_t> seen(b.nstates, 0), returns(b.nstates, 0);
  for (const std::uint32_t r : b.callerAt) returns[r] = 1;
  std::vector<std::uint32_t> stops, visited, todo;
  for (std::uint32_t s = 0; s < b.nstates; ++s) {
    if (returns[s] == 0) continue;
    stops.clear();
    visited.clear();
    todo.assign(1, s);
    while (!todo.empty()) {
      const std::uint32_t t = todo.back();
      todo.pop_back();
      if (seen[t] != 0) continue;
      seen[t] = 1;
      visited.push_back(t);
      const LlAtnState& st = b.states[t];
      bool stop = st.kind == LlAtnKind::ruleStop;
      for (std::uint32_t k = 0; k < st.nedges; ++k) {
        const LlAtnEdge& e = b.edges[st.edge + k];
        if (e.kind == LlEdgeKind::epsilon)
          todo.push_back(e.target);
        else
          stop = true;
      }
      if (stop) stops.push_back(t);
    }
    for (const std::uint32_t t : visited) seen[t] = 0;
    std::sort(stops.begin(), stops.end());
    b.returnClass[s] = byStops.try_emplace(stops, s).first->second;
    if (stops.size() == 1 && b.states[stops[0]].kind == LlAtnKind::ruleStop)
      b.returnClass[s] |= kLlTailReturn;
  }
}

// A set of configurations, canonical: the same readings met twice are one
// state of the automaton, whatever order they were reached in.
std::vector<LlConfig> set_key(const std::vector<LlConfig>& cs) {
  std::vector<LlConfig> k = cs;
  std::sort(k.begin(), k.end());
  k.erase(std::unique(k.begin(), k.end()), k.end());
  return k;
}

// Every token the readings of one set could read next.
DynSet viable_terms(const LlAtnView& atn, const std::vector<LlConfig>& cs) {
  DynSet t;
  for (const LlConfig& c : cs)
    for (const LlAtnEdge& e : atn.outgoing(c.state))
      if (e.kind == LlEdgeKind::atom) t.set(e.label);
  return t;
}

// How many states one choice may explore before it is left to the parse: a
// grammar whose readings do not close is one whose automaton would be filled
// from the text anyway.
constexpr std::size_t kExploreCap = 512;

LlAtnView view_of(const Built& b) {
  return LlAtnView{.states = b.states.data(),
                   .nstates = b.nstates,
                   .edges = b.edges.data(),
                   .ruleStart = b.ruleStart.data(),
                   .callerBase = b.callerBase.data(),
                   .callerAt = b.callerAt.data(),
                   .returnClass = b.returnClass.data()};
}

// True where the exploration closed: the automaton is then a table, and no
// parse ever runs a closure for this choice.
bool explore_decision(Built& b, LlDecision& dec, const LlBuildOptions& opt) {
  const LlAtnView atn = view_of(b);
  LlSimulation sim(atn, 4096);
  std::map<std::vector<LlConfig>, std::uint32_t> index;
  std::vector<std::vector<LlConfig>> setOf;
  std::vector<std::vector<LlDfaEdge>> out;
  std::vector<std::uint8_t> depthOf, apart;
  std::vector<std::uint16_t> nodeOf, armOf;
  // The readings of a set cross the gap before their next token under one
  // configuration, or the set is the parse's to read.
  const bool regions = b.cfg.cfgTriviaAt.size() > 2;
  const auto read_node = [&](const std::vector<LlConfig>& cs) {
    std::uint16_t node = kNoRow;
    apart.push_back(0);
    if (!regions) return node;
    for (const LlConfig& c : cs) {
      const LlAtnState& q = atn.state(c.state);
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        if (atn.edge(q.edge + i).kind != LlEdgeKind::atom) continue;
        const std::uint16_t at = b.edgeNode[q.edge + i];
        if (at == kNoRow) continue;
        if (node == kNoRow)
          node = at;
        else
          apart.back() |= b.gapCfg[at] != b.gapCfg[node];
      }
    }
    return node;
  };

  const auto intern = [&](std::uint8_t depth) {
    const auto& cs = sim.configs();
    const auto [at, fresh] = index.try_emplace(
        set_key(cs), static_cast<std::uint32_t>(setOf.size()));
    const auto id = at->second;
    if (!fresh) return id;
    setOf.push_back(cs);
    armOf.push_back(sim.settled());
    nodeOf.push_back(read_node(cs));
    out.emplace_back();
    depthOf.push_back(depth);
    return id;
  };

  sim.start(dec.state, kLlCtxNone);
  // The unknown caller also includes nullable returns. A condition reached
  // before reading a token needs the actual input and cannot enter this DFA.
  if (sim.capped() || sim.needs_filter()) return false;
  intern(0);
  for (std::uint32_t at = 0; at < setOf.size(); ++at) {
    // A set naming one arm is an answer; one naming none is a reading no arm
    // survives, which the parse reports rather than predicts.
    if (setOf[at].empty() || armOf[at] != kNoRow) continue;
    if (apart[at]) return false;
    if (depthOf[at] >= opt.depth || setOf.size() >= kExploreCap) return false;
    const DynSet terms = viable_terms(atn, setOf[at]);
    for (std::uint16_t t = 0; t < b.cfg.nterms; ++t) {
      if (!terms.test(t)) continue;
      sim.reset();
      sim.load(setOf[at]);
      const bool survives = sim.step(t);
      // A pending guard must not remove an arm during token-only exploration.
      // Once one arm remains, its guards belong to parsing that selected arm;
      // the DFA stops at this state without traversing them.
      if (sim.capped() ||
          (sim.needs_filter() && sim.settled() == kNoRow))
        return false;
      if (!survives) continue;
      const auto to = intern(static_cast<std::uint8_t>(depthOf[at] + 1));
      out[at].push_back(LlDfaEdge{.term = t, .target = to});
    }
  }
  // A set from which no token settles an arm ends in the parse's own choice
  // however far it reads: the walk hands over before entering it, or a run of
  // such readings costs every token after it once per choice it meets.
  std::vector<std::uint8_t> settles(setOf.size(), 0);
  for (std::size_t i = 0; i < setOf.size(); ++i)
    settles[i] = armOf[i] != kNoRow;
  for (bool grew = true; grew;) {
    grew = false;
    for (std::size_t i = 0; i < setOf.size(); ++i)
      if (settles[i] == 0 &&
          std::ranges::any_of(out[i], [&](const LlDfaEdge& e) {
            return settles[e.target] != 0;
          }))
        settles[i] = grew = true;
  }
  for (auto& edges : out)
    std::erase_if(edges,
                  [&](const LlDfaEdge& e) { return settles[e.target] == 0; });
  // ...and into the pool, where a state's arm is what its readings agree on.
  const auto base = static_cast<std::uint32_t>(b.dfa.size());
  bool open = false;
  for (std::size_t i = 0; i < setOf.size(); ++i) {
    const std::uint16_t alt = armOf[i];
    open = open || (alt == kNoRow && out[i].empty());
    LlDfaState st{.alt = alt,
                  .edge = static_cast<std::uint32_t>(b.dfaEdge.size()),
                  .nedges = static_cast<std::uint32_t>(out[i].size()),
                  .node = nodeOf[i]};
    for (LlDfaEdge e : out[i]) {
      e.target += base;
      b.dfaEdge.push_back(e);
    }
    b.dfa.push_back(st);
  }
  dec.dfa = base;
  b.nopen += open;
  return true;
}

// What a position costs to invent: one token per terminal, and for a call the
// cheapest reading of the rule it names. A least fixed point, so a rule that
// reaches itself is not cheaper than the reading that does not.
constexpr std::uint32_t kLlCostless = 0xFFFFFFFFu;

// What an arm costs to be ENTERED, which is not what it costs to be written
// whole: once a token has been invented the construct is open, and the
// repetition behind it holds the author's own text -- what closes that text
// is owed only after it is read, never at the entry. `whole` asks the other.
struct LlEntry {
  std::uint32_t cost = kLlCostless;
  bool stop = false;
};

LlEntry entry_cost(const Built& b, std::uint16_t id,
                   const std::vector<std::uint32_t>& rule, bool opened,
                   bool whole = false) {
  const LlNode& n = b.cfg.nodes[id];
  switch (n.kind) {
    case LlShape::term:
      return {1, false};
    case LlShape::empty:
    case LlShape::implied:
      return {0, false};
    // End of input is not a thing a repair can invent: asserting that the
    // document stops where it plainly does not would accept, in silence, the
    // text the assertion exists to refuse.
    case LlShape::endMark:
    case LlShape::condition:
      return {kLlCostless, false};
    case LlShape::call:
      return {rule[n.sym], false};
    case LlShape::seq: {
      std::uint32_t sum = 0;
      for (unsigned i = 0; i < n.count; ++i) {
        const LlEntry k =
            entry_cost(b, b.cfg.kids[n.sym + i], rule, opened, whole);
        if (k.cost == kLlCostless) return {kLlCostless, false};
        sum += k.cost;
        if (k.stop) return {sum, true};
        if (k.cost != 0) opened = true;
      }
      return {sum, false};
    }
    case LlShape::alt: {
      LlEntry best;
      for (unsigned i = 0; i < n.count; ++i) {
        const LlEntry k =
            entry_cost(b, b.cfg.kids[n.sym + i], rule, opened, whole);
        if (k.cost < best.cost) best = k;
      }
      return best;
    }
    case LlShape::rep: {
      if (n.min == 0) return {0, opened && !whole};
      const LlEntry k = entry_cost(b, n.sym, rule, opened, whole);
      if (k.cost == kLlCostless) return {kLlCostless, false};
      return k;
    }
  }
  return {kLlCostless, false};
}

void settle_repair(Built& b) {
  std::vector<std::uint32_t> rule(b.cfg.nrules, kLlCostless);
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
      const LlRuleInfo& u = b.cfg.rules[r];
      const std::uint32_t k =
          u.climb != kNoRow  ? (b.cfg.climbs[u.climb].primary != kNoRow
                                    ? rule[b.cfg.climbs[u.climb].primary]
                                    : 1u)
          : u.body != kNoRow ? entry_cost(b, u.body, rule, false, true).cost
                             : kLlCostless;
      if (k < rule[r]) rule[r] = k, moved = true;
    }
  }
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    LlDecision& dec = b.cfg.decisions[d];
    const LlNode& n = b.cfg.nodes[dec.node];
    if (n.kind != LlShape::alt) continue;
    std::uint32_t best = kLlCostless;
    for (std::uint16_t i = 0; i < n.count; ++i) {
      const std::uint32_t k =
          entry_cost(b, b.cfg.kids[n.sym + i], rule, false).cost;
      if (k < best) best = k, dec.repair = i;
    }
    b.cfg.decisionBuild[d].repairCost =
        best > kLlRepairCostless ? kLlRepairCostless
                                 : static_cast<std::uint16_t>(best);
  }
}

// The state the graph stands in once each position is read: what a caret
// seeds its closure from. The walk itself is the prediction's own, run at the
// caret rather than at a choice.
void settle_after_states(Built& b) {
  const std::size_t n = b.cfg.nnodes;
  b.nodeAfter.assign(n, 0xFFFFFFFFu);
  for (std::uint32_t st = 0; st < b.nstates; ++st) {
    const LlAtnState& q = b.states[st];
    for (std::uint32_t i = 0; i < q.nedges; ++i) {
      const std::uint16_t node = b.edgeNode[q.edge + i];
      if (node == kNoRow) continue;
      b.nodeAfter[node] = b.edges[q.edge + i].target;
    }
  }
  // A call stands after its own return state: that is where the graph is once
  // the rule it names has been read, and it is what a caret climbs to name
  // the member a token inside that rule fills.
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id)
    if (b.cfg.nodes[id].kind == LlShape::call)
      b.nodeAfter[id] = b.nodeFollow[id];
}

// The positions an invocation of a rule can read first, before it has read
// anything: only there does the rule's caller state what the token fills.
void settle_head_nodes(Built& b) {
  b.nodeHead.assign(b.cfg.nnodes, 0);
  std::vector<std::uint8_t> nullable(b.cfg.nrules, 0);
  std::vector<std::uint32_t> stamp(b.nstates, 0);
  std::vector<std::uint32_t> todo;
  std::uint32_t round = 0;
  const auto walk = [&](std::uint16_t rule, auto&& visit) {
    ++round;
    todo.assign(1, b.ruleStart[rule]);
    stamp[todo.back()] = round;
    while (!todo.empty()) {
      const std::uint32_t at = todo.back();
      todo.pop_back();
      visit(at);
      const LlAtnState& q = b.states[at];
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        const LlAtnEdge& e = b.edges[q.edge + i];
        if (e.kind == LlEdgeKind::atom ||
            (e.kind == LlEdgeKind::rule && !nullable[e.label]))
          continue;
        const std::uint32_t next =
            e.kind == LlEdgeKind::rule ? e.follow : e.target;
        if (stamp[next] != round) {
          stamp[next] = round;
          todo.push_back(next);
        }
      }
    }
  };
  for (bool moved = true; moved;) {
    moved = false;
    for (std::uint16_t r = 1; r < b.cfg.nrules; ++r) {
      if (nullable[r]) continue;
      walk(r, [&](std::uint32_t at) {
        const LlAtnState& q = b.states[at];
        if (q.kind == LlAtnKind::ruleStop && q.rule == r && !nullable[r])
          nullable[r] = moved = true;
      });
    }
  }
  for (std::uint16_t r = 1; r < b.cfg.nrules; ++r)
    walk(r, [&](std::uint32_t at) {
      const LlAtnState& q = b.states[at];
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        const std::uint16_t node = b.edgeNode[q.edge + i];
        if ((b.edges[q.edge + i].kind == LlEdgeKind::atom ||
             b.edges[q.edge + i].kind == LlEdgeKind::rule) &&
            node != kNoRow && b.cfg.nodes[node].kind != LlShape::endMark)
          b.nodeHead[node] = 1;
      }
    });
  // An infix operator fills its own member, even when its primary is empty.
  for (std::uint16_t i = 0; i < b.cfg.nclimbOps; ++i) {
    const auto leaf = b.cfg.atomic_operator(b.cfg.climbOps[i].body);
    if (leaf != kNoRow) b.nodeHead[leaf] = 0;
  }
}

void settle_cuts(Built& b) {
  b.cut.assign(b.cfg.ngapConds, DynSet{});
  for (unsigned k = 0; k < b.cfg.ngapConds; ++k)
    gap_cut_alone(b.cfg, k, &b.cut[k]);
}

// What a commit's exit refuses where it follows a loop, by the loop's
// decision: the exit stands right behind the loop in its sequence.
std::vector<std::uint16_t> loop_exits(const Built& b) {
  std::vector<std::uint16_t> exits(b.cfg.ndecisions, kNoRow);
  const auto last_loop = [&](std::uint16_t id) {
    for (const LlNode* n = &b.cfg.nodes[id]; n->kind == LlShape::seq;
         n = &b.cfg.nodes[id]) {
      if (n->count == 0) break;
      id = b.cfg.kids[n->sym + n->count - 1u];
    }
    return id;
  };
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::seq) continue;
    const auto k = b.cfg.children(n);
    for (unsigned i = 0; i + 1 < k.size(); ++i) {
      const LlNode& next = b.cfg.nodes[k[i + 1]];
      if (next.kind != LlShape::condition || b.cut[next.sym].none()) continue;
      const LlNode& loop = b.cfg.nodes[last_loop(k[i])];
      if (loop.kind == LlShape::rep) exits[loop.decision] = next.sym;
    }
  }
  return exits;
}

// One token separates the arms, or the choice is left to the simulation. What
// the static separation cannot do costs a closure per parse; what it can is a
// switch.
void settle_decisions(Built& b, const LlBuildOptions& opt) {
  b.laSet.clear();
  const LlAtnView atn = view_of(b);
  LlSimulation prefix(atn, 4096);
  const std::vector<std::uint16_t> exits = loop_exits(b);
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    LlDecision& dec = b.cfg.decisions[d];
    const LlNode& n = b.cfg.nodes[dec.node];
    const DynSet& after = b.after[dec.node];
    const auto base = static_cast<std::uint32_t>(b.laSet.size());
    if (n.kind == LlShape::rep) {
      // Entering the body wins a turn; leaving it is what follows the loop.
      b.laSet.push_back(b.lookahead(n.sym, after));
      b.laSet.push_back(after);
      if (exits[d] != kNoRow) b.laSet.back().andnot(b.cut[exits[d]]);
    } else {
      const auto k = b.cfg.children(n);
      for (unsigned i = 0; i < k.size(); ++i) {
        b.laSet.push_back(b.lookahead(k[i], after));
        const LlNode& arm = b.cfg.nodes[k[i]];
        if (arm.kind == LlShape::condition)
          b.laSet.back().andnot(b.cut[arm.sym]);
      }
    }
    dec.set = base;
    // A choice the entry never reaches is never asked, and has no graph.
    if (!b.liveRules[dec.rule]) continue;
    bool disjoint = opt.staticPredict;
    const unsigned nalts = b.cfg.decisionBuild[d].nalts;
    for (unsigned i = 0; disjoint && i < nalts; ++i)
      for (unsigned j = i + 1; disjoint && j < nalts; ++j)
        disjoint = !b.laSet[base + i].intersects(b.laSet[base + j]);
    if (disjoint && b.cfg.ngapConds != 0) {
      // A guard elsewhere in the grammar does not prevent this token from
      // selecting an arm. Prove that the initial closure has no pending
      // condition, including nullable returns through any caller. A bounded
      // or conditional closure keeps the adaptive path.
      prefix.forget();
      prefix.start(dec.state, kLlCtxNone);
      disjoint =
          !prefix.capped() &&
          !prefix.needs_filter([&](std::uint16_t condition) {
            return condition < b.cfg.ngapConds && b.cut[condition].none();
          });
    }
    if (disjoint) {
      dec.how = LlPredict::single;
      continue;
    }
    // What a ceiling of tokens tells apart is a table; the rest is a closure
    // the parse runs, over the stack it actually stands on.
    dec.how = opt.staticPredict && explore_decision(b, dec, opt)
                  ? LlPredict::staged
                  : LlPredict::adaptive;
    if (dec.how == LlPredict::adaptive) ++b.nadaptive;
  }
}

// The join, interned, and its provenance kept: two runs become one chain, and
// the pair that made it is what an entry is written from.
std::uint16_t join_chain(Built& b, std::uint16_t held, std::uint16_t over) {
  const ChainBuild left =
      held == kNoRow ? ChainBuild{} : b.writes.chainBuild[held];
  const ChainBuild right =
      over == kNoRow ? ChainBuild{} : b.writes.chainBuild[over];
  std::uint16_t r;
  // Closing the same value twice on one record is idempotent. A recursive
  // adoption (parenthesized expressions, for example) can otherwise append
  // that close forever while computing the possible rows of a rule.
  if (held != kNoRow && over != kNoRow && right.makes && !right.creates) {
    const Chain x = b.writes.chains[held], y = b.writes.chains[over];
    const auto* acts = b.writes.acts.data();
    if (x.count != 0 && y.count != 0 &&
        acts[x.off + x.count - 1] == acts[y.off]) {
      std::vector<std::uint16_t> joined(acts + x.off, acts + x.off + x.count);
      joined.insert(joined.end(), acts + y.off + 1, acts + y.off + y.count);
      r = b.writes.intern_chain(joined.data(), joined.size());
    } else {
      r = b.writes.concat(held, over);
    }
  } else {
    r = b.writes.concat(held, over);
  }
  if (r == kNoRow) return r;
  // Joined chains still carry their creations: the interned chain starts with
  // an empty build row, which a later join would read as one that makes
  // nothing.
  b.writes.chainBuild[r] = {left.makes || right.makes,
                            left.creates || right.creates,
                            left.lead != kNoRow ? left.lead : right.lead};
  if (r >= b.nestChains.size()) b.nestChains.resize(b.writes.nchains, 0);
  const bool leftNest = held < b.nestChains.size() && b.nestChains[held] != 0;
  const bool rightNest = over < b.nestChains.size() && b.nestChains[over] != 0;
  b.nestChains[r] = leftNest || rightNest;
  if (r >= b.origin.size())
    b.origin.resize(b.writes.nchains, LlFuse{kNoRow, kNoRow});
  if (r != held && r != over && b.origin[r].from == kNoRow)
    b.origin[r] = LlFuse{held, over};
  return r;
}

void settle_fusion(Built& b) {
  b.origin.assign(b.writes.nchains, LlFuse{kNoRow, kNoRow});
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    if (!n.owns || n.kind != LlShape::seq || n.count != 1 ||
        n.row >= b.nestChains.size() || b.nestChains[n.row] == 0 ||
        b.cfg.nodeBuild[id].foldStep)
      continue;
    const LlNode& only = b.cfg.nodes[b.cfg.kids[n.sym]];
    if (only.kind != LlShape::term) continue;
    const std::uint16_t stated =
        only.row == kNoRow ? n.row : join_chain(b, n.row, only.row);
    b.cfg.nodeBuild[id].leafRow =
        join_chain(b, b.cfg.terms[only.sym].element, stated);
  }
  // A terminal's own conversion joins its position's chain first: what a leaf
  // carries is then one row, and every walk below reads it as such.
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    LlNode& n = b.cfg.nodes[id];
    if (n.kind != LlShape::term) continue;
    const std::uint16_t own = b.cfg.terms[n.sym].element;
    n.row = n.row == kNoRow ? own : join_chain(b, own, n.row);
  }
  LlRecordPlan records(b.cfg, b.writes, b.nestChains);
  const auto join = [&](std::uint16_t held, std::uint16_t over) {
    return join_chain(b, held, over);
  };
  records.settle_rows(b.nullable, join);
  b.fuseBase.assign(b.cfg.nnodes + 1, 0);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    b.fuseBase[id] = static_cast<std::uint32_t>(b.fuse.size());
    if (n.kind != LlShape::call || n.row == kNoRow || n.owns) continue;
    for (std::uint16_t x : records.rule_rows(n.sym))
      b.fuse.push_back(LlFuse{x, join_chain(b, x, n.row)});
  }
  b.fuseBase[b.cfg.nnodes] = static_cast<std::uint32_t>(b.fuse.size());
  // ...and what an owning group would ride onto when the text gives it one
  // position and no more: a record over the group would then cover exactly
  // what that position's covers, and two records over one span is a node
  // nobody wrote. Two creations refuse each other, so the join is not offered
  // there and the group lays its own.
  b.ownBase.assign(b.cfg.nnodes + 1, 0);
  for (std::uint32_t id = 0; id < b.cfg.nnodes; ++id) {
    const LlNode& n = b.cfg.nodes[id];
    b.ownBase[id] = static_cast<std::uint32_t>(b.fuse.size());
    if (!n.owns || n.row == kNoRow) continue;
    records.own_joins(static_cast<std::uint16_t>(id), join,
                      [&](const LlFuse& f) { b.fuse.push_back(f); });
  }
  b.ownBase[b.cfg.nnodes] = static_cast<std::uint32_t>(b.fuse.size());

  // A climb's three positions state their members over records too: the
  // operand's, whichever reading of it the text took, and -- from the second
  // turn on -- the node the climb itself just laid.
  for (std::uint16_t k = 0; k < b.cfg.nclimbs; ++k) {
    LlClimb& cl = b.cfg.climbs[k];
    std::vector<std::uint16_t> hands =
        cl.primary != kNoRow
            ? records.rule_rows(cl.primary)
            : std::vector<std::uint16_t>{b.cfg.nodes[cl.primaryNode].row};
    LlRecordPlan::add(hands, cl.node);
    cl.leftBase = static_cast<std::uint32_t>(b.fuse.size());
    for (std::uint16_t h : hands)
      b.fuse.push_back(LlFuse{h, join_chain(b, h, cl.leftRow)});
    cl.leftCount = static_cast<std::uint32_t>(b.fuse.size()) - cl.leftBase;
    cl.rightBase = static_cast<std::uint32_t>(b.fuse.size());
    for (std::uint16_t h : hands)
      b.fuse.push_back(LlFuse{h, join_chain(b, h, cl.rightRow)});
    cl.rightCount = static_cast<std::uint32_t>(b.fuse.size()) - cl.rightBase;
  }
}

// The repetitions that take another turn before they return to the caller
// when both read the text: a token of the caller's, longer, does not override
// a continuation of the body.
std::vector<std::uint8_t> repeat_scopes(const Built& b, const LlAtnView& atn,
                                        const std::uint8_t* shared) {
  std::vector<std::uint8_t> out(b.cfg.ndecisions, 0);
  LlReturnPool pool;
  pool.read_returns_by_class(atn.returnClass);
  LlEpisodeSim sim(atn, pool, false);
  sim.share_calls(shared);
  std::vector<LlThread> seeded;
  std::vector<std::pair<std::uint32_t, std::uint16_t>> visited;
  for (std::uint16_t d = 0; d < b.cfg.ndecisions; ++d) {
    const LlDecision& dec = b.cfg.decisions[d];
    if (!b.cfg.repeats(dec) || !b.liveRules[dec.rule]) continue;
    sim.reset();
    visited.clear();
    sim.seed(dec.state, kLlCtxNone, seeded, &visited);
    bool local = false, caller = false;
    for (const auto& [at, root] : visited) {
      if (root != 1) continue;
      const auto& state = atn.state(at);
      caller = caller ||
               (state.kind == LlAtnKind::ruleStop && state.rule == dec.rule);
      for (const LlAtnEdge& e : atn.outgoing(at))
        if (e.kind == LlEdgeKind::atom) {
          local = local || state.rule == dec.rule;
          caller = caller || state.rule != dec.rule;
        }
    }
    out[d] = !local || caller;
  }
  return out;
}

// What an episode reads besides the graph, settled here so that no parse
// works it out on its first episode. Every table keeps them: an automaton the
// construction closed can still leave a choice to the episode.
void settle_episode_flags(Built& b) {
  const LlAtnView atn = view_of(b);
  const auto none = [](const std::vector<std::uint8_t>& flags) {
    return std::ranges::none_of(flags, [](std::uint8_t f) { return f != 0; });
  };
  std::vector<std::uint8_t> shared = ll_shared_rules(atn, b.cfg.nrules);
  b.repeatScopes = repeat_scopes(b, atn, shared.data());
  if (none(b.repeatScopes)) b.repeatScopes.clear();
  if (!none(shared)) b.sharedRules = std::move(shared);
}

}  // namespace

std::vector<std::uint8_t> ll_shared_rules(const LlAtnView& atn,
                                          std::uint16_t nrules) {
  std::vector<std::uint8_t> nullable(nrules, 0), shared(nrules, 0);
  std::vector<std::uint32_t> seen(atn.nstates, 0), todo;
  // Each rule walks its own states once. A call waits until its rule can
  // return unread; conditions, like tokens, stop this unguarded walk.
  {
    std::vector<std::vector<std::uint32_t>> waiting(nrules);
    for (std::uint16_t r = 0; r < nrules; ++r) todo.push_back(atn.ruleStart[r]);
    while (!todo.empty()) {
      const auto at = todo.back();
      todo.pop_back();
      if (seen[at] != 0) continue;
      seen[at] = 1;
      const auto& state = atn.state(at);
      if (state.kind == LlAtnKind::ruleStop && nullable[state.rule] == 0) {
        nullable[state.rule] = 1;
        const auto& calls = waiting[state.rule];
        todo.insert(todo.end(), calls.begin(), calls.end());
      }
      for (const LlAtnEdge& e : atn.outgoing(at)) {
        if (e.kind == LlEdgeKind::epsilon)
          todo.push_back(e.target);
        else if (e.kind == LlEdgeKind::rule) {
          if (nullable[e.label] != 0)
            todo.push_back(e.follow);
          else
            waiting[e.label].push_back(e.follow);
        }
      }
    }
    std::ranges::fill(seen, 0);
  }
  // A rule the entry never reaches has no states, its start being another's,
  // and no edge calls it: the regions below number the others densely.
  std::vector<std::uint16_t> live, dense(nrules, kNoRow);
  for (std::uint16_t r = 0; r < nrules; ++r)
    if (atn.state(atn.ruleStart[r]).rule == r) {
      dense[r] = static_cast<std::uint16_t>(live.size());
      live.push_back(r);
    }
  const std::size_t words = (live.size() + 63u) / 64u;
  std::uint32_t round = 0;
  // The rules called from `from` before a token is read, into `bits`:
  // the visitor either records a direct call or folds its settled region.
  const auto scan = [&](std::uint32_t from, auto&& call) {
    ++round;
    todo.assign(1, from);
    while (!todo.empty()) {
      const std::uint32_t at = todo.back();
      todo.pop_back();
      if (seen[at] == round) continue;
      seen[at] = round;
      for (const LlAtnEdge& e : atn.outgoing(at)) {
        if (e.kind == LlEdgeKind::epsilon) {
          todo.push_back(e.target);
        } else if (e.kind == LlEdgeKind::rule) {
          call(dense[e.label]);
          if (nullable[e.label] != 0) todo.push_back(e.follow);
        }
      }
    }
  };
  std::vector<std::vector<std::uint32_t>> heads(live.size());
  for (std::uint16_t r = 0; r < live.size(); ++r) {
    scan(atn.ruleStart[live[r]],
         [&](auto called) { heads[r].push_back(called); });
    auto& row = heads[r];
    std::ranges::sort(row);
    row.erase(std::unique(row.begin(), row.end()), row.end());
  }
  const auto scc = StrongComponents<>::of(heads);
  const auto count =
      live.empty() ? 0u : *std::ranges::max_element(scc.component) + 1u;
  std::vector<std::vector<std::uint16_t>> members(count);
  std::vector<std::uint64_t> region(std::size_t{count} * words, 0);
  for (std::uint16_t r = 0; r < live.size(); ++r) {
    members[scc.component[r]].push_back(r);
    region[std::size_t{scc.component[r]} * words + (r >> 6)] |=
        std::uint64_t{1} << (r & 63u);
  }
  // Tarjan closes dependencies before their callers. Each component's
  // transitive region is therefore folded once per direct outgoing edge.
  for (std::uint32_t c = 0; c < count; ++c)
    for (const auto r : members[c])
      for (const auto called : heads[r]) {
        const auto to = scc.component[called];
        if (to == c) continue;
        for (std::size_t w = 0; w < words; ++w)
          region[std::size_t{c} * words + w] |=
              region[std::size_t{to} * words + w];
      }
  std::vector<std::uint64_t> bits(words), once(words), twice(words);
  const auto include = [&](std::uint16_t rule) {
    for (std::size_t w = 0; w < words; ++w)
      bits[w] |= region[std::size_t{scc.component[rule]} * words + w];
  };
  for (std::uint32_t at = 0; at < atn.nstates; ++at) {
    if (atn.state(at).nedges < 2) continue;
    std::ranges::fill(once, 0);
    for (const LlAtnEdge& e : atn.outgoing(at)) {
      std::ranges::fill(bits, 0);
      if (e.kind == LlEdgeKind::epsilon) {
        scan(e.target, include);
      } else if (e.kind == LlEdgeKind::rule) {
        include(dense[e.label]);
        if (nullable[e.label] != 0) scan(e.follow, include);
      }
      for (std::size_t w = 0; w < words; ++w) {
        twice[w] |= once[w] & bits[w];
        once[w] |= bits[w];
      }
    }
  }
  for (std::uint16_t r = 0; r < live.size(); ++r)
    if (((twice[r >> 6] >> (r & 63u)) & 1u) != 0 && nullable[live[r]] == 0)
      shared[live[r]] = 1;
  return shared;
}

LlBuiltProgram ll_build_program(const LlLowered& low, LlBuildOptions opt) {
  Built b;
  b.cfg = low.cfg;
  b.writes = low.writes;
  b.nestChains = low.nestChains;
  b.fault = low.fault;
  b.liveRules = reached_from(b.cfg, b.cfg.entryRule);
  refuse_skipped_terminal(b);
  settle_nullable(b.cfg, b.nullable);
  const auto empty = refuse_non_consuming(b);
  settle_first(b.cfg, b.nullable, b.first, b.ruleFirst);
  const bool leftCyclic = refuse_left_recursion(b, empty);
  refuse_unproductive(b);
  refuse_precedence_clash(b);
  // A left-cyclic rule has no fixpoint for the walks below: the follow pass
  // would descend it for ever.
  if (leftCyclic) return b;
  settle_after(b.cfg, b.nullable, b.first, b.ruleFirst, b.after, b.ruleFollow);
  settle_cuts(b);
  build_atn(b);
  settle_gaps(b);
  settle_decisions(b, opt);
  refuse_section_lookahead(b);
  settle_repair(b);
  settle_fusion(b);
  if (b.writes.capped) b.cap_hit("the chain pool");
  detail::ll_build_lexer(b);
  if (b.cfg.lowerFail()) return b;
  settle_after_states(b);
  settle_head_nodes(b);
  settle_episode_flags(b);
  return b;
}

}  // namespace cursiv
