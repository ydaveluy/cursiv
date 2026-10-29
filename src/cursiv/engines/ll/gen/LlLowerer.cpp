#include "cursiv/engines/ll/gen/LlLowerer.hpp"

#include <algorithm>

#include "cursiv/engines/ll/gen/LlRecordPlan.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

using detail::llow::Acts;
using detail::llow::cat;
using detail::llow::emit;
using detail::llow::put_node;
using detail::llow::too_many_positions;

std::uint16_t LlLowerer::intern_name(std::string_view text) {
  return _cfg.intern_name(text);
}

void LlLowerer::declare_start() {
  _cfg.nrules = 1;
  name_row(_cfg.rules[0], "$start");
}

void LlLowerer::declare_rules() { name_rules(_cfg.rules, _cfg.nrules); }

ImageId LlLowerer::ended(ImageId id) { return ended_in(id, false); }

ImageId LlLowerer::ended_in(ImageId id, bool inPredicate) {
  const std::uint64_t memo = std::uint64_t{id} << 1 | (inPredicate ? 1u : 0u);
  if (const auto it = _endedIn.find(memo); it != _endedIn.end())
    return it->second;
  const ImageNode n = at(id);
  const bool predicate =
      n.kind == ImageKind::andOf || n.kind == ImageKind::notOf;
  ImageId out = id;
  if (n.kind == ImageKind::eof && inPredicate) {
    out = section_end();
  } else if (predicate && !inPredicate &&
             at(kid(n, 0)).kind == ImageKind::eof) {
    out = id;
  } else {
    switch (n.kind) {
      case ImageKind::lit:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::implied:
        break;
      default:
        out = map_kids(id, [&](ImageId k) {
          return ended_in(k, inPredicate || predicate);
        });
    }
  }
  _endedIn.emplace(memo, out);
  return out;
}

// An operator reads its rules in place, except a rule it reaches again inside
// its own body: that one stays a call.
ImageId LlLowerer::operator_body(unsigned cfg, ImageId op,
                                 std::vector<std::uint32_t>& reading) {
  const ImageNode n = at(op);
  switch (n.kind) {
    case ImageKind::ref: {
      if (terminal_rule(n.a) || std::ranges::count(reading, n.a) != 0)
        return op;
      reading.push_back(n.a);
      const ImageId body = operator_body(cfg, body_in(cfg, n.a), reading);
      reading.pop_back();
      return body;
    }
    case ImageKind::seq:
    case ImageKind::choice:
      return map_kids(
          op, [&](ImageId k) { return operator_body(cfg, k, reading); });
    default:
      return op;
  }
}

std::uint16_t LlLowerer::climb_chain(const void* row) {
  const std::uint16_t r = row_id(row);
  return _o.writes.intern_chain(&r, 1);
}

void LlLowerer::emit_op(unsigned cfg, ImageId op, const void* opRow,
                        std::uint8_t prec, Association group) {
  const std::uint32_t opRule = op_rule_of(op);
  const bool buildsNode =
      opRule != kNoImage && (node_rule(opRule) || infix_rule(opRule));
  std::vector<std::uint32_t> reading;
  const std::uint16_t laid = lay(cfg, operator_body(cfg, op, reading));
  const auto atomic = [&](const auto& self, std::uint16_t id) -> bool {
    const Node& n = _p.e[id];
    if (n.kind == Shape::sym) return is_term(n.sym);
    if (n.kind == Shape::alt) {
      for (unsigned i = 0; i < n.count; ++i)
        if (!self(self, _p.kid[n.sym + i])) return false;
      return true;
    }
    if (n.kind != Shape::seq) return false;
    unsigned tokens = 0;
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = _p.kid[n.sym + i];
      if (_p.e[k].kind == Shape::condition || _p.e[k].kind == Shape::none)
        continue;
      if (++tokens != 1 || !self(self, k)) return false;
    }
    return tokens == 1;
  };
  const bool whole = atomic(atomic, laid);
  if (buildsNode ||
      (opRule == kNoImage && !whole && _p.e[laid].kind != Shape::implied)) {
    _o.fault.raise(GrammarFaultCode::infixOperatorNotText, _p.owner);
    return;
  }
  if (whole) {
    detail::llow::emit_operator_nodes(_o, _p, laid, climb_chain(opRow), prec,
                                      group, static_cast<std::uint8_t>(cfg));
    return;
  }
  const std::uint16_t action = row_id(opRow);
  const std::uint16_t body = emit(_o, _p, laid, _p.e[laid].made,
                                  cat(_p.e[laid].row, Acts{action}), _p.owner);
  _cfg.climbOps[_cfg.nclimbOps++] =
      LlClimbOp{.body = body, .prec = prec, .assoc = group};
}

