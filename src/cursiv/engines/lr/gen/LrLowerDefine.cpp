#include <span>

#include "cursiv/engines/lr/gen/LrLowerer.hpp"
#include "cursiv/engines/lr/gen/lr_lower_define.hpp"
#include "cursiv/engines/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

namespace {

Association group_of(ImageGroup g) {
  switch (g) {
    case ImageGroup::right:
      return Association::right;
    case ImageGroup::non:
      return Association::non;
    default:
      return Association::left;
  }
}

}  // namespace

LrLowerSlot LrLowerer::infix_side(unsigned cfg, const void* row,
                                  std::uint16_t sym) {
  LrLowerSlot s;
  s.sym = sym;
  s.cfg = static_cast<std::uint8_t>(cfg);
  s.row.push_back(row_id(row));
  return s;
}

void LrLowerer::emit_op(unsigned cfg, std::uint16_t lhs, ImageId op,
                        const void* opRow, const LrLowerSlot& left,
                        const LrLowerSlot& right, const LrActs& node,
                        std::uint8_t prec, Association group) {
  const std::uint32_t opRule = op_rule_of(op);
  const bool buildsNode =
      opRule != kNoImage && (node_rule(opRule) || infix_rule(opRule));
  const LrShapeId laid = lay(cfg, op);
  std::vector<LrShapeId> kids{laid};
  if (_p.e[laid].kind == LrShape::seq && _p.e[laid].made.empty())
    kids.assign(_p.kid.begin() + _p.e[laid].sym,
                _p.kid.begin() + _p.e[laid].sym + _p.e[laid].count);
  std::vector<LrLowerSlot> before, after;
  std::uint16_t sym = kNoRow;
  bool plain = !buildsNode;
  for (const LrShapeId k : kids) {
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
    const LrLowerSlot s = lr_slot_for(_o, _p, k, n.made, n.row);
    if (_p.is_gap(s.sym))
      (sym == kNoRow ? before : after).push_back(s);
    else if (sym == kNoRow && !s.optional && !s.none && !s.end &&
             s.made.empty() && s.row.empty())
      sym = s.sym;
    else
      plain = false;
  }
  if (!plain || sym == kNoRow) {
    _o.fault.raise(GrammarFaultCode::infixOperatorNotText, lhs);
    return;
  }
  std::vector<LrLowerSlot> slots{left};
  slots.insert(slots.end(), before.begin(), before.end());
  const auto slot = static_cast<std::uint8_t>(slots.size());
  slots.push_back(infix_side(cfg, opRow, sym));
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
  const LrActs node{row_id(st.climbRow)};
  const LrLowerSlot left = infix_side(cfg, st.leftRow, nt_sym(lhs));
  const LrLowerSlot right = infix_side(cfg, st.rightRow, nt_sym(lhs));
  for (std::uint32_t i = 0; i < st.levelCount; ++i) {
    const ImageStageLevelDesc& level = st.levels[i];
    const auto prec = static_cast<std::uint8_t>(st.levelCount - i);
    for (std::uint32_t j = 0; j < level.count; ++j)
      emit_op(cfg, lhs, _im.node(*level.ops[j].op), level.ops[j].row, left,
              right, node, prec, group_of(level.group));
  }
  const std::uint32_t primary = _im.rule(*st.primary);
  const LrLowerSlot base{.sym = sym_of(cfg, primary),
                         .cfg = static_cast<std::uint8_t>(cfg)};
  if (cfg != 0 && !is_term(base.sym)) _cfg.cloneNeeded[nt_index(base.sym)] = true;
  lr_push_prod(_o, _p, lhs, std::span(&base, 1));
}

void LrLowerer::define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                           std::uint8_t tail) {
  _p.owner = lhs;
  _p.home = static_cast<std::uint8_t>(_cfgs[cfg].home);
  const ImageStageDesc& st = stage(rule);
  if (st.kind == ImageRuleKind::infix) {
    define_infix(cfg, rule, lhs);
    return;
  }
  const LrShapeId at_ = lay(cfg, body_in(cfg, rule));
  if (tail != 0) ambient(at_, false, tail);
  if (st.kind == ImageRuleKind::node) {
    bool opens = false;
    for (const unsigned short i : _p.e[at_].made) {
      const WriteKind k = _p.rowKind[i];
      opens = opens || k == WriteKind::create || k == WriteKind::climb ||
              k == WriteKind::close;
    }
    if (!opens) {
      _p.e[at_].made.push_back(row_id(st.closeRow));
      const bool writes =
          has_rows(body_in(cfg, rule)) || rule == _im.rule(*_d.entry);
      if (!writes) _p.passClose = _p.e[at_].made.back();
    }
  }
  if (st.kind == ImageRuleKind::data)
    _p.e[at_].made.push_back(row_id(st.convertRow));
  lr_emit_alts(_o, _p, lhs, at_, _p.e[at_].made, _p.e[at_].row);
  _p.passClose = kNoRow;
}

bool LrLowerer::open_one(unsigned cfg, std::uint32_t rule,
                         std::uint16_t reading) {
  bool grew = false;
  if (!reads_under(cfg, rule)) return false;
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
  bool grew = false;
  const auto n = static_cast<std::uint16_t>(_ntRule.size());
  for (unsigned k = 0; k < _cfgs.size(); ++k) {
    auto i = static_cast<std::uint16_t>(k * n);
    for (const std::uint32_t rule : _ntRule) {
      ++i;
      grew = open_one(k, rule, i) || grew;
    }
  }
  return grew;
}

}  // namespace cursiv
