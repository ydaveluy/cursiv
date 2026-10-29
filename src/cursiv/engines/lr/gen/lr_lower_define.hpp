// A run of positions becomes productions: an auxiliary where a repetition or
// a choice cannot stand in one body, the actions above a position on its
// slot.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowerPool.hpp"
#include "cursiv/engines/lr/gen/LrLowered.hpp"
#include "cursiv/engines/lr/gen/lr_intern_name.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

// A chain of auxiliaries passes `like` to spell its name once.
template <class Cfg>
std::uint16_t lr_new_aux(Cfg& c, std::uint16_t like = kNoRow) {
  if (c.nnts >= kNtBase) {
    lower_refuse(c, LowerFail::tooManyRules);
    throw LowerRefused(LowerFail::tooManyRules);
  }
  const std::uint16_t i = c.nnts++;
  if (like != kNoRow) {
    c.nts[i].nameOff = c.nts[like].nameOff;
    c.nts[i].nameLen = c.nts[like].nameLen;
    ++c.auxCount;
    return i;
  }
  const std::string name = "_" + std::to_string(c.auxCount++);
  c.nts[i].nameOff = lr_intern_name(c, name);
  c.nts[i].nameLen = static_cast<std::uint16_t>(name.size());
  return i;
}

inline std::uint16_t lr_priority_alt(LrBuildCfg& c, std::uint16_t lhs) {
  const auto next = c.priorityNext[lhs]++;
  if (next >= kNoRow) throw LowerRefused(LowerFail::tooManyProductions);
  return static_cast<std::uint16_t>(next);
}

// The production pushed, or `kNoRow` where the pool refused it.
inline std::uint16_t lr_push_prod(LrLowered& o, const LrLowerPool& p,
                                  std::uint16_t lhs,
                                  std::span<const LrLowerSlot> slots,
                                  const LrActs& node = {}, bool endOnly = false,
                                  std::uint8_t prec = 0,
                                  std::uint16_t priorityAlt = kNoRow,
                                  std::uint16_t prioritySubset = 0) {
  LrBuildCfg& c = o.cfg;
  if (c.nprods == 0xffffu) {
    lower_refuse(c, LowerFail::tooManyProductions);
    return kNoRow;
  }
  // A truncated length would leave the run in the pool under another
  // production, so check it before narrowing to sixteen bits.
  if (slots.size() > 0xffffu) {
    lower_refuse(c, LowerFail::productionTooWide);
    return kNoRow;
  }
  const std::uint16_t self = c.nprods;
  LrLowProd& q = c.prods[c.nprods++];
  LrProdBuild& b = c.prodBuild[self];
  q.lhs = lhs;
  b.owner = p.owner;
  b.home = p.home;
  b.priorityAlt = priorityAlt == kNoRow ? lr_priority_alt(c, lhs) : priorityAlt;
  b.prioritySubset = prioritySubset;
  q.node = o.writes.intern_chain(node.data(), node.size());
  b.turn = p.turning && p.turns(node);
  b.endOnly = endOnly;
  b.prec = prec;
  const auto len = static_cast<std::uint16_t>(slots.size());
  q.len = len;
  q.at = c.nslots;
  LrLowSlot* const put = c.slots.row(c.nslots, len);
  std::uint16_t* const sym = c.slotSym.row(c.nslots, len);
  std::uint8_t* const tgt = c.slotCfg.row(c.nslots, len);
  for (unsigned i = 0; i < len; ++i) {
    put[i] = LrLowSlot{o.writes.intern_cat(slots[i].made, slots[i].row)};
    sym[i] = slots[i].sym;
    // A position where a region opens stands on the caller's boundary.
    tgt[i] = slots[i].lead ? slots[i].lead - 1u : slots[i].cfg;
  }
  c.nslots += len;
  // ...and so does the boundary after the last one where a region closes.
  b.tailCfg = slots.empty()        ? 0u
              : slots.back().trail ? slots.back().trail - 1u
                                   : slots.back().cfg;
  return self;
}
// The options production `k` decided, `position << 16 | arm` each.
inline void lr_decide(LrBuildCfg& c, std::uint16_t k,
                      std::span<const std::uint32_t> arms) {
  if (k == kNoRow || arms.empty()) return;
  c.prodBuild[k].decideAt = static_cast<std::uint32_t>(c.decisions.size());
  c.prodBuild[k].decideLen = static_cast<std::uint32_t>(arms.size());
  c.decisions.v.insert(c.decisions.v.end(), arms.begin(), arms.end());
}