void LlLowerer::emit_climb(unsigned cfg, std::uint32_t rule,
                           std::uint16_t lhs) {
  const ImageStageDesc& st = stage(rule);
  if (st.levelCount > 255) {
    _o.fault.raise(GrammarFaultCode::tooManyPrecedences, lhs);
    return;
  }
  LlClimb cl;
  const std::uint16_t base =
      _p.e[lay_ref(cfg, _im.rule(*st.primary))].sym;
  if (is_term(base))
    cl.primaryNode = put_node(_o, LlNode{.kind = LlShape::term, .sym = base});
  else
    cl.primary = nt_index(base);
  cl.node = climb_chain(st.climbRow);
  cl.leftRow = climb_chain(st.leftRow);
  cl.rightRow = climb_chain(st.rightRow);
  cl.opBase = _cfg.nclimbOps;
  each_op(st, [&](ImageId op, const void* row, std::uint8_t prec,
                  Association group) { emit_op(cfg, op, row, prec, group); });
  cl.opCount = static_cast<std::uint16_t>(_cfg.nclimbOps - cl.opBase);
  if (cl.opCount != 0) {
    std::vector<std::uint16_t> arms;
    for (unsigned i = 0; i < cl.opCount; ++i)
      arms.push_back(_cfg.climbOps[cl.opBase + i].body);
    arms.push_back(put_node(_o, LlNode{.kind = LlShape::empty},
                            LlNodeBuild{static_cast<std::uint8_t>(cfg)}));
    cl.selectNode =
        detail::llow::put_group(_o, LlShape::alt, arms, kNoRow,
                                LlNodeBuild{static_cast<std::uint8_t>(cfg)});
    _cfg.nodes[cl.selectNode].decision =
        detail::llow::new_decision(_o, cl.selectNode,
                                   static_cast<std::uint16_t>(arms.size()),
                                   lhs);
  }
  _cfg.rules[lhs].climb = _cfg.nclimbs;
  _cfg.climbs[_cfg.nclimbs++] = cl;
}

void LlLowerer::define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                           std::uint8_t) {
  _p.e.clear();
  _p.kid.clear();
  _p.owner = lhs;
  _cfg.ruleCfg[lhs] = static_cast<std::uint8_t>(cfg);
  const ImageStageDesc& st = stage(rule);
  if (st.kind == ImageRuleKind::infix) {
    emit_climb(cfg, rule, lhs);
    return;
  }
  const std::uint16_t at_ = lay(cfg, body_in(cfg, rule));
  close_body(st, at_);
  _cfg.rules[lhs].body = emit(_o, _p, at_, _p.e[at_].made, _p.e[at_].row, lhs);
}

void LlLowerer::note_nests() {
  _o.nestChains.assign(_o.writes.nchains, 0);
  for (std::uint16_t i = 0; i < _o.writes.nchains; ++i) {
    const Chain chain = _o.writes.chains[i];
    if (chain.count != 0)
      _o.nestChains[i] = _rowKind[_o.writes.acts[chain.off]] == WriteKind::nest;
  }
}

LlLowered LlLowerer::run() {
  index();
  _lex.declare_word(_d.bound);
  const std::uint32_t entry = _im.rule(*_d.entry);
  _cfg.entryRule = static_cast<std::uint16_t>(1 + _ntIdx.at(entry));
  declare();
  define();
  _o.writes.note_makes(_rowKind);
  note_nests();
  LlRecordPlan records(_o.cfg, _o.writes, _o.nestChains);
  records.settle_ownership();
  detail::llow::ride_chains_down(_o, records);
  _o.writes.note_makes(_rowKind);
  note_nests();
  detail::llow::lay_own_records(_o, records);
  return std::move(_o);
}

LlLowered ll_lower(GrammarImage& image, const LowerDesc& desc) {
  return LlLowerer(image, desc).run();
}

}  // namespace cursiv
