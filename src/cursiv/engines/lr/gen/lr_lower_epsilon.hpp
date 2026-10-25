// Optional positions become subsets up to the gate; surplus positions use
// a nullable auxiliary.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrLowerPool.hpp"
#include "cursiv/engines/lr/gen/lr_lower_define.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

// A position a turn's receiver may take in: a symbol, and no end assertion or
// region edge, which mean something else inside the receiver.
inline bool lr_holds_position(const LrLowerSlot& s) {
  return !s.end && !s.endAlt && !s.none && !s.lead && !s.trail;
}

// A token required after a choice that may end the text: the arm that ends it
// is never read. This is the position after the last required terminal, zero
// for none; a token follows slot `at` when it lies past `at + 1`.
inline std::size_t lr_required_end(const std::vector<LrLowerSlot>& slots) {
  std::size_t end = slots.size();
  while (end != 0) {
    const LrLowerSlot& s = slots[end - 1];
    if (!s.optional && !s.none && !s.end && is_term(s.sym)) break;
    --end;
  }
  return end;
}

// A position the subset walk decides: one holding no symbol reads the same
// either way.
inline bool lr_subset_bit(const LrLowerSlot& s) {
  return s.optional && !s.none;
}

// An optional position as a rule of its own, `O -> X | <nothing>`: its
// actions belong to X alone, never to O's empty production.
inline std::uint16_t lr_optional_aux(LrLowered& o, LrLowerPool& p,
                                     LrLowerSlot& s) {
  const std::uint16_t aux = lr_new_aux(o.cfg);
  LrLowerSlot inner = s;
  inner.optional = false;
  inner.endAlt = false;
  s.made.clear();
  s.row.clear();
  lr_push_prod(o, p, aux, std::span(&inner, 1));
  const auto absent = lr_push_prod(o, p, aux, {}, {}, s.endAlt);
  lr_close_region(o.cfg, absent, s.trail);
  if (s.endAlt && absent != kNoRow) o.cfg.prodBuild[absent].tailCfg = s.cfg;
  s.sym = nt_sym(aux);
  s.optional = false;
  s.endAlt = false;
  return aux;
}

// Two optional positions on one symbol with nothing needed between them, that
// write different things: each taken alone is a production spelled like the
// other's, however the gate spells them.
inline bool lr_optional_twins(const std::vector<LrLowerSlot>& slots) {
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const LrLowerSlot& a = slots[i];
    if (!lr_subset_bit(a) || a.lead != 0 || a.trail != 0) continue;
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      const LrLowerSlot& b = slots[j];
      if (b.none) continue;
      if (!lr_subset_bit(b)) break;
      if (b.sym == a.sym && b.cfg == a.cfg && b.lead == 0 && b.trail == 0 &&
          (b.made != a.made || b.row != a.row))
        return true;
    }
  }
  return false;
}

