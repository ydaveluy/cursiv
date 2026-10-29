#include <algorithm>
#include <bit>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include "cursiv/engines/ll/gen/LlLowerPool.hpp"
#include "cursiv/engines/ll/gen/LlRecordPlan.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv::detail::llow {

Acts cat(const Acts& a, const Acts& b) {
  Acts o = a;
  o.insert(o.end(), b.begin(), b.end());
  return o;
}

std::uint16_t put_node(LlLowered& o, const LlNode& n,
                       const LlNodeBuild& b) {
  if (o.cfg.nnodes >= kNoRow) too_many_positions();
  const auto id = static_cast<std::uint16_t>(o.cfg.nnodes);
  o.cfg.nodeBuild[o.cfg.nnodes] = b;
  o.cfg.nodes[o.cfg.nnodes++] = n;
  return id;
}

std::uint16_t put_group(LlLowered& o, LlShape kind,
                        std::span<const std::uint16_t> ids, std::uint16_t row,
                        const LlNodeBuild& b) {
  if (std::uint64_t{o.cfg.nkids} + ids.size() > kNoRow) too_many_positions();
  const auto base = static_cast<std::uint16_t>(o.cfg.nkids);
  std::ranges::copy(ids, o.cfg.kids.row(o.cfg.nkids, ids.size()));
  o.cfg.nkids += ids.size();
  return put_node(o,
                  LlNode{.kind = kind,
                         .sym = base,
                         .count = static_cast<std::uint16_t>(ids.size()),
                         .row = row},
                  b);
}

