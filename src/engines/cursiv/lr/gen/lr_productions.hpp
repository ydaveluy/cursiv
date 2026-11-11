// A run of positions becomes productions: an auxiliary where a repetition or
// a choice cannot stand in one body, the actions above a position on its
// slot.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cursiv/lr/gen/LrLowerPool.hpp"
#include "cursiv/lr/gen/LrLowered.hpp"
#include "cursiv/lr/gen/lr_intern_name.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/GrammarFault.hpp"
#include "cursiv/shared/table/LexNode.hpp"

namespace cursiv::engine {

// A nonterminal under a name of its own, or one that takes the row of `like`:
// a copy of it, or one more link of its chain.
template <class Cfg>
std::uint16_t lr_new_aux(Cfg& c, std::uint16_t like = kNoRow) {
  if (c.nnts >= kNtBase) throw LowerRefused(LowerFail::tooManyRules);
  const std::uint16_t i = c.nnts++;
  if (like != kNoRow) {
    // By value: the write may grow the array the row is read from.
    const auto row = c.nts[like];
    c.nts[i] = row;
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
                                  const Acts& node = {}, bool endOnly = false,
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
  q.lhs = lhs;
  q.owner = p.owner;
  q.sectionCfg = p.sectionCfg;
  q.priorityAlt = priorityAlt == kNoRow ? lr_priority_alt(c, lhs) : priorityAlt;
  q.prioritySubset = prioritySubset;
  q.node = o.writes.intern_chain(node.data(), node.size());
  q.turn = p.turning && p.turns(node);
  q.endOnly = endOnly;
  q.prec = prec;
  const auto len = static_cast<std::uint16_t>(slots.size());
  q.len = len;
  q.at = c.nslots;
  LrLowSlot* const put = c.slots.row(c.nslots, len);
  // A position where a region opens stands on the caller's boundary.
  for (unsigned i = 0; i < len; ++i)
    put[i] = LrLowSlot{
        .row = o.writes.intern_cat(slots[i].made, slots[i].row, slots[i].doc),
        .sym = slots[i].sym,
        .cfg = static_cast<std::uint8_t>(
            slots[i].leading ? slots[i].leading - 1u : slots[i].cfg)};
  c.nslots += len;
  // The boundary after the last one where a region closes stands there too.
  q.tailCfg = slots.empty()           ? 0u
              : slots.back().trailing ? slots.back().trailing - 1u
                                      : slots.back().cfg;
  q.regionExit =
      !endOnly && !slots.empty() && slots.back().trailing &&
      slots.back().cfg != q.tailCfg &&
      c.section_cfg_at(slots.back().cfg) == c.section_cfg_at(q.tailCfg);
  return self;
}
// The options production `k` decided, `position << 16 | arm` each.
inline void lr_decide(LrBuildCfg& c, std::uint16_t k,
                      std::span<const std::uint32_t> arms) {
  if (k == kNoRow || arms.empty()) return;
  c.prods[k].decideAt = static_cast<std::uint32_t>(c.decisions.size());
  c.prods[k].decideLen = static_cast<std::uint32_t>(arms.size());
  c.decisions.v.insert(c.decisions.v.end(), arms.begin(), arms.end());
}

// A production of no position but conditions, pushed where a region closes:
// it stands past the region, on the boundary `trailing` names.
inline void lr_close_region(LrBuildCfg& c, std::uint16_t k,
                            std::uint8_t trailing) {
  if (trailing == 0 || k == kNoRow) return;
  LrLowProd& q = c.prods[k];
  q.closesRegion = true;
  q.tailCfg = static_cast<std::uint8_t>(trailing - 1u);
  q.regionExit = !q.endOnly && c.section_cfg_at(q.sectionCfg) ==
                                   c.section_cfg_at(trailing - 1u);
}

inline Descent<void> lr_emit_alts(LrLowered& o, LrLowerPool& p,
                                  std::uint16_t lhs, LrShapeId at,
                                  const Acts& made, const Acts& row);
inline Descent<void> lr_emit_unordered(LrLowered& o, LrLowerPool& p,
                                       std::uint16_t lhs, LrShapeId at,
                                       const Acts& made, const Acts& row);
inline void lr_emit_subsets(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                            std::vector<LrLowerSlot> slots, const Acts& node,
                            std::uint16_t priority);

// `made` and `row` are handed in by the caller, which may have taken them
// for itself: read off the node, one directive would apply twice.
inline Descent<LrLowerSlot> lr_slot_for(LrLowered& o, LrLowerPool& p,
                                        LrShapeId at, const Acts& made,
                                        const Acts& row);

// The slot of auxiliary `aux` where `e` stands, a node or a slot.
template <class At>
LrLowerSlot lr_aux_slot(std::uint16_t aux, const At& e) {
  return {.sym = nt_sym(aux),
          .cfg = e.cfg,
          .leading = e.leading,
          .trailing = e.trailing};
}

inline Descent<LrLowerSlot> lr_slot_for_group(LrLowered& o, LrLowerPool& p,
                                              LrShapeId at, const Acts& made,
                                              const Acts& row) {
  LrBuildCfg& c = o.cfg;
  const LrLowerNode& e = p.e[at];
  const std::uint16_t aux = lr_new_aux(c);
  if (e.kind == LrShape::unordered)
    co_await lr_emit_unordered(o, p, aux, at, made, row);
  else
    co_await lr_emit_alts(o, p, aux, at, made, row);
  co_return lr_aux_slot(aux, e);
}

// A choice becomes an auxiliary, one production per branch; a branch that is
// the end assertion alone makes it optional instead. A lone condition branch
// where the choice closes a region is refused.
inline Descent<LrLowerSlot> lr_slot_for_alt(LrLowered& o, LrLowerPool& p,
                                            LrShapeId at, const Acts& made,
                                            const Acts& row) {
  const LrLowerNode& e = p.e[at];
  unsigned nEnd = 0;
  for (unsigned i = 0; e.trailing != 0 && i < e.count; ++i) {
    const LrLowerNode& arm = p.e[p.child[e.sym + i]];
    const auto sym = static_cast<std::uint16_t>(arm.sym);
    if (arm.kind == LrShape::sym && p.is_gap(sym))
      lower_refuse(o.cfg, LowerFail::regionBoundaryShared);
  }
  bool decoratedEnd = !made.empty() || !row.empty();
  for (unsigned i = 0; i < e.count; ++i) {
    const auto& child = p.e[p.child[e.sym + i]];
    if (child.kind == LrShape::end) {
      ++nEnd;
      decoratedEnd = decoratedEnd || !child.made.empty() || !child.row.empty();
    }
  }
  if (nEnd > 1) o.fault.raise(GrammarFaultCode::twoEndMarks, p.owner);
  if (nEnd != 0 && decoratedEnd)
    co_return co_await lr_slot_for_group(o, p, at, made, row);
  if (nEnd == e.count) co_return LrLowerSlot{.end = true, .cfg = e.cfg};
  if (nEnd == 0) co_return co_await lr_slot_for_group(o, p, at, made, row);
  const std::uint16_t aux = lr_new_aux(o.cfg);
  for (unsigned i = 0; i < e.count; ++i) {
    const LrShapeId k = p.child[e.sym + i];
    if (p.e[k].kind != LrShape::end)
      co_await lr_emit_alts(o, p, aux, k, cat(p.e[k].made, made),
                            cat(p.e[k].row, row));
  }
  LrLowerSlot slot = lr_aux_slot(aux, e);
  slot.optional = true;
  slot.endAlt = true;
  co_return slot;
}

// `n` turns of one item: only the first stands at the head of the run, and
// only the last at its tail unless something follows.
inline std::vector<LrLowerSlot> lr_turns(const LrLowerSlot& in, std::uint32_t n,
                                         bool last = true) {
  std::vector items(n, in);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (i != 0) items[i].leading = 0;
    if (i + 1 != n || !last) items[i].trailing = 0;
  }
  return items;
}

// `R -> R X | X`, the actions on the item.
inline void lr_emit_left_recursion(LrLowered& o, const LrLowerPool& p,
                                   std::uint16_t aux, const LrLowerSlot& in,
                                   std::uint32_t min) {
  // Only the base case stands at the head of the run, and only the item at
  // its tail.
  LrLowerSlot inner = in;
  inner.leading = 0;
  const LrLowerSlot pair[2] = {
      {.sym = nt_sym(aux), .cfg = in.cfg, .leading = in.leading}, inner};
  lr_push_prod(o, p, aux, pair);
  lr_push_prod(o, p, aux, lr_turns(in, std::max(min, 1u)));
}

// Each optional turn owns its empty alternative, the turn written first: left
// recursion would put the decision to stop on the caller's reduce.
inline void lr_emit_chained_repeat(LrLowered& o, const LrLowerPool& p,
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
    item.leading = 0;
    if (hasTail) item.trailing = 0;
    const LrLowerSlot slots[2] = {
        item, {.sym = nt_sym(tail), .cfg = in.cfg, .trailing = in.trailing}};
    lr_push_prod(o, p, next, std::span(slots).first(hasTail ? 2 : 1));
    lr_close_region(o.cfg, lr_push_prod(o, p, next, {}), in.trailing);
    if (o.cfg.lowerFail()) return;
    tail = next;
  }
  std::vector<LrLowerSlot> slots = lr_turns(in, floor, tail == kNoRow);
  if (tail != kNoRow)
    slots.push_back(LrLowerSlot{
        .sym = nt_sym(tail), .cfg = in.cfg, .trailing = in.trailing});
  lr_push_prod(o, p, aux, slots);
}

