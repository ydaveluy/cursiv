#include <algorithm>
#include <bit>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include "cursiv/ll/gen/LlLowerPool.hpp"
#include "cursiv/ll/gen/LlRecordPlan.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/table/Symbol.hpp"

namespace cursiv::engine::llow {

std::uint16_t put_node(LlLowered& o, const LlNode& n) {
  if (o.cfg.nnodes >= kNoRow) too_many_positions();
  const auto id = static_cast<std::uint16_t>(o.cfg.nnodes);
  o.cfg.nodes[o.cfg.nnodes++] = n;
  return id;
}

std::uint16_t put_group(LlLowered& o, LlNode n,
                        std::span<const std::uint16_t> ids) {
  if (std::uint64_t{o.cfg.nchildIds} + ids.size() > kNoRow)
    too_many_positions();
  n.sym = static_cast<std::uint16_t>(o.cfg.nchildIds);
  n.count = static_cast<std::uint16_t>(ids.size());
  std::ranges::copy(ids, o.cfg.childIds.row(o.cfg.nchildIds, ids.size()));
  o.cfg.nchildIds += ids.size();
  return put_node(o, n);
}

std::uint16_t with_decision(LlLowered& o, std::uint16_t node,
                            std::uint16_t nalts, std::uint16_t owner) {
  if (o.cfg.ndecisions >= kNoRow) too_many_positions();
  o.cfg.nodes[node].decision = static_cast<std::uint16_t>(o.cfg.ndecisions);
  o.cfg.decisions[o.cfg.ndecisions++] =
      LlDecision{.node = node, .rule = owner, .nalts = nalts};
  return node;
}

// A directive over `eof`, or over nothing, standing as one position among
// others: no record carries it.
inline void no_write_off_position(LlLowered& o, const Node& k,
                                  std::uint16_t owner) {
  if ((k.kind == Shape::none || k.kind == Shape::end) &&
      (!k.made.empty() || !k.row.empty()))
    o.fault.raise(GrammarFaultCode::directiveOnNoPosition, owner);
}

// An option over a zero-width item says nothing its absence does not.
inline void refuse_item(LlLowered& o, const Pool& p, const Node& e,
                        std::uint16_t owner) {
  const Node& in = p.e[e.sym];
  no_write_off_position(o, in, owner);
  unsigned ends = 0;
  if (in.kind == Shape::alt)
    for (unsigned i = 0; i < in.count; ++i)
      ends += p.e[p.child[in.sym + i]].kind == Shape::end ? 1u : 0u;
  const bool end =
      in.kind == Shape::end || (in.kind == Shape::alt && ends == in.count);
  if (e.max != 1 || e.min != 0) return;
  if (end) o.fault.raise(GrammarFaultCode::optionalEndMark, owner);
  if (in.kind == Shape::condition)
    o.fault.raise(GrammarFaultCode::optionalCondition, owner);
}

inline std::uint16_t refuse_node(LlLowered& o, LowerFail why,
                                 std::uint16_t owner) {
  lower_refuse_rule(o.cfg, why, owner);
  return put_node(o, LlNode{.kind = LlShape::empty});
}

inline Descent<std::uint16_t> emit_item(LlLowered& o, const Pool& p,
                                        std::uint16_t k, std::uint16_t owner) {
  no_write_off_position(o, p.e[k], owner);
  co_return co_await emit(o, p, k, p.e[k].made, p.e[k].row, owner);
}

inline Descent<std::uint16_t> emit_seq(LlLowered& o, const Pool& p,
                                       const Node& e, const Acts& made,
                                       const Acts& row, std::uint16_t owner) {
  std::vector<std::uint16_t> ids(e.count);
  for (unsigned i = 0; i < e.count; ++i)
    ids[i] = co_await emit_item(o, p, p.child[e.sym + i], owner);
  co_return put_group(o,
                      LlNode{.kind = LlShape::seq,
                             .row = o.writes.intern_cat(made, row),
                             .cfg = e.cfg,
                             .section = e.section,
                             .foldStep = e.foldStep},
                      ids);
}

inline Descent<std::uint16_t> emit_alt(LlLowered& o, const Pool& p,
                                       std::uint16_t at, const Acts& made,
                                       const Acts& row, std::uint16_t owner) {
  const Node& e = p.e[at];
  unsigned nEnd = 0;
  for (unsigned i = 0; i < e.count; ++i)
    if (p.e[p.child[e.sym + i]].kind == Shape::end) ++nEnd;
  if (nEnd > 1) o.fault.raise(GrammarFaultCode::twoEndMarks, owner);
  std::vector<std::uint16_t> ids(e.count);
  for (unsigned i = 0; i < e.count; ++i) {
    const std::uint16_t k = p.child[e.sym + i];
    ids[i] = co_await emit(o, p, k, cat(p.e[k].made, made),
                           cat(p.e[k].row, row), owner);
  }
  const std::uint16_t id = put_group(
      o, LlNode{.kind = LlShape::alt, .cfg = e.cfg, .section = e.section}, ids);
  co_return with_decision(o, id, e.count, owner);
}

inline Descent<std::uint16_t> emit_unordered(LlLowered& o, const Pool& p,
                                             const Node& e, const Acts& made,
                                             const Acts& row,
                                             std::uint16_t owner) {
  const auto group = [&](LlShape kind, const std::vector<std::uint16_t>& ids,
                         std::uint16_t rule, std::uint16_t chain = kNoRow,
                         bool foldStep = false) {
    const auto id = put_group(
        o,
        LlNode{.kind = kind, .row = chain, .cfg = e.cfg, .foldStep = foldStep},
        ids);
    return kind == LlShape::alt
               ? with_decision(o, id, static_cast<std::uint16_t>(ids.size()),
                               rule)
               : id;
  };
  const auto tooWide = [&] {
    return refuse_node(o, LowerFail::unorderedTooWide, owner);
  };
  if (e.count > unordered_most(e.unorderedTurns)) co_return tooWide();
  if (e.unorderedTurns) {
    std::vector<std::uint16_t> order(e.count), arms;
    std::iota(order.begin(), order.end(), 0);
    const auto chain = o.writes.intern_cat(made, row);
    // A nest reaches back over preceding operands within the same sequence.
    do {
      std::vector<std::uint16_t> ids;
      for (const auto i : order)
        ids.push_back(co_await emit_item(o, p, p.child[e.sym + i], owner));
      arms.push_back(group(LlShape::seq, ids, owner, chain, e.foldStep));
    } while (std::ranges::next_permutation(order).found);
    co_return group(LlShape::alt, arms, owner);
  }
  if (std::uint32_t{o.cfg.nrules} + (1u << e.count) - 1u > kNtBase)
    co_return tooWide();
  // Where only optional items are left, the group may end there.
  const auto ends = [&](std::uint32_t mask) {
    return (mask & ~e.unorderedOptional) == 0;
  };
  std::uint64_t spelt = o.cfg.nnodes;
  for (std::uint32_t mask = 1; mask < (1u << e.count); ++mask) {
    const auto bits = static_cast<unsigned>(std::popcount(mask));
    spelt += bits > 1 ? 3 * bits + 1 : 1;
    if (ends(mask)) ++spelt;
  }
  if (spelt > kNoRow) co_return tooWide();
  const std::uint32_t subsets = 1u << e.count;
  const std::uint16_t baseRule = o.cfg.nrules;
  const LlRuleInfo named = o.cfg.rules[owner];
  for (std::uint32_t mask = 1; mask < subsets; ++mask) {
    const auto rule = o.cfg.nrules++;
    o.cfg.rules[rule] = {.nameOff = named.nameOff, .nameLen = named.nameLen};
    o.cfg.ruleCfg[rule] = e.cfg;
  }
  // Auxiliary calls share suffix rules, while every operand keeps its own site.
  // A position belongs to the rule whose body holds it, as its graph states do.
  for (std::uint32_t mask = 1; mask < subsets; ++mask) {
    const auto aux = static_cast<std::uint16_t>(baseRule + mask - 1u);
    std::vector<std::uint16_t> arms;
    for (unsigned i = 0; i < e.count; ++i) {
      const std::uint32_t bit = 1u << i;
      if ((mask & bit) == 0) continue;
      const auto item = co_await emit_item(o, p, p.child[e.sym + i], aux);
      const std::uint32_t remaining = mask ^ bit;
      if (remaining == 0) {
        arms.push_back(item);
      } else {
        const auto suffix = put_node(
            o,
            LlNode{.kind = LlShape::call,
                   .sym = static_cast<std::uint16_t>(baseRule + remaining - 1u),
                   .cfg = e.cfg});
        arms.push_back(group(LlShape::seq, {item, suffix}, aux));
      }
    }
    std::uint16_t body =
        arms.size() == 1 ? arms.front() : group(LlShape::alt, arms, aux);
    // What is left where the group may end is an option like any other: an
    // item still read is the preferred reading, and its absence is the
    // caller's to read past.
    if (ends(mask))
      body = with_decision(o,
                           put_node(o, LlNode{.kind = LlShape::rep,
                                              .sym = body,
                                              .min = 0,
                                              .max = 1,
                                              .cfg = e.cfg}),
                           2, aux);
    o.cfg.rules[aux].body = body;
  }
  co_return put_node(
      o, LlNode{.kind = LlShape::call,
                .sym = static_cast<std::uint16_t>(baseRule + subsets - 2u),
                .row = o.writes.intern_cat(made, row),
                .cfg = e.cfg,
                .section = e.section,
                .foldStep = e.foldStep});
}

Descent<std::uint16_t> emit(LlLowered& o, const Pool& p, std::uint16_t at,
                            const Acts& made, const Acts& row,
                            std::uint16_t owner) {
  const Node& e = p.e[at];
  const auto place = [&](LlShape kind, std::uint16_t sym,
                         std::uint32_t min = 0) {
    return put_node(o, LlNode{.kind = kind,
                              .sym = sym,
                              .min = min,
                              .row = o.writes.intern_cat(made, row, e.doc),
                              .cfg = e.cfg,
                              .section = e.section});
  };
  switch (e.kind) {
    case Shape::sym:
      co_return is_term(e.sym) ? place(LlShape::term, e.sym)
                               : place(LlShape::call, nt_index(e.sym));
    case Shape::none:
      co_return put_node(o, LlNode{.kind = LlShape::empty,
                                   .row = o.writes.intern_cat(made, row)});
    case Shape::condition:
      co_return place(LlShape::condition, e.sym);
    case Shape::implied:
      co_return place(LlShape::implied, e.sym, e.min);
    case Shape::end:
      co_return place(LlShape::endMark, e.sym);
    case Shape::seq:
      co_return co_await emit_seq(o, p, e, made, row, owner);
    case Shape::alt:
      co_return co_await emit_alt(o, p, at, made, row, owner);
    case Shape::unordered:
      co_return co_await emit_unordered(o, p, e, made, row, owner);
    case Shape::rep: {
      refuse_item(o, p, e, owner);
      const auto body = [&] { return emit_item(o, p, e.sym, owner); };
      if ((e.max != kEndless && (e.max > 1 || e.min == e.max)) ||
          e.min > 1) {
        // Each counted occurrence is its own call site: the body and the ATN
        // are both expanded, as the body and the caret read one `nodeAfter`.
        // Nested options keep one ordered prefix.
        const std::uint64_t turns = e.max == kEndless ? e.min : e.max;
        const std::uint64_t optional = e.max == kEndless ? 0 : e.max - e.min;
        if (const std::uint64_t spelt =
                turns + (optional == 0 ? 1 : 2 * optional);
            o.cfg.nnodes + spelt > kNoRow)
          co_return refuse_node(o, LowerFail::tooManyPositions, owner);
        const auto sequence = [&](const std::vector<std::uint16_t>& ids,
                                  std::uint16_t chain, bool outer = false) {
          return put_group(
              o,
              LlNode{.kind = ids.empty() ? LlShape::empty : LlShape::seq,
                     .row = chain,
                     .cfg = e.cfg,
                     .section = e.section,
                     .foldStep = outer && e.foldStep},
              ids);
        };
        const auto repetition = [&](std::uint16_t child,
                                    std::uint32_t maximum) {
          const auto id = put_node(o, LlNode{.kind = LlShape::rep,
                                             .sym = child,
                                             .min = 0,
                                             .max = maximum,
                                             .cfg = e.cfg,
                                             .section = e.section});
          return with_decision(o, id, 2, owner);
        };
        std::vector<std::uint16_t> ids;
        ids.reserve(e.min + (e.min != e.max));
        for (std::uint32_t count = 0; count < e.min; ++count)
          ids.push_back(co_await body());
        if (e.max == kEndless) {
          ids.push_back(repetition(co_await body(), kEndless));
        } else if (e.max > e.min) {
          // D? nested as (D (D (... D?)?)?)? keeps the greedy arm first.
          std::uint16_t tail = kNoRow;
          for (std::uint32_t count = e.min; count < e.max; ++count) {
            auto child = co_await body();
            if (tail != kNoRow) {
              child = sequence({child, tail}, kNoRow);
              o.cfg.nodes[child].countedTail = true;
            }
            tail = repetition(child, 1);
            o.cfg.nodes[tail].countedTail = true;
          }
          ids.push_back(tail);
        }
        co_return sequence(ids, o.writes.intern_cat(made, row), true);
      }
      const std::uint16_t child = co_await body();
      const std::uint16_t id =
          put_node(o, LlNode{.kind = LlShape::rep,
                             .sym = child,
                             .min = e.min,
                             .max = e.max,
                             .row = o.writes.intern_cat(made, row),
                             .cfg = e.cfg,
                             .section = e.section,
                             .leftFold = e.leftFold});
      // The turn is the choice: one arm enters the body, the other leaves.
      co_return with_decision(o, id, 2, owner);
    }
  }
  co_return put_node(o, LlNode{.kind = LlShape::empty});
}

// The chain of a group that stands for one position goes to that position: two
// records over one span would make a node nobody wrote. A child's creation
// stays apart, so the caller's writes stay apart from the child's.
void move_chains_down(LlLowered& o, const LlRecordPlan& plan) {
  for (std::uint32_t i = 0; i < o.cfg.nnodes; ++i) {
    const auto id = static_cast<std::uint16_t>(i);
    LlNode& n = o.cfg.nodes[id];
    if (n.row == kNoRow || n.foldStep) continue;
    const std::uint16_t only = plan.single_position(id);
    if (only == kNoRow || !plan.hands(only)) continue;
    LlNode& child = o.cfg.nodes[only];
    // An outer write belongs to the caller even when the child is the same
    // AST class. Fusion below keeps the two action runs' provenance; plain
    // concatenation here would mistake that write for a child member fill.
    if (plan.opens_chain(child.row) ||
        (child.kind == LlShape::call && plan.rule_opens(child.sym)))
      continue;
    if (child.kind == LlShape::term && o.writes.chainBuild[n.row].nest)
      continue;
    child.row = o.writes.concat(n.row, child.row);
    n.row = kNoRow;
  }
}

// A position whose chain no record of its own carries opens one: a group
// always, and a call whose rule creates none or that turns.
void add_own_records(LlLowered& o, const LlRecordPlan& plan) {
  for (std::uint32_t id = 0; id < o.cfg.nnodes; ++id) {
    using enum LlShape;
    LlNode& n = o.cfg.nodes[id];
    if (n.row == kNoRow || n.kind == term || n.kind == implied) continue;
    n.owns = n.kind != call || !plan.rule_hands(n.sym) ||
             o.writes.chainBuild[n.row].nest;
  }
}

Descent<void> emit_operator_nodes(LlLowered& o, const Pool& p, std::uint16_t at,
                                  const Acts& made, const Acts& row,
                                  std::uint8_t prec, Association group,
                                  std::uint8_t cfg,
                                  std::vector<std::uint16_t> before,
                                  std::vector<std::uint16_t> after) {
  const Node& n = p.e[at];
  if (n.kind == Shape::sym && is_term(n.sym)) {
    const auto token =
        put_node(o, LlNode{.kind = LlShape::term,
                           .sym = n.sym,
                           .row = o.writes.intern_cat(made, row, n.doc),
                           .cfg = cfg});
    const auto guard = [&](const std::vector<std::uint16_t>& conditions) {
      if (conditions.empty()) return kNoRow;
      const auto condition = conditions.size() == 1
                                 ? conditions.front()
                                 : o.cfg.gap_conjoin(conditions);
      return condition == kNoRow
                 ? kNoRow
                 : put_node(o, LlNode{.kind = LlShape::condition,
                                      .sym = condition,
                                      .cfg = cfg});
    };
    const auto head = guard(before);
    const auto tail = guard(after);
    auto body = token;
    if (head != kNoRow || tail != kNoRow) {
      std::uint16_t run[3];
      std::uint16_t count = 0;
      if (head != kNoRow) run[count++] = head;
      run[count++] = token;
      if (tail != kNoRow) run[count++] = tail;
      body = put_group(o, LlNode{.kind = LlShape::seq, .cfg = cfg},
                       std::span(run, count));
    }
    o.cfg.climbOps[o.cfg.nclimbOps++] =
        LlClimbOp{.body = body, .prec = prec, .assoc = group};
    co_return;
  }
  if (n.kind == Shape::alt) {
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = p.child[n.sym + i];
      co_await emit_operator_nodes(o, p, k, cat(p.e[k].made, made),
                                   cat(p.e[k].row, row), prec, group, cfg,
                                   before, after);
    }
    co_return;
  }
  if (n.kind == Shape::seq) {
    std::uint16_t token = kNoRow;
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = p.child[n.sym + i];
      if (p.e[k].kind == Shape::condition)
        (token == kNoRow ? before : after).push_back(p.e[k].sym);
      else if (p.e[k].kind != Shape::none)
        token = k;
    }
    if (token != kNoRow) {
      co_await emit_operator_nodes(o, p, token, cat(p.e[token].made, made),
                                   cat(p.e[token].row, row), prec, group, cfg,
                                   std::move(before), std::move(after));
      co_return;
    }
  }
  // Losing an operator silently changes the language and precedence.
  o.fault.raise(GrammarFaultCode::infixOperatorNotText, p.owner);
}

}  // namespace cursiv::engine::llow