// A production of no position but conditions, pushed where a region closes:
// it stands past the region, on the boundary `trail` names.
inline void lr_close_region(LrBuildCfg& c, std::uint16_t k,
                            std::uint8_t trail) {
  if (trail == 0 || k == kNoRow) return;
  c.prodBuild[k].closesRegion = true;
  c.prodBuild[k].tailCfg = static_cast<std::uint8_t>(trail - 1u);
}

inline void lr_emit_alts(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                         LrShapeId at, const LrActs& made, const LrActs& row);
inline void lr_emit_unordered(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                              LrShapeId at, const LrActs& made,
                              const LrActs& row);

// `made` and `row` are handed in by the caller, which may have taken them
// for itself: read off the node, one directive would apply twice.
inline LrLowerSlot lr_slot_for(LrLowered& o, LrLowerPool& p, LrShapeId at,
                               const LrActs& made, const LrActs& row);

inline LrLowerSlot lr_slot_for_group(LrLowered& o, LrLowerPool& p, LrShapeId at,
                                    const LrActs& made, const LrActs& row) {
  LrBuildCfg& c = o.cfg;
  const LrLowerNode& e = p.e[at];
  const std::uint16_t aux = lr_new_aux(c);
  if (e.kind == LrShape::unordered)
    lr_emit_unordered(o, p, aux, at, made, row);
  else
    lr_emit_alts(o, p, aux, at, made, row);
  return LrLowerSlot{
      .sym = nt_sym(aux), .cfg = e.cfg, .lead = e.lead, .trail = e.trail};
}

// A choice becomes an auxiliary, one production per branch; a branch that is
// the end assertion alone makes the choice optional instead. A branch that is
// a condition alone, where the choice closes a region, is refused: the point
// after it would be read inside the region and outside it.
inline LrLowerSlot lr_slot_for_alt(LrLowered& o, LrLowerPool& p, LrShapeId at,
                                   const LrActs& made, const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  unsigned nEnd = 0;
  for (unsigned i = 0; e.trail != 0 && i < e.count; ++i) {
    const LrLowerNode& arm = p.e[p.kid[e.sym + i]];
    const auto sym = static_cast<std::uint16_t>(arm.sym);
    if (arm.kind == LrShape::sym && p.is_gap(sym))
      lower_refuse(o.cfg, LowerFail::regionBoundaryShared);
  }
  bool decoratedEnd = !made.empty() || !row.empty();
  for (unsigned i = 0; i < e.count; ++i) {
    const auto& child = p.e[p.kid[e.sym + i]];
    if (child.kind == LrShape::end) {
      ++nEnd;
      decoratedEnd = decoratedEnd || !child.made.empty() || !child.row.empty();
    }
  }
  if (nEnd > 1) o.fault.raise(GrammarFaultCode::twoEndMarks, p.owner);
  if (nEnd != 0 && decoratedEnd) return lr_slot_for_group(o, p, at, made, row);
  if (nEnd == e.count) return LrLowerSlot{.end = true, .cfg = e.cfg};
  if (nEnd == 0) return lr_slot_for_group(o, p, at, made, row);
  const std::uint16_t aux = lr_new_aux(o.cfg);
  for (unsigned i = 0; i < e.count; ++i) {
    const LrShapeId k = p.kid[e.sym + i];
    if (p.e[k].kind != LrShape::end)
      lr_emit_alts(o, p, aux, k, lr_cat(p.e[k].made, made),
                   lr_cat(p.e[k].row, row));
  }
  return LrLowerSlot{.sym = nt_sym(aux),
                     .optional = true,
                     .cfg = e.cfg,
                     .lead = e.lead,
                     .trail = e.trail,
                     .endAlt = true};
}