// One production per subset of an alternative's optional positions. The empty
// subset is written too: a rule keeps an empty right-hand side where every
// position may be left out.
inline void lr_emit_subsets(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                            std::vector<LrLowerSlot> slots, const LrActs& node,
                            std::uint16_t priority = kNoRow) {
  LrBuildCfg& c = o.cfg;
  if (priority == kNoRow) priority = lr_priority_alt(c, lhs);
  const unsigned n = static_cast<unsigned>(slots.size());
  unsigned nopt = 0;
  for (const LrLowerSlot& s : slots)
    if (lr_subset_bit(s)) ++nopt;
  if (lr_optional_twins(slots))
    o.fault.raise(GrammarFaultCode::twinProductions, p.owner);
  const std::size_t tokenEnd =
      nopt > o.optionalGate ? lr_required_end(slots) : 0;
  for (std::size_t i = 0; i < slots.size() && nopt > o.optionalGate; ++i) {
    LrLowerSlot& s = slots[i];
    if (!lr_subset_bit(s)) continue;
    if (s.endAlt && tokenEnd > i + 1) {
      s.optional = false;
      s.endAlt = false;
    } else {
      o.gated.push_back(lr_optional_aux(o, p, s));
    }
    --nopt;
  }

  std::vector<LrLowerSlot> keptSlots(n);
  std::vector<std::uint32_t> arms;
  std::vector<std::uint16_t> endAux;
  for (unsigned mask = 0; mask < (1u << nopt); ++mask) {
    if (c.lowerFail()) return;
    unsigned len = 0, bit = 0;
    bool endOnly = false, unreachable = false, asserted = false;
    std::uint8_t endCfg = 0;
    arms.clear();
    // A head left out hands its edge to the first position kept, a tail to
    // the last: the outermost edge, the first head and the last tail.
    std::uint8_t leadOwed = 0, trailOwed = 0;
    for (unsigned i = 0; i < n; ++i) {
      bool lr_keep = true;
      if (lr_subset_bit(slots[i])) {
        lr_keep = ((mask >> bit++) & 1u) != 0;
        arms.push_back(len << 16 | (lr_keep ? 0u : 1u));
      }
      if (!lr_keep || slots[i].none) {
        // Dropping an `endAlt` slot is the branch that asserted the end.
        if (!lr_keep && slots[i].endAlt) {
          if (!endOnly) endCfg = slots[i].cfg;
          endOnly = true;
        }
        if (!leadOwed) leadOwed = slots[i].lead;
        if (slots[i].trail) trailOwed = slots[i].trail;
        continue;
      }
      if (slots[i].end) {
        if (!endOnly) endCfg = slots[i].cfg;
        endOnly = true;
        continue;
      }
      // A nonterminal after EOF may still derive an empty subtree with
      // writes: the assertion stands at its entry.
      if (endOnly && is_term(slots[i].sym)) unreachable = true;
      if (endOnly && !asserted && !unreachable) {
        if (endAux.size() <= endCfg) endAux.resize(endCfg + 1u, kNoRow);
        auto& aux = endAux[endCfg];
        if (aux == kNoRow) {
          aux = lr_new_aux(c);
          const auto end = lr_push_prod(o, p, aux, {}, {}, true);
          if (end != kNoRow) c.prodBuild[end].tailCfg = endCfg;
        }
        keptSlots[len] = {.sym = nt_sym(aux), .cfg = endCfg};
        ++len;
        asserted = true;
      }
      keptSlots[len] = slots[i];
      if (leadOwed) keptSlots[len].lead = leadOwed;
      leadOwed = 0;
      trailOwed = 0;
      ++len;
    }
    if (unreachable) continue;
    if (trailOwed && len != 0) keptSlots[len - 1].trail = trailOwed;
    unsigned greedy = 0;
    for (unsigned k = 0; k < nopt; ++k)
      greedy = (greedy << 1) | ((mask >> k) & 1u);
    // A loop's exit alone is the empty production that stops it.
    const std::uint16_t stop =
        len == 1 && !endOnly && keptSlots[0].made.empty() &&
                keptSlots[0].row.empty() && keptSlots[0].cfg == 0 &&
                keptSlots[0].lead == 0 && keptSlots[0].trail == 0
            ? p.stop_of(keptSlots[0].sym)
            : std::uint16_t{0};
    const std::uint16_t k = lr_push_prod(
        o, p, lhs, std::span(keptSlots).first(stop != 0 ? 0u : len), node,
        endOnly, 0, priority, static_cast<std::uint16_t>(greedy));
    if (stop != 0 && k != kNoRow) c.prodBuild[k].stop = stop;
    lr_decide(c, k, arms);
    lr_close_region(c, k, len == 0 ? trailOwed : std::uint8_t{0});
    if (endOnly && !asserted && k != kNoRow) c.prodBuild[k].tailCfg = endCfg;
  }
}

// What a sequence read before a position that turns, as one rule: the turn
// finds it in the frame below its own.
inline LrLowerSlot lr_receiver(LrLowered& o, LrLowerPool& p,
                               std::vector<LrLowerSlot> prefix) {
  const std::uint16_t aux = lr_new_aux(o.cfg);
  const LrLowerSlot slot{.sym = nt_sym(aux), .cfg = prefix.front().cfg};
  const bool turning = p.turning;
  p.turning = false;
  lr_emit_subsets(o, p, aux, std::move(prefix), {});
  p.turning = turning;
  return slot;
}

