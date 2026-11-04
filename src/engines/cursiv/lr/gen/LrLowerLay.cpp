#include "cursiv/lr/gen/LrLowerer.hpp"
#include "cursiv/lr/gen/lr_lower_define.hpp"
#include "cursiv/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/table/Symbol.hpp"

namespace cursiv {

// An embed's close, read under the section it ends: the boundary after it is
// the caller's.
LrShapeId LrLowerer::lay_close(unsigned cfg, unsigned in, ImageId e) {
  const LrShapeId close = lay(in, e);
  _p.e[close].trail = static_cast<std::uint8_t>(cfg + 1u);
  return close;
}

LrShapeId LrLowerer::lay_repeat(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  if (n.m > 0xffffu || (n.n != kUnbounded && n.n > 0xffffu)) {
    lower_refuse(_cfg, LowerFail::productionTooWide);
    return _p.push(LrLowerNode{LrShape::none});
  }
  const LrShapeId k = lay(cfg, kid(n, 0));
  return mark(cfg, _p.push(LrLowerNode{LrShape::rep, k, 0,
                                       static_cast<std::uint32_t>(n.m),
                                       n.n == kUnbounded
                                           ? kEndless
                                           : static_cast<std::uint32_t>(n.n)}));
}

std::uint16_t LrLowerer::open_tail(std::uint16_t reading, std::uint8_t tail) {
  const auto [at, fresh] = _p.openTail.try_emplace({reading, tail}, 0);
  if (!fresh) return at->second;
  if (_cfg.nnts >= kNtBase) {
    _p.openTail.erase(at);
    lower_refuse(_cfg, LowerFail::tooManyRules);
    return reading;
  }
  const LrNtInfo row = _cfg.nts[reading];
  const std::uint16_t nt = _cfg.nnts++;
  ++_cfg.auxCount;
  _cfg.nts[nt] = row;
  at->second = nt;
  _p.openOf.emplace(nt, std::pair{reading, tail});
  return nt;
}

// A condition, bare or under a repetition or a scope: it takes no position.
bool LrLowerer::is_condition(LrShapeId at) const {
  while (_p.e[at].kind == LrShape::rep || _p.e[at].kind == LrShape::scope)
    at = _p.e[at].sym;
  const auto& node = _p.e[at];
  return (node.kind == LrShape::seq && node.count == 0) ||
         (node.kind == LrShape::sym &&
          _p.is_gap(static_cast<std::uint16_t>(node.sym)));
}

void LrLowerer::ambient(LrShapeId at, bool open, std::uint8_t edge) {
  (open ? _p.e[at].lead : _p.e[at].trail) = edge;
  const auto kind = _p.e[at].kind;
  const auto base = _p.e[at].sym;
  const auto count = _p.e[at].count;
  const std::size_t nrules = _d.nts.size();
  const std::size_t ncfgs = _cfgs.size();
  if (kind == LrShape::sym) {
    const auto sym = static_cast<std::uint16_t>(base);
    if (open || is_term(sym)) return;
    // A commit's exit that ends the region stands past it, as a loop's own end
    // does: the token in hand is read for the caller as well.
    if (const std::uint16_t cut = _p.stop_of(sym); cut != 0) {
      _p.e[at].sym = nt_sym(closing_stop(cut, edge));
      return;
    }
    std::uint16_t reading = nt_index(sym);
    if (const auto was = _p.openOf.find(reading); was != _p.openOf.end())
      reading = was->second.first;
    if (reading == 0 || reading > ncfgs * nrules) return;
    const auto view = static_cast<std::uint8_t>((reading - 1u) / nrules);
    const auto tail = static_cast<std::uint8_t>(edge - 1u);
    _p.e[at].sym = nt_sym(tail == view ? reading : open_tail(reading, tail));
  } else if (kind == LrShape::seq) {
    for (unsigned j = 0; j < count; ++j) {
      const LrShapeId k = _p.kid[base + (open ? j : count - 1 - j)];
      ambient(k, open, edge);
      // A condition that ends the region reads past it, so what it follows
      // ends the region too. A nest's checkpoint holds no position, so what
      // follows it opens the region.
      const auto held = _p.e[k].kind;
      const bool passes =
          open ? held == LrShape::end ||
                     (held == LrShape::seq && _p.e[k].count == 0)
               : is_condition(k);
      if (held != LrShape::none && !passes) break;
    }
  } else if (kind == LrShape::alt) {
    for (unsigned i = 0; i < count; ++i) ambient(_p.kid[base + i], open, edge);
  } else if (kind == LrShape::unordered) {
    std::vector<LrShapeId> kids;
    kids.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      const LrShapeId k = _p.copy(_p.kid[base + i]);
      ambient(k, open, edge);
      kids.push_back(k);
    }
    (open ? _p.unorderedHead : _p.unorderedTail)[at] = std::move(kids);
  } else if (kind == LrShape::rep || kind == LrShape::scope) {
    ambient(base, open, edge);
  }
}

LrShapeId LrLowerer::lay_skip(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const unsigned in = region_of(cfg, n);
  const LrShapeId node = lay(in, kid(n, 0));
  if (in != cfg) {
    const auto edge = static_cast<std::uint8_t>(cfg + 1u);
    ambient(node, true, edge);
    ambient(node, false, edge);
  }
  return node;
}