// `n` turns of one item: only the first stands at the head of the run, and
// only the last at its tail unless something follows.
inline std::vector<LrLowerSlot> lr_turns(const LrLowerSlot& in, std::uint32_t n,
                                         bool last = true) {
  std::vector<LrLowerSlot> items(n, in);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (i != 0) items[i].lead = 0;
    if (i + 1 != n || !last) items[i].trail = 0;
  }
  return items;
}

// `R -> R X | X`, the actions riding the item.
inline void lr_emit_left_recursion(LrLowered& o, LrLowerPool& p,
                                   std::uint16_t aux, const LrLowerSlot& in,
                                   std::uint32_t min) {
  // Only the base case stands at the head of the run, and only the item at
  // its tail.
  LrLowerSlot self;
  self.sym = nt_sym(aux);
  self.cfg = in.cfg;
  self.lead = in.lead;
  self.trail = 0;
  LrLowerSlot inner = in;
  inner.lead = 0;
  const LrLowerSlot pair[2] = {self, inner};
  lr_push_prod(o, p, aux, pair);
  const std::uint32_t n = min > 1 ? min : 1;
  lr_push_prod(o, p, aux, lr_turns(in, n));
}

// Each optional turn owns its empty alternative, the turn written first: left
// recursion would put the decision to stop on the caller's reduce.
inline void lr_emit_chained_repeat(LrLowered& o, LrLowerPool& p,
                                   std::uint16_t aux, const LrLowerSlot& in,
                                   const LrLowerNode& e) {
  const std::uint32_t floor = std::max(e.min, 1u);
  const std::uint32_t extra = e.max - floor;
  if (extra >= unsigned{kNtBase} - o.cfg.nnts) {
    lower_refuse(o.cfg, LowerFail::tooManyRules);
    return;
  }
  std::uint16_t tail = kNoRow;
  for (std::uint32_t j = 0; j < extra; ++j) {
    const std::uint16_t next = lr_new_aux(o.cfg, aux);
    const bool hasTail = tail != kNoRow;
    LrLowerSlot item = in;
    item.lead = 0;
    if (hasTail) item.trail = 0;
    const LrLowerSlot slots[2] = {
        item, {.sym = nt_sym(tail), .cfg = in.cfg, .trail = in.trail}};
    lr_push_prod(o, p, next, std::span(slots).first(hasTail ? 2 : 1));
    lr_close_region(o.cfg, lr_push_prod(o, p, next, {}), in.trail);
    if (o.cfg.lowerFail()) return;
    tail = next;
  }
  std::vector<LrLowerSlot> slots = lr_turns(in, floor, tail == kNoRow);
  if (tail != kNoRow)
    slots.push_back(
        LrLowerSlot{.sym = nt_sym(tail), .cfg = in.cfg, .trail = in.trail});
  lr_push_prod(o, p, aux, slots);
}