inline std::vector<LrLowerSlot> lr_slots_for_sequence(
    LrLowered& o, LrLowerPool& p, std::span<const LrShapeId> kids, bool turn) {
  const bool turning = p.turning;
  std::vector<LrLowerSlot> slots;
  bool held = false;
  slots.reserve(kids.size());
  for (std::size_t i = 0; i < kids.size(); ++i) {
    const LrShapeId k = kids[i];
    bool reaches =
        i == 0 ? turning && !turn : !p.inTurn && !held && p.head_turns(k);
    for (const LrLowerSlot& s : slots)
      reaches = reaches && (i == 0 || lr_holds_position(s));
    if (reaches && i != 0) {
      LrLowerSlot receiver = lr_receiver(o, p, std::move(slots));
      slots.clear();
      slots.push_back(receiver);
    }
    p.turning = reaches;
    slots.push_back(lr_slot_for(o, p, k, p.e[k].made, p.e[k].row));
    held = held || p.turns(p.e[k].made);
  }
  return slots;
}

// The productions of `lhs` over `kids` in order, `made` and `row` over them.
// A turn's own positions reach back to nothing; its record stands over
// whatever it holds...
inline void lr_emit_sequence(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                             std::span<const LrShapeId> kids,
                             const LrLowerNode& e, const LrActs& made,
                             const LrActs& row) {
  const auto priority = lr_priority_alt(o.cfg, lhs);
  const bool turning = p.turning, inTurn = p.inTurn;
  const bool turn = p.turns(made);
  if (turn) p.inTurn = true;
  // ...and what it writes over its positions stands on them, not on that
  // record, which reaches back over its receiver.
  const bool apart = turning && turn && !row.empty();
  std::vector<LrLowerSlot> slots;
  if (apart) {
    p.turning = false;
    const std::uint16_t inner = lr_new_aux(o.cfg);
    lr_emit_sequence(o, p, inner, kids, e, {}, row);
    slots.push_back(LrLowerSlot{
        .sym = nt_sym(inner), .cfg = e.cfg, .lead = e.lead, .trail = e.trail});
  } else {
    slots = lr_slots_for_sequence(o, p, kids, turn);
  }
  p.turning = turning;
  p.inTurn = inTurn;
  lr_emit_subsets(o, p, lhs, std::move(slots), apart ? made : lr_cat(made, row),
                  priority);
}

inline void lr_emit_one(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                        LrShapeId at, const LrActs& made, const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  if (e.kind == LrShape::seq) {
    lr_emit_sequence(o, p, lhs, std::span(p.kid).subspan(e.sym, e.count), e,
                     made, row);
    return;
  }
  // `made` and `row` are already the production's chain: a turn's writes
  // ride its one position.
  const auto priority = lr_priority_alt(o.cfg, lhs);
  const bool turning = p.turning, inTurn = p.inTurn;
  const bool turn = p.turns(made);
  if (turn) p.inTurn = true;
  const bool apart = turning && turn && !row.empty();
  p.turning = turning && !turn;
  std::vector<LrLowerSlot> slots{
      lr_slot_for(o, p, at, LrActs{}, apart ? row : LrActs{})};
  p.turning = turning;
  p.inTurn = inTurn;
  lr_emit_subsets(o, p, lhs, std::move(slots), apart ? made : lr_cat(made, row),
                  priority);
}

// An item that may read nothing becomes a rule that may derive nothing.
inline void lr_symbolize_item(LrLowered& o, LrLowerPool& p, LrLowerSlot& s) {
  if (lr_subset_bit(s)) {
    lr_optional_aux(o, p, s);
  } else if (s.none || s.end) {
    const std::uint16_t aux = lr_new_aux(o.cfg);
    const auto empty = lr_push_prod(o, p, aux, {}, {}, s.end);
    if (s.end && empty != kNoRow) o.cfg.prodBuild[empty].tailCfg = s.cfg;
    lr_close_region(o.cfg, empty, s.trail);
    s = LrLowerSlot{.sym = nt_sym(aux), .cfg = s.cfg, .lead = s.lead,
                    .trail = s.trail};
  }
}