inline Descent<LrLowerSlot> lr_slot_for_rep(LrLowered& o, LrLowerPool& p,
                                            LrShapeId at, const Acts& made,
                                            const Acts& row) {
  const LrLowerNode& e = p.e[at];
  const LrShapeId child = e.sym;
  LrLowerSlot in =
      co_await lr_slot_for(o, p, child, p.e[child].made, p.e[child].row);
  if (e.max == 1) {
    // An optional end assertion says nothing its absence does not.
    if (e.min == 0 && in.end)
      o.fault.raise(GrammarFaultCode::optionalEndMark, p.owner);
    // Nor does an optional condition.
    if (e.min == 0 && p.is_gap(in.sym))
      o.fault.raise(GrammarFaultCode::optionalCondition, p.owner);
    in.optional |= e.min == 0;
    if (e.leading) in.leading = e.leading;
    if (e.trailing) in.trailing = e.trailing;
    // An optional turn carries no end assertion.
    in.endAlt &= e.min != 0;
    in.made.insert(in.made.end(), made.begin(), made.end());
    in.row.insert(in.row.end(), row.begin(), row.end());
    co_return in;
  }
  // An item that may match nothing is refused: one holding no position names
  // no symbol to go on with either.
  if (in.optional || in.none || in.end)
    co_return LrLowerSlot{.sym = in.sym, .cfg = in.cfg};
  const std::uint16_t aux = lr_new_aux(o.cfg);
  if (e.max == kEndless && e.min != 0 && in.trailing != 0) {
    // An unbounded run closing a region has a required final item. Keep its
    // earlier items in an internal prefix: no state both shifts another
    // internal item and reduces the region under the caller's trivia.
    const std::uint16_t prefix = lr_new_aux(o.cfg);
    LrLowerSlot repeated = in;
    repeated.trailing = 0;
    lr_emit_left_recursion(o, p, prefix, repeated, e.min - 1u);
    LrLowerSlot last = in;
    last.leading = 0;
    std::vector<LrLowerSlot> slots{{.sym = nt_sym(prefix),
                                    .optional = e.min == 1,
                                    .cfg = in.cfg,
                                    .leading = in.leading},
                                   last};
    lr_emit_subsets(o, p, aux, std::move(slots), {}, kNoRow);
  } else if (e.max != kEndless)
    lr_emit_chained_repeat(o, p, aux, in, e);
  else
    lr_emit_left_recursion(o, p, aux, in, e.min);
  const auto run = [&](std::uint16_t nt) {
    LrLowerSlot slot = lr_aux_slot(nt, in);
    slot.optional = e.min == 0;
    return slot;
  };
  // A directive over the list names one record over the whole run: a head
  // holds it.
  if (made.empty() && row.empty()) co_return run(aux);
  const std::uint16_t head = lr_new_aux(o.cfg);
  // The head stands where the run stands, edges included, or it and the
  // caller would answer one point two ways.
  const LrLowerSlot only = lr_aux_slot(aux, in);
  lr_push_prod(o, p, head, std::span(&only, 1), cat(made, row));
  co_return run(head);
}