inline LrLowerSlot lr_slot_for_rep(LrLowered& o, LrLowerPool& p, LrShapeId at,
                                   const LrActs& made, const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  const LrShapeId kid = e.sym;
  LrLowerSlot in = lr_slot_for(o, p, kid, p.e[kid].made, p.e[kid].row);
  if (e.max == 1) {
    // An optional end assertion says nothing its absence does not.
    if (e.min == 0 && in.end)
      o.fault.raise(GrammarFaultCode::optionalEndMark, p.owner);
    // ...and neither does an optional condition.
    if (e.min == 0 && p.is_gap(in.sym))
      o.fault.raise(GrammarFaultCode::optionalCondition, p.owner);
    in.optional |= e.min == 0;
    if (e.lead) in.lead = e.lead;
    if (e.trail) in.trail = e.trail;
    // An optional turn carries no end assertion.
    in.endAlt &= e.min != 0;
    in.made.insert(in.made.end(), made.begin(), made.end());
    in.row.insert(in.row.end(), row.begin(), row.end());
    return in;
  }
  // An item that may match nothing reads one text many ways under a
  // repetition; one holding no position names no symbol either.
  if (in.optional || in.none || in.end) {
    o.fault.raise(GrammarFaultCode::nullableRepetition, p.owner);
    return LrLowerSlot{.sym = in.sym, .cfg = in.cfg};
  }
  const std::uint16_t aux = lr_new_aux(o.cfg);
  if (e.max != kEndless)
    lr_emit_chained_repeat(o, p, aux, in, e);
  else
    lr_emit_left_recursion(o, p, aux, in, e.min);
  const auto run = [&](std::uint16_t nt) {
    return LrLowerSlot{.sym = nt_sym(nt),
                       .optional = e.min == 0,
                       .cfg = in.cfg,
                       .lead = in.lead,
                       .trail = in.trail};
  };
  // A directive over the list names one record over the whole run: a head
  // holds it.
  if (made.empty() && row.empty()) return run(aux);
  const std::uint16_t head = lr_new_aux(o.cfg);
  const std::uint16_t one = nt_sym(aux);
  // The head stands where the run stands, edges included, or it and the
  // caller would answer one point two ways.
  const LrLowerSlot only{
      .sym = one, .cfg = in.cfg, .lead = in.lead, .trail = in.trail};
  lr_push_prod(o, p, head, std::span(&only, 1), lr_cat(made, row));
  return run(head);
}

inline LrLowerSlot lr_slot_for(LrLowered& o, LrLowerPool& p, LrShapeId at,
                               const LrActs& made, const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  switch (e.kind) {
    case LrShape::sym:
      // A turn over one symbol is a production of its own: riding that
      // symbol's record, it could not reach back over its receiver.
      if (p.turning && p.turns(made))
        return lr_slot_for_group(o, p, at, made, row);
      return LrLowerSlot{.sym = static_cast<std::uint16_t>(e.sym),
                         .cfg = e.cfg,
                         .lead = e.lead,
                         .trail = e.trail,
                         .made = made,
                         .row = row};
    // An item that takes no position has no record for a directive to ride.
    case LrShape::none:
      if (!made.empty() || !row.empty())
        o.fault.raise(GrammarFaultCode::directiveOnNoPosition, p.owner);
      return LrLowerSlot{.none = true, .lead = e.lead, .trail = e.trail};
    case LrShape::end:
      if (!made.empty() || !row.empty())
        o.fault.raise(GrammarFaultCode::directiveOnNoPosition, p.owner);
      return LrLowerSlot{
          .end = true, .cfg = e.cfg, .lead = e.lead, .trail = e.trail};
    case LrShape::alt:
      return lr_slot_for_alt(o, p, at, made, row);
    case LrShape::unordered:
    case LrShape::seq:
      return lr_slot_for_group(o, p, at, made, row);
    case LrShape::scope: {
      // Like a named rule returning its node: internal actions belong to the
      // callee, external directives to the call site.
      const std::uint16_t aux = lr_new_aux(o.cfg);
      const LrLowerNode& child = p.e[e.sym];
      lr_emit_alts(o, p, aux, e.sym, child.made, child.row);
      return LrLowerSlot{.sym = nt_sym(aux),
                         .cfg = e.cfg,
                         .lead = e.lead,
                         .trail = e.trail,
                         .made = made,
                         .row = row};
    }
    case LrShape::rep:
      return lr_slot_for_rep(o, p, at, made, row);
  }
  return LrLowerSlot{};
}

}  // namespace cursiv