inline void lr_emit_unordered(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                              LrShapeId at, const LrActs& made,
                              const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  const std::size_t n = e.count;
  const std::vector<LrShapeId> kids(p.kid.begin() + e.sym,
                                    p.kid.begin() + e.sym + n);
  const auto hi = p.unorderedHead.find(at), ti = p.unorderedTail.find(at);
  const auto& heads = hi == p.unorderedHead.end() ? kids : hi->second;
  const auto& tails = ti == p.unorderedTail.end() ? kids : ti->second;
  // Every order of the group is spelled; past the pools its rule is refused.
  const auto too_wide = [&] {
    lower_refuse_rule(o.cfg, LowerFail::unorderedTooWide, p.owner);
  };
  bool turns = false;
  for (const LrShapeId k : kids) turns = turns || p.holds_turn(k);
  if (n > unordered_most(turns)) {
    too_wide();
    return;
  }
  // A nest depends on the preceding positions, so its orders stay whole.
  if (turns) {
    std::uint64_t count = 1;
    for (std::size_t i = 2; i <= n; ++i) {
      if (count > (0xffffu - o.cfg.nprods) / i) {
        too_wide();
        return;
      }
      count *= i;
    }
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::vector<LrShapeId> positions(n);
    do {
      for (std::size_t i = 0; i < n; ++i)
        positions[i] = i == 0       ? heads[order[i]]
                       : i + 1 == n ? tails[order[i]]
                                    : kids[order[i]];
      const std::uint16_t before = o.cfg.nprods;
      lr_emit_sequence(o, p, lhs, positions, e, made, row);
      if (o.cfg.lowerFail()) return;
      for (std::uint16_t k = before; k < o.cfg.nprods; ++k)
        if (o.cfg.prods[k].lhs == lhs) o.cfg.prodBuild[k].permuted = true;
    } while (std::next_permutation(order.begin(), order.end()));
    return;
  }

  const std::size_t end = std::size_t{1} << n, full = end - 1;
  const std::size_t productions = n * (end >> 1) - n;
  const bool turning = p.turning;
  std::vector<LrLowerSlot> body(n), head(n), tail(n);
  p.turning = false;
  for (std::size_t i = 0; i < n; ++i) {
    const auto symbol = [&](LrShapeId k) {
      auto s = lr_slot_for(o, p, k, p.e[k].made, p.e[k].row);
      lr_symbolize_item(o, p, s);
      return s;
    };
    body[i] = symbol(kids[i]);
    head[i] = heads[i] == kids[i] ? body[i] : symbol(heads[i]);
    tail[i] = tails[i] == kids[i] ? body[i] : symbol(tails[i]);
  }
  p.turning = turning;
  if (o.cfg.lowerFail()) return;
  // Operand auxiliaries share the same pools as the subset states.
  if (productions > 0xffffu - o.cfg.nprods ||
      end - n - 2 > std::size_t{kNtBase} - o.cfg.nnts) {
    too_wide();
    return;
  }
  // Spelt to the right: the election compares readings from the first item.
  std::vector<std::uint16_t> states(end, kNoRow);
  for (std::size_t mask = 1; mask <= full; ++mask)
    if (!std::has_single_bit(mask))
      states[mask] = mask == full ? lhs : lr_new_aux(o.cfg);
  for (std::size_t mask = 1; mask <= full; ++mask) {
    if (std::has_single_bit(mask)) continue;
    for (std::size_t i = 0; i < n; ++i) {
      if ((mask & (std::size_t{1} << i)) == 0) continue;
      const std::size_t rest = mask ^ (std::size_t{1} << i);
      const LrLowerSlot next = std::has_single_bit(rest)
                                   ? tail[std::countr_zero(rest)]
                                   : LrLowerSlot{.sym = nt_sym(states[rest]),
                                                 .cfg = e.cfg,
                                                 .trail = e.trail};
      const LrLowerSlot slots[] = {mask == full ? head[i] : body[i], next};
      const auto k = lr_push_prod(o, p, states[mask], slots,
                                  mask == full ? lr_cat(made, row) : LrActs{});
      if (k != kNoRow) o.cfg.prodBuild[k].permuted = true;
    }
  }
}

// `made` and `row` are the actions standing over `at`, already resolved.
inline void lr_emit_alts(LrLowered& o, LrLowerPool& p, std::uint16_t lhs,
                         LrShapeId at, const LrActs& made, const LrActs& row) {
  const LrLowerNode& e = p.e[at];
  if (e.kind == LrShape::alt)
    for (unsigned i = 0; i < e.count; ++i) {
      const LrShapeId k = p.kid[e.sym + i];
      // A directive over a choice distributes over its alternatives.
      lr_emit_alts(o, p, lhs, k, lr_cat(p.e[k].made, made),
                   lr_cat(p.e[k].row, row));
    }
  else if (p.passClose != kNoRow && e.kind == LrShape::sym && !is_term(e.sym) &&
           made.size() == 1 && made.front() == p.passClose && row.empty())
    lr_emit_one(o, p, lhs, at, LrActs{}, row);
  else
    lr_emit_one(o, p, lhs, at, made, row);
}

}  // namespace cursiv