LrShapeId LrLowerer::lay_made(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const LrShapeId node = lay(cfg, kid(n, 0));
  if (n.kind == ImageKind::nest) {
    if (_p.e[node].kind == LrShape::none || _p.e[node].kind == LrShape::end) {
      _o.fault.raise(GrammarFaultCode::directiveOnNoPosition, _p.owner);
      return node;
    }
    // The receiver is complete before the payload starts. In particular an
    // optional following nest must not become part of this receiver's CST.
    const auto marker = mark(cfg, _p.push(LrLowerNode{
        .kind = LrShape::seq, .made = {row_id(n.row)}}));
    return gather(cfg, LrShape::seq, {marker, node});
  }
  _p.e[node].made.push_back(row_id(n.row));
  return node;
}

LrShapeId LrLowerer::capture_created(LrShapeId at) {
  for (const std::uint16_t r : _p.e[at].made)
    if (_p.rowKind[r] == WriteKind::create ||
        _p.rowKind[r] == WriteKind::climb) {
      const LrLowerNode& e = _p.e[at];
      return _p.push(LrLowerNode{.kind = LrShape::scope,
                                 .sym = at,
                                 .cfg = e.cfg,
                                 .lead = e.lead,
                                 .trail = e.trail});
    }
  if (_p.e[at].kind == LrShape::rep && _p.e[at].max == 1) {
    const LrShapeId child = capture_created(_p.e[at].sym);
    _p.e[at].sym = child;
  }
  return at;
}

LrShapeId LrLowerer::once_site(LrShapeId at) const {
  while (_p.e[at].kind == LrShape::rep && _p.e[at].max == 1) at = _p.e[at].sym;
  return at;
}

LrShapeId LrLowerer::lay_write(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const LrShapeId node = capture_created(lay(cfg, kid(n, 0)));
  _p.e[once_site(node)].row.push_back(row_id(n.row));
  return node;
}

LrShapeId LrLowerer::lay_flag(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  LrShapeId node = capture_created(lay(cfg, kid(n, 0)));
  // A flag over a repetition that may read nothing sets on the empty path
  // too: the row stands on a sequence around it, not on the item.
  if (_p.e[node].kind == LrShape::rep && _p.e[node].min == 0)
    node = wrap(cfg, node);
  _p.e[node].row.push_back(row_id(n.row));
  return node;
}

LrShapeId LrLowerer::lay(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::eof:
      return mark(cfg, _p.push(LrLowerNode{LrShape::end}));
    case ImageKind::unordered:
      return lay_unordered(cfg, e);
    case ImageKind::skip:
      return lay_skip(cfg, e);
    case ImageKind::create:
    case ImageKind::nest:
      return lay_made(cfg, e);
    case ImageKind::notOf: {
      if (const ImageNode k = at(kid(n, 0)); k.kind == ImageKind::trigger) {
        const Cut cut = cut_prefix(kid(k, 0));
        if (cut.ok && cut.guards.empty()) return lay_stop(cfg, e, cut);
      }
      return lay_condition(cfg, e);
    }
    default:
      return lay_shared(cfg, e, n);
  }
}

LrShapeId LrLowerer::lay_condition(unsigned cfg, ImageId e) {
  const std::uint16_t k = gap_cond(cfg, e);
  if (_p.gapNt.size() <= k) _p.gapNt.resize(k + 1u, 0);
  if (_p.gapNt[k] == 0) {
    const std::uint16_t aux = lr_new_aux(_cfg);
    if (const std::uint16_t prod = lr_push_prod(_o, _p, aux, {}); prod != kNoRow)
      _cfg.prods[prod].gap = static_cast<std::uint8_t>(k + 1u);
    _p.gapNt[k] = static_cast<std::uint16_t>(aux + 1u);
  }
  return push_sym(nt_sym(_p.gapNt[k] - 1u), cfg);
}

// The empty rule of a commit's exit, its production pushed under `trail`.
std::uint16_t LrLowerer::stop_rule(DynSet terms, std::uint8_t trail) {
  const std::uint16_t aux = lr_new_aux(_cfg);
  const std::uint16_t prod = lr_push_prod(_o, _p, aux, {});
  _cfg.stopCuts.push_back(std::move(terms));
  if (prod != kNoRow)
    _cfg.prodBuild[prod].stop = static_cast<std::uint16_t>(_cfg.stopCuts.size());
  lr_close_region(_cfg, prod, trail);
  _p.stopNt.push_back(static_cast<std::uint16_t>(aux + 1u));
  return aux;
}

LrShapeId LrLowerer::lay_stop(unsigned cfg, ImageId e, const Cut& cut) {
  const ImageId trigger = kid(at(e), 0);
  const auto [at_, fresh] = _p.stopCut.try_emplace(
      key(Key::stop, cfg, trigger),
      static_cast<std::uint16_t>(_p.stopNt.size()));
  if (fresh) {
    DynSet terms;
    cut_terms(cfg, cut.token, terms);
    stop_rule(std::move(terms), 0);
  }
  return push_sym(nt_sym(_p.stopNt[at_->second] - 1u), cfg);
}

std::uint16_t LrLowerer::closing_stop(std::uint16_t cut, std::uint8_t trail) {
  const auto [at, fresh] = _p.stopTail.try_emplace({cut, trail}, 0);
  if (fresh) at->second = stop_rule(_cfg.stopCuts[cut - 1u], trail);
  return at->second;
}

LrShapeId LrLowerer::lay_implied(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  check_implied_token(cfg, kid(n, 0));
  const std::uint16_t k = gap_cond(cfg, kid(n, 1));
  return push_sym(implied_term(e, k), cfg);
}

}  // namespace cursiv
