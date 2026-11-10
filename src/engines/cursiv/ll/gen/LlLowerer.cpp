#include "cursiv/ll/gen/LlLowerer.hpp"

#include <algorithm>

#include "cursiv/ll/gen/LlRecordPlan.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

using llow::Acts;
using llow::cat;
using llow::emit;
using llow::put_node;
using llow::too_many_positions;

std::uint16_t LlLowerer::intern_name(std::string_view text) {
  return _cfg.intern_name(text);
}

void LlLowerer::declare_start() {
  _cfg.nrules = 1;
  name_row(_cfg.rules[0], "$start");
}

void LlLowerer::declare_rules() { name_rules(_cfg.rules, _cfg.nrules); }

Descent<ImageId> LlLowerer::ended(ImageId id) { return ended_in(id, false); }

Descent<ImageId> LlLowerer::ended_in(ImageId id, bool inPredicate) {
  const std::uint64_t memo = std::uint64_t{id} << 1 | (inPredicate ? 1u : 0u);
  if (const auto it = _endedIn.find(memo); it != _endedIn.end())
    co_return it->second;
  const ImageNode n = at(id);
  const bool predicate =
      n.kind == ImageKind::andOf || n.kind == ImageKind::notOf;
  ImageId out = id;
  if (n.kind == ImageKind::eof && inPredicate) {
    out = section_end();
  } else if (predicate && !inPredicate &&
             at(child_at(n, 0)).kind == ImageKind::eof) {
    out = id;
  } else {
    switch (n.kind) {
      case ImageKind::lit:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::implied:
        break;
      default:
        out = co_await map_children(id, [&](ImageId k) {
          return ended_in(k, inPredicate || predicate);
        });
    }
  }
  _endedIn.emplace(memo, out);
  co_return out;
}

// An operator reads in place the rules it reaches through choices and
// sequences, except one it reaches again in its own body. Each keeps its
// conversion except the rule the operator names, whose row runs it.
Descent<std::uint16_t> LlLowerer::build_operator(
    unsigned cfg, ImageId op, std::vector<std::uint32_t>& reading) {
  const ImageNode n = at(op);
  if (n.kind == ImageKind::ref) {
    if (terminal_rule(n.a) || std::ranges::count(reading, n.a) != 0)
      co_return co_await build(cfg, op);
    reading.push_back(n.a);
    const std::uint16_t body =
        co_await build_operator(cfg, body_in(cfg, n.a), reading);
    reading.pop_back();
    if (const ImageStageDesc& st = stage(n.a);
        !reading.empty() && st.kind == ImageRuleKind::data && !st.folded)
      convert_over(body, row_id(st.convertRow));
    co_return body;
  }
  if ((n.kind != ImageKind::seq && n.kind != ImageKind::choice) ||
      descend(gap(op)))
    co_return co_await build(cfg, op);
  const bool seq = n.kind == ImageKind::seq;
  const std::vector<ImageId> parts =
      seq ? gap_runs(children_of(n)) : children_of(n);
  if (seq && parts.size() == 1)
    co_return co_await build_operator(cfg, parts.front(), reading);
  std::vector<std::uint16_t> children;
  for (const ImageId k : parts)
    children.push_back(co_await build_operator(cfg, k, reading));
  co_return gather(cfg, seq ? Shape::seq : Shape::alt, children);
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
  const std::uint16_t built = descend(build_operator(cfg, op, reading));
  const auto atomic = [&](const auto& self, std::uint16_t id) {
    const Node& n = _p.e[id];
    if (n.kind == Shape::sym) return is_term(n.sym);
    if (n.kind == Shape::alt) {
      for (unsigned i = 0; i < n.count; ++i)
        if (!self(self, _p.child[n.sym + i])) return false;
      return true;
    }
    if (n.kind != Shape::seq) return false;
    unsigned tokens = 0;
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = _p.child[n.sym + i];
      if (_p.e[k].kind == Shape::condition || _p.e[k].kind == Shape::none)
        continue;
      if (++tokens != 1 || !self(self, k)) return false;
    }
    return tokens == 1;
  };
  const bool whole = atomic(atomic, built);
  if (buildsNode ||
      (opRule == kNoImage && !whole && _p.e[built].kind != Shape::implied)) {
    _o.fault.raise(GrammarFaultCode::infixOperatorNotText, _p.owner);
    return;
  }
  const Acts row = cat(_p.e[built].row, Acts{row_id(opRow)});
  if (whole) {
    llow::emit_operator_nodes(_o, _p, built, _p.e[built].made, row, prec, group,
                              static_cast<std::uint8_t>(cfg));
    return;
  }
  const std::uint16_t body =
      descend(emit(_o, _p, built, _p.e[built].made, row, _p.owner));
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
  cl.primary =
      nt_index(_p.e[descend(build_ref(cfg, _im.rule(*st.primary)))].sym);
  cl.node = climb_chain(st.climbRow);
  cl.leftRow = climb_chain(st.leftRow);
  cl.rightRow = climb_chain(st.rightRow);
  cl.opBase = _cfg.nclimbOps;
  each_op(rule, [&](ImageId op, const void* row, std::uint8_t prec,
                    Association group) { emit_op(cfg, op, row, prec, group); });
  cl.opCount = static_cast<std::uint16_t>(_cfg.nclimbOps - cl.opBase);
  if (cl.opCount != 0) {
    std::vector<std::uint16_t> arms;
    for (unsigned i = 0; i < cl.opCount; ++i)
      arms.push_back(_cfg.climbOps[cl.opBase + i].body);
    arms.push_back(put_node(_o, LlNode{.kind = LlShape::empty},
                            LlNodeBuild{static_cast<std::uint8_t>(cfg)}));
    cl.selectNode = llow::with_decision(
        _o,
        llow::put_group(_o, LlShape::alt, arms, kNoRow,
                        LlNodeBuild{static_cast<std::uint8_t>(cfg)}),
        static_cast<std::uint16_t>(arms.size()), lhs);
  }
  _cfg.rules[lhs].climb = _cfg.nclimbs;
  _cfg.climbs[_cfg.nclimbs++] = cl;
}

void LlLowerer::define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                           std::uint8_t) {
  _p.e.clear();
  _p.child.clear();
  _p.owner = lhs;
  _cfg.ruleCfg[lhs] = static_cast<std::uint8_t>(cfg);
  const ImageStageDesc& st = stage(rule);
  if (st.kind == ImageRuleKind::infix) {
    emit_climb(cfg, rule, lhs);
    return;
  }
  const std::uint16_t at_ = descend(build(cfg, body_in(cfg, rule)));
  close_body(st, at_);
  _cfg.rules[lhs].body =
      descend(emit(_o, _p, at_, _p.e[at_].made, _p.e[at_].row, lhs));
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
  records.compute_ownership();
  llow::move_chains_down(_o, records);
  _o.writes.note_makes(_rowKind);
  note_nests();
  llow::add_own_records(_o, records);
  return std::move(_o);
}

LlLowered ll_lower(GrammarImage& image, const LowerDesc& desc) {
  return LlLowerer(image, desc).run();
}

}  // namespace cursiv::engine