inline Descent<LrLowerSlot> lr_slot_for(LrLowered& o, LrLowerPool& p,
                                        LrShapeId at, const Acts& made,
                                        const Acts& row) {
  switch (const LrLowerNode& e = p.e[at]; e.kind) {
    case LrShape::sym:
      co_return LrLowerSlot{.sym = static_cast<std::uint16_t>(e.sym),
                            .cfg = e.cfg,
                            .leading = e.leading,
                            .trailing = e.trailing,
                            .made = made,
                            .row = row,
                            .doc = e.doc};
    // An item that takes no position has no record for a directive to attach
    // to.
    case LrShape::none:
      if (!made.empty() || !row.empty())
        o.fault.raise(GrammarFaultCode::directiveOnNoPosition, p.owner);
      co_return LrLowerSlot{
          .none = true, .leading = e.leading, .trailing = e.trailing};
    case LrShape::end:
      if (!made.empty() || !row.empty())
        o.fault.raise(GrammarFaultCode::directiveOnNoPosition, p.owner);
      co_return LrLowerSlot{.end = true,
                            .cfg = e.cfg,
                            .leading = e.leading,
                            .trailing = e.trailing};
    case LrShape::alt:
      co_return co_await lr_slot_for_alt(o, p, at, made, row);
    case LrShape::unordered:
    case LrShape::seq:
      co_return co_await lr_slot_for_group(o, p, at, made, row);
    case LrShape::scope: {
      // Like a named rule returning its node: internal actions belong to the
      // callee, external directives to the call site.
      const std::uint16_t aux = lr_new_aux(o.cfg);
      const LrLowerNode& child = p.e[e.sym];
      co_await lr_emit_alts(o, p, aux, e.sym, child.made, child.row);
      LrLowerSlot slot = lr_aux_slot(aux, e);
      slot.made = made;
      slot.row = row;
      co_return slot;
    }
    case LrShape::rep:
      co_return co_await lr_slot_for_rep(o, p, at, made, row);
  }
  co_return LrLowerSlot{};
}

}  // namespace cursiv::engine