std::uint16_t with_decision(LlLowered& o, std::uint16_t node,
                            std::uint16_t nalts, std::uint16_t owner) {
  if (o.cfg.ndecisions >= kNoRow) too_many_positions();
  o.cfg.nodes[node].decision = static_cast<std::uint16_t>(o.cfg.ndecisions);
  o.cfg.decisionBuild[o.cfg.ndecisions].nalts = nalts;
  o.cfg.decisions[o.cfg.ndecisions++] = LlDecision{.node = node, .rule = owner};
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

// An option over a zero-width item says nothing its absence does not, and a
// repetition over an item that may hold no position repeats nothing.
inline void refuse_item(LlLowered& o, const Pool& p, const Node& e,
                        std::uint16_t owner) {
  const Node& in = p.e[e.sym];
  no_write_off_position(o, in, owner);
  unsigned ends = 0;
  if (in.kind == Shape::alt)
    for (unsigned i = 0; i < in.count; ++i)
      ends += p.e[p.kid[in.sym + i]].kind == Shape::end ? 1u : 0u;
  const bool end =
      in.kind == Shape::end || (in.kind == Shape::alt && ends == in.count);
  if (e.max == 1) {
    if (e.min == 0 && end)
      o.fault.raise(GrammarFaultCode::optionalEndMark, owner);
    if (e.min == 0 && in.kind == Shape::condition)
      o.fault.raise(GrammarFaultCode::optionalCondition, owner);
    return;
  }
  if (end || ends != 0 || in.kind == Shape::none ||
      (in.kind == Shape::rep && in.min == 0))
    o.fault.raise(GrammarFaultCode::nullableRepetition, owner);
}

inline std::uint16_t refuse_node(LlLowered& o, LowerFail why,
                                 std::uint16_t owner) {
  lower_refuse_rule(o.cfg, why, owner);
  return put_node(o, LlNode{.kind = LlShape::empty});
}

inline std::uint16_t emit_item(LlLowered& o, const Pool& p, std::uint16_t k,
                               std::uint16_t owner) {
  no_write_off_position(o, p.e[k], owner);
  return emit(o, p, k, p.e[k].made, p.e[k].row, owner);
}

inline std::uint16_t emit_seq(LlLowered& o, const Pool& p, const Node& e,
                              const Acts& made, const Acts& row,
                              std::uint16_t owner) {
  std::vector<std::uint16_t> ids(e.count);
  for (unsigned i = 0; i < e.count; ++i)
    ids[i] = emit_item(o, p, p.kid[e.sym + i], owner);
  return put_group(
      o, LlShape::seq, ids, o.writes.intern_cat(made, row),
      LlNodeBuild{.cfg = e.cfg, .section = e.section, .foldStep = e.foldStep});
}

inline std::uint16_t emit_alt(LlLowered& o, const Pool& p, std::uint16_t at,
                              const Acts& made, const Acts& row,
                              std::uint16_t owner) {
  const Node& e = p.e[at];
  unsigned nEnd = 0;
  for (unsigned i = 0; i < e.count; ++i)
    if (p.e[p.kid[e.sym + i]].kind == Shape::end) ++nEnd;
  if (nEnd > 1) o.fault.raise(GrammarFaultCode::twoEndMarks, owner);
  std::vector<std::uint16_t> ids(e.count);
  for (unsigned i = 0; i < e.count; ++i) {
    const std::uint16_t k = p.kid[e.sym + i];
    ids[i] = emit(o, p, k, cat(p.e[k].made, made), cat(p.e[k].row, row), owner);
  }
  const std::uint16_t id = put_group(
      o, LlShape::alt, ids, kNoRow,
      LlNodeBuild{.cfg = e.cfg, .section = e.section});
  return with_decision(o, id, e.count, owner);
}

inline std::uint16_t emit_unordered(LlLowered& o, const Pool& p, const Node& e,
                                    const Acts& made, const Acts& row,
                                    std::uint16_t owner) {
  const auto group = [&](LlShape kind, const std::vector<std::uint16_t>& ids,
                         std::uint16_t rule, std::uint16_t chain = kNoRow,
                         bool foldStep = false) {
    const auto id = put_group(o, kind, ids, chain,
                              LlNodeBuild{.cfg = e.cfg, .foldStep = foldStep});
    return kind == LlShape::alt
               ? with_decision(o, id, static_cast<std::uint16_t>(ids.size()),
                               rule)
               : id;
  };
  const auto tooWide = [&] {
    return refuse_node(o, LowerFail::unorderedTooWide, owner);
  };
  if (e.count > unordered_most(e.unorderedTurns)) return tooWide();
  if (e.unorderedTurns) {
    std::vector<std::uint16_t> order(e.count), arms;
    std::iota(order.begin(), order.end(), 0);
    const auto chain = o.writes.intern_cat(made, row);
    // A nest reaches back over preceding operands within the same sequence.
    do {
      std::vector<std::uint16_t> ids;
      for (const auto i : order)
        ids.push_back(emit_item(o, p, p.kid[e.sym + i], owner));
      arms.push_back(group(LlShape::seq, ids, owner, chain, e.foldStep));
    } while (std::ranges::next_permutation(order).found);
    return group(LlShape::alt, arms, owner);
  }
  if (std::uint32_t{o.cfg.nrules} + (1u << e.count) - 1u > kNtBase)
    return tooWide();
  // Where only optional items are left, the group may end there.
  const auto ends = [&](std::uint32_t mask) {
    return (mask & ~e.unorderedOptional) == 0;
  };
  std::uint64_t spelt = o.cfg.nnodes;
  for (std::uint32_t mask = 1; mask < (1u << e.count); ++mask) {
    const auto bits = static_cast<unsigned>(std::popcount(mask));
    spelt += bits > 1 ? 3 * bits + 1 : 1;
    if (ends(mask)) spelt += bits > 1 ? 1 : 2;
  }
  if (spelt > kNoRow) return tooWide();
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
      const auto item = emit_item(o, p, p.kid[e.sym + i], aux);
      const std::uint32_t remaining = mask ^ bit;
      if (remaining == 0) {
        arms.push_back(item);
      } else {
        const auto suffix = put_node(o,
                                     LlNode{.kind = LlShape::call,
                                            .sym = static_cast<std::uint16_t>(
                                                baseRule + remaining - 1u)},
                                     LlNodeBuild{.cfg = e.cfg});
        arms.push_back(group(LlShape::seq, {item, suffix}, aux));
      }
    }
    // The end comes last, as an option's absence does: an item still read is
    // the preferred reading.
    if (ends(mask))
      arms.push_back(put_node(o, LlNode{.kind = LlShape::empty},
                              LlNodeBuild{.cfg = e.cfg}));
    o.cfg.rules[aux].body =
        arms.size() == 1 ? arms.front() : group(LlShape::alt, arms, aux);
  }
  return put_node(
      o,
      LlNode{.kind = LlShape::call,
             .sym = static_cast<std::uint16_t>(baseRule + subsets - 2u),
             .row = o.writes.intern_cat(made, row)},
      LlNodeBuild{.cfg = e.cfg, .section = e.section, .foldStep = e.foldStep});
}

