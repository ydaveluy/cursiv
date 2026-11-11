#include <span>

#include "cursiv/lr/gen/LrLowerer.hpp"
#include "cursiv/lr/gen/lr_productions.hpp"
#include "cursiv/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/shared/gen/WrittenReads.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

LrLowerSlot LrLowerer::infix_side(unsigned cfg, const void* row,
                                  std::uint16_t sym) const {
  return LrLowerSlot{
      .sym = sym, .cfg = static_cast<std::uint8_t>(cfg), .row = {row_id(row)}};
}

void LrLowerer::emit_op(unsigned cfg, std::uint16_t lhs, ImageId op,
                        const void* opRow, const LrLowerSlot& left,
                        const LrLowerSlot& right, const Acts& node,
                        std::uint8_t prec, Association group) {
  const bool buildsNode = op_builds_node(op);
  const LrShapeId built = descend(build(cfg, op));
  std::vector children{built};
  if (_p.e[built].kind == LrShape::seq && _p.e[built].made.empty())
    children.assign(_p.child.begin() + _p.e[built].sym,
                    _p.child.begin() + _p.e[built].sym + _p.e[built].count);
  std::vector<LrLowerSlot> before, after;
  std::uint16_t sym = kNoRow;
  std::string_view doc;
  bool plain = !buildsNode;
  for (const LrShapeId k : children) {
    LrLowerNode n = _p.e[k];
    // An operator read in place stands as one symbol: its token, which the
    // operator's own row converts, or a record over its tokens.
    if (n.kind == LrShape::sym)
      std::erase_if(n.made, [&](std::uint16_t r) {
        return _p.rowKind[r] == WriteKind::convert;
      });
    if (n.kind != LrShape::sym && n.kind != LrShape::alt &&
        (n.kind != LrShape::seq || n.made.empty())) {
      plain = false;
      continue;
    }
    const LrLowerSlot s = descend(lr_slot_for(_o, _p, k, n.made, n.row));
    if (_p.is_gap(s.sym))
      (sym == kNoRow ? before : after).push_back(s);
    else if (sym == kNoRow && !s.optional && !s.none && !s.end &&
             s.made.empty() && s.row.empty()) {
      sym = s.sym;
      doc = s.doc;
    } else
      plain = false;
  }
  if (!plain || sym == kNoRow) {
    _o.fault.raise(GrammarFaultCode::infixOperatorNotText, lhs);
    return;
  }
  std::vector<LrLowerSlot> slots{left};
  slots.insert(slots.end(), before.begin(), before.end());
  const auto slot = static_cast<std::uint16_t>(slots.size());
  slots.push_back(infix_side(cfg, opRow, sym));
  slots.back().doc = doc;
  slots.insert(slots.end(), after.begin(), after.end());
  slots.push_back(right);
  const std::uint16_t prod =
      lr_push_prod(_o, _p, lhs, slots, node, false, prec);
  if (prod != kNoRow)
    _o.infixOps.push_back(InfixOp{prod, lhs, prec, group, slot});
}

void LrLowerer::define_infix(unsigned cfg, std::uint32_t rule,
                             std::uint16_t lhs) {
  const ImageStageDesc& st = stage(rule);
  if (st.levelCount > 255) {
    _o.fault.raise(GrammarFaultCode::tooManyPrecedences, lhs);
    return;
  }
  const Acts node{row_id(st.climbRow)};
  const LrLowerSlot left = infix_side(cfg, st.leftRow, nt_sym(lhs));
  const LrLowerSlot right = infix_side(cfg, st.rightRow, nt_sym(lhs));
  each_op(rule, [&](ImageId op, const void* row, std::uint8_t prec,
                    Association group) {
    emit_op(cfg, lhs, op, row, left, right, node, prec, group);
  });
  const std::uint32_t primary = _im.rule(*st.primary);
  const LrLowerSlot base{.sym = sym_of(cfg, primary),
                         .cfg = static_cast<std::uint8_t>(cfg)};
  if (cfg != 0 && !is_term(base.sym)) _cfg.cloneNeeded[nt_index(base.sym)] = true;
  lr_push_prod(_o, _p, lhs, std::span(&base, 1));
}

void LrLowerer::define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                           std::uint8_t tail) {
  _p.owner = lhs;
  _p.sectionCfg = static_cast<std::uint8_t>(_cfgs[cfg].sectionCfg);
  const ImageStageDesc& st = stage(rule);
  if (st.kind == ImageRuleKind::infix) {
    define_infix(cfg, rule, lhs);
    return;
  }
  const LrShapeId at_ = descend(build(cfg, body_in(cfg, rule)));
  if (tail != 0) descend(ambient(at_, false, tail));
  if (close_body(st, at_) &&
      !descend(holds(
          body_in(cfg, rule),
          [](const ImageNode& n) { return is_directive_kind(n.kind); })) &&
      rule != _im.rule(*_d.entry))
    _p.passClose = _p.e[at_].made.back();
  descend(lr_emit_alts(_o, _p, lhs, at_, _p.e[at_].made, _p.e[at_].row));
  _p.passClose = kNoRow;
}

bool LrLowerer::open_one(unsigned cfg, std::uint32_t rule,
                         std::uint16_t reading) {
  bool grew = false;
  for (auto at_ = _p.openTail.lower_bound({reading, 0});
       at_ != _p.openTail.end() && at_->first.first == reading;) {
    const auto [key, nt] = *at_;
    if (_p.openDone.insert(nt).second) {
      define_one(cfg, rule, nt, static_cast<std::uint8_t>(key.second + 1u));
      grew = true;
      at_ = _p.openTail.upper_bound(key);
    } else {
      ++at_;
    }
  }
  return grew;
}

bool LrLowerer::define_open() {
  return define_pass(
      0, [this](auto k, auto rule, auto i) { return open_one(k, rule, i); });
}

}  // namespace cursiv::engine