std::uint16_t emit(LlLowered& o, const Pool& p, std::uint16_t at,
                   const Acts& made, const Acts& row, std::uint16_t owner) {
  const Node& e = p.e[at];
  const auto place = [&](LlShape kind, std::uint16_t sym,
                         std::uint32_t min = 0) {
    return put_node(o,
                    LlNode{.kind = kind,
                           .sym = sym,
                           .min = min,
                           .row = o.writes.intern_cat(made, row)},
                    LlNodeBuild{e.cfg, e.section});
  };
  switch (e.kind) {
    case Shape::sym:
      return is_term(e.sym) ? place(LlShape::term, e.sym)
                            : place(LlShape::call, nt_index(e.sym));
    case Shape::none:
      return put_node(o, LlNode{.kind = LlShape::empty,
                                .row = o.writes.intern_cat(made, row)});
    case Shape::condition:
      return place(LlShape::condition, e.sym);
    case Shape::implied:
      return place(LlShape::implied, e.sym, e.min);
    case Shape::end:
      return place(LlShape::endMark, e.sym);
    case Shape::seq:
      return emit_seq(o, p, e, made, row, owner);
    case Shape::alt:
      return emit_alt(o, p, at, made, row, owner);
    case Shape::unordered:
      return emit_unordered(o, p, e, made, row, owner);
    case Shape::rep: {
      refuse_item(o, p, e, owner);
      const auto body = [&] {
        const auto copy = emit_item(o, p, e.sym, owner);
        if (e.max > 1) o.cfg.nodeBuild[copy].repeatOwner = owner;
        return copy;
      };
      if ((e.max != kEndless && (e.max > 1 || e.min == e.max)) ||
          e.min > 1) {
        // Each counted occurrence is a different call site. Expanding only
        // the ATN would overwrite the one nodeFollow/nodeAfter used by the
        // emitted body and caret; lower both execution and prediction alike.
        // Nested options keep a single ordered prefix of the body.
        const std::uint64_t turns = e.max == kEndless ? e.min : e.max;
        const std::uint64_t optional = e.max == kEndless ? 0 : e.max - e.min;
        const std::uint64_t spelt = turns + (optional == 0 ? 1 : 2 * optional);
        if (o.cfg.nnodes + spelt > kNoRow)
          return refuse_node(o, LowerFail::tooManyPositions, owner);
        const auto sequence = [&](const std::vector<std::uint16_t>& ids,
                                  std::uint16_t chain, bool outer = false) {
          return put_group(o, ids.empty() ? LlShape::empty : LlShape::seq, ids,
                           chain,
                           LlNodeBuild{.cfg = e.cfg,
                                       .section = e.section,
                                       .foldStep = outer && e.foldStep});
        };
        const auto repetition = [&](std::uint16_t kid, std::uint32_t maximum) {
          const auto id = put_node(
              o,
              LlNode{
                  .kind = LlShape::rep, .sym = kid, .min = 0, .max = maximum},
              LlNodeBuild{.cfg = e.cfg, .section = e.section});
          return with_decision(o, id, 2, owner);
        };
        std::vector<std::uint16_t> ids;
        ids.reserve(e.min + (e.min != e.max));
        for (std::uint32_t count = 0; count < e.min; ++count)
          ids.push_back(body());
        if (e.max == kEndless) {
          ids.push_back(repetition(body(), kEndless));
        } else if (e.max > e.min) {
          // D? nested as (D (D (... D?)?)?)? keeps the greedy arm first.
          std::uint16_t tail = kNoRow;
          for (std::uint32_t count = e.min; count < e.max; ++count) {
            auto kid = body();
            if (tail != kNoRow) {
              kid = sequence({kid, tail}, kNoRow);
              o.cfg.nodeBuild[kid].ladder = true;
            }
            tail = repetition(kid, 1);
            o.cfg.nodeBuild[tail].ladder = true;
          }
          ids.push_back(tail);
        }
        return sequence(ids, o.writes.intern_cat(made, row), true);
      }
      const std::uint16_t kid = body();
      const std::uint16_t id = put_node(
          o,
          LlNode{.kind = LlShape::rep,
                 .sym = kid,
                 .min = e.min,
                 .max = e.max,
                 .row = o.writes.intern_cat(made, row)},
          LlNodeBuild{
              .cfg = e.cfg, .section = e.section, .leftFold = e.leftFold});
      // The turn is the choice: one arm enters the body, the other leaves.
      return with_decision(o, id, 2, owner);
    }
  }
  return put_node(o, LlNode{.kind = LlShape::empty});
}

// The chain of a group that stands for one position goes to that position: a
// record over the group and a record over what it holds cover the same span,
// and two of them where the shape holds one is a node nobody wrote. A child's
// creation stays separate until record fusion keeps the caller's writes apart
// from the writes filling that child.
void ride_chains_down(LlLowered& o, const LlRecordPlan& plan) {
  for (std::uint32_t i = 0; i < o.cfg.nnodes; ++i) {
    const auto id = static_cast<std::uint16_t>(i);
    LlNode& n = o.cfg.nodes[id];
    if (n.row == kNoRow || o.cfg.nodeBuild[id].foldStep) continue;
    const std::uint16_t only = plan.single_position(id);
    if (only == kNoRow || !plan.hands(only)) continue;
    LlNode& kid = o.cfg.nodes[only];
    // An outer write belongs to the caller even when the child is the same
    // AST class. Fusion below keeps the two action runs' provenance; plain
    // concatenation here would mistake that write for a child member fill.
    if (plan.opens_chain(kid.row) ||
        (kid.kind == LlShape::call && plan.rule_opens(kid.sym)))
      continue;
    if (kid.kind == LlShape::term && n.row < o.nestChains.size() &&
        o.nestChains[n.row] != 0)
      continue;
    kid.row = o.writes.concat(n.row, kid.row);
    n.row = kNoRow;
  }
}

// ...and what is left: a position whose chain no record of its own carries
// opens one. A group always does -- nothing else spans it -- and a call does
// where the rule it names lays none, or where it turns: riding the callee's
// record, a nest could not reach back over its receiver.
void lay_own_records(LlLowered& o, const LlRecordPlan& plan) {
  for (std::uint32_t id = 0; id < o.cfg.nnodes; ++id) {
    LlNode& n = o.cfg.nodes[id];
    if (n.row == kNoRow || n.kind == LlShape::term ||
        n.kind == LlShape::implied)
      continue;
    n.owns = n.kind != LlShape::call || !plan.rule_hands(n.sym) ||
             (n.row < o.nestChains.size() && o.nestChains[n.row] != 0);
  }
}

void emit_operator_nodes(LlLowered& o, const Pool& p, std::uint16_t at,
                         std::uint16_t row, std::uint8_t prec,
                         Association group, std::uint8_t cfg,
                         std::vector<std::uint16_t> before,
                         std::vector<std::uint16_t> after) {
  const Node& n = p.e[at];
  if (n.kind == Shape::sym && is_term(n.sym)) {
    const auto token =
        put_node(o, LlNode{.kind = LlShape::term, .sym = n.sym, .row = row},
                 LlNodeBuild{cfg});
    const auto guard = [&](const std::vector<std::uint16_t>& conditions) {
      if (conditions.empty()) return kNoRow;
      const auto condition = conditions.size() == 1
                                 ? conditions.front()
                                 : o.cfg.gap_conjoin(conditions);
      return condition == kNoRow
                 ? kNoRow
                 : put_node(
                       o, LlNode{.kind = LlShape::condition, .sym = condition},
                       LlNodeBuild{cfg});
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
      body = put_group(o, LlShape::seq, std::span(run, count), kNoRow,
                       LlNodeBuild{cfg});
    }
    o.cfg.climbOps[o.cfg.nclimbOps++] =
        LlClimbOp{.body = body, .prec = prec, .assoc = group};
    return;
  }
  if (n.kind == Shape::alt) {
    for (unsigned i = 0; i < n.count; ++i)
      emit_operator_nodes(o, p, p.kid[n.sym + i], row, prec, group, cfg, before,
                          after);
    return;
  }
  if (n.kind == Shape::seq) {
    std::uint16_t token = kNoRow;
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = p.kid[n.sym + i];
      if (p.e[k].kind == Shape::condition)
        (token == kNoRow ? before : after).push_back(p.e[k].sym);
      else if (p.e[k].kind != Shape::none)
        token = k;
    }
    if (token != kNoRow) {
      emit_operator_nodes(o, p, token, row, prec, group, cfg, std::move(before),
                          std::move(after));
      return;
    }
  }
  // Losing an operator silently changes the language and precedence.
  o.fault.raise(GrammarFaultCode::infixOperatorNotText, p.owner);
}

}  // namespace cursiv::detail::llow
