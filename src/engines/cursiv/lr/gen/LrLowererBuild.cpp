#include "cursiv/lr/gen/LrLowerer.hpp"
#include "cursiv/lr/gen/lr_productions.hpp"
#include "cursiv/lr/gen/lr_lower_epsilon.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

// An embed's close, read under the section it ends: the boundary after it is
// the caller's.
Descent<LrShapeId> LrLowerer::build_close(unsigned cfg, unsigned in,
                                          ImageId e) {
  const LrShapeId close = co_await build(in, e);
  _p.e[close].trailing = static_cast<std::uint8_t>(cfg + 1u);
  co_return close;
}

Descent<LrShapeId> LrLowerer::build_repeat(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  if (n.m > 0xffffu || (n.n != kUnbounded && n.n > 0xffffu)) {
    lower_refuse(_cfg, LowerFail::productionTooWide);
    co_return _p.push(LrLowerNode{LrShape::none});
  }
  const LrShapeId k = co_await build(cfg, child_at(n, 0));
  co_return mark(
      cfg,
      _p.push(LrLowerNode{
          LrShape::rep, k, 0, static_cast<std::uint32_t>(n.m),
          n.n == kUnbounded ? kEndless : static_cast<std::uint32_t>(n.n)}));
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
  using enum LrShape;
  while (_p.e[at].kind == rep || _p.e[at].kind == scope) at = _p.e[at].sym;
  const auto& node = _p.e[at];
  return (node.kind == seq && node.count == 0) ||
         (node.kind == sym && _p.is_gap(static_cast<std::uint16_t>(node.sym)));
}

Descent<void> LrLowerer::ambient(LrShapeId at, bool open, std::uint8_t edge) {
  (open ? _p.e[at].leading : _p.e[at].trailing) = edge;
  const auto kind = _p.e[at].kind;
  const auto base = _p.e[at].sym;
  const auto count = _p.e[at].count;
  const std::size_t nrules = _d.nts.size();
  const std::size_t ncfgs = _cfgs.size();
  if (kind == LrShape::sym) {
    const auto sym = static_cast<std::uint16_t>(base);
    if (open || is_term(sym)) co_return;
    // A commit's exit that ends the region stands past it, as a loop's own end
    // does: the token in hand is read for the caller as well.
    if (const std::uint16_t cut = _p.stop_of(sym); cut != 0) {
      _p.e[at].sym = nt_sym(closing_stop(cut, edge));
      co_return;
    }
    std::uint16_t reading = nt_index(sym);
    if (const auto was = _p.openOf.find(reading); was != _p.openOf.end())
      reading = was->second.first;
    if (reading == 0 || reading > ncfgs * nrules) co_return;
    const auto view = static_cast<std::uint8_t>((reading - 1u) / nrules);
    const auto tail = static_cast<std::uint8_t>(edge - 1u);
    _p.e[at].sym = nt_sym(tail == view ? reading : open_tail(reading, tail));
  } else if (kind == LrShape::seq) {
    for (unsigned j = 0; j < count; ++j) {
      const LrShapeId k = _p.child[base + (open ? j : count - 1 - j)];
      co_await ambient(k, open, edge);
      // A condition that ends the region reads past it, so what it follows
      // ends the region too. A nest's checkpoint holds no position, so what
      // follows it opens the region.
      const auto held = _p.e[k].kind;
      const bool passes = open
                              ? held == LrShape::end ||
                                    (held == LrShape::seq && _p.e[k].count == 0)
                              : is_condition(k);
      if (held != LrShape::none && !passes) break;
    }
  } else if (kind == LrShape::alt) {
    for (unsigned i = 0; i < count; ++i)
      co_await ambient(_p.child[base + i], open, edge);
  } else if (kind == LrShape::unordered) {
    std::vector<LrShapeId> children;
    children.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      const LrShapeId k = co_await _p.copy(_p.child[base + i]);
      co_await ambient(k, open, edge);
      children.push_back(k);
    }
    (open ? _p.unorderedHead : _p.unorderedTail)[at] = std::move(children);
  } else if (kind == LrShape::rep || kind == LrShape::scope) {
    co_await ambient(base, open, edge);
  }
}

Descent<LrShapeId> LrLowerer::build_skip(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const unsigned in = region_of(cfg, n);
  const LrShapeId node = co_await build(in, child_at(n, 0));
  if (in != cfg) {
    const auto edge = static_cast<std::uint8_t>(cfg + 1u);
    co_await ambient(node, true, edge);
    co_await ambient(node, false, edge);
  }
  co_return node;
}

Descent<LrShapeId> LrLowerer::build_create(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const LrShapeId node = co_await build(cfg, child_at(n, 0));
  _p.e[node].made.push_back(row_id(n.row));
  co_return node;
}

Descent<LrShapeId> LrLowerer::capture_created(LrShapeId at) {
  for (const std::uint16_t r : _p.e[at].made)
    if (_p.rowKind[r] == WriteKind::create ||
        _p.rowKind[r] == WriteKind::climb) {
      const LrLowerNode& e = _p.e[at];
      co_return _p.push(LrLowerNode{.kind = LrShape::scope,
                                    .sym = at,
                                    .cfg = e.cfg,
                                    .leading = e.leading,
                                    .trailing = e.trailing});
    }
  if (_p.e[at].kind == LrShape::rep && _p.e[at].max == 1) {
    const LrShapeId child = co_await capture_created(_p.e[at].sym);
    _p.e[at].sym = child;
  }
  co_return at;
}

LrShapeId LrLowerer::once_site(LrShapeId at) const {
  while (_p.e[at].kind == LrShape::rep && _p.e[at].max == 1) at = _p.e[at].sym;
  return at;
}

Descent<LrShapeId> LrLowerer::build_write(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const LrShapeId node =
      co_await capture_created(co_await build(cfg, child_at(n, 0)));
  _p.e[once_site(node)].row.push_back(row_id(n.row));
  co_return node;
}

Descent<LrShapeId> LrLowerer::build_flag(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  LrShapeId node =
      co_await capture_created(co_await build(cfg, child_at(n, 0)));
  // A flag over a repetition that may read nothing sets on the empty path
  // too: the row stands on a sequence around it, not on the item.
  if (_p.e[node].kind == LrShape::rep && _p.e[node].min == 0)
    node = wrap(cfg, node);
  _p.e[node].row.push_back(row_id(n.row));
  co_return node;
}

Descent<LrShapeId> LrLowerer::build(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::eof:
      co_return mark(cfg, _p.push(LrLowerNode{LrShape::end}));
    case ImageKind::skip:
      co_return co_await build_skip(cfg, e);
    case ImageKind::notOf: {
      if (const ImageNode k = at(child_at(n, 0));
          k.kind == ImageKind::trigger) {
        const Cut cut = co_await cut_prefix(child_at(k, 0));
        if (cut.ok && cut.guards.empty()) co_return build_stop(cfg, e, cut);
      }
      co_return build_condition(cfg, e);
    }
    default:
      co_return co_await build_shared(cfg, e, n);
  }
}

LrShapeId LrLowerer::build_condition(unsigned cfg, ImageId e) {
  const std::uint16_t k = gap_cond(cfg, e);
  if (_p.gapNt.size() <= k) _p.gapNt.resize(k + 1u, 0);
  if (_p.gapNt[k] == 0) {
    const std::uint16_t aux = lr_new_aux(_cfg);
    if (const std::uint16_t prod = lr_push_prod(_o, _p, aux, {});
        prod != kNoRow)
      _cfg.prods[prod].gap = static_cast<std::uint8_t>(k + 1u);
    _p.gapNt[k] = static_cast<std::uint16_t>(aux + 1u);
  }
  return push_sym(nt_sym(_p.gapNt[k] - 1u), cfg);
}

// The empty rule of a commit's exit, its production pushed under `trailing`.
std::uint16_t LrLowerer::stop_rule(DynSet terms, std::uint8_t trailing) {
  const std::uint16_t aux = lr_new_aux(_cfg);
  const std::uint16_t prod = lr_push_prod(_o, _p, aux, {});
  _cfg.stopCuts.push_back(std::move(terms));
  if (prod != kNoRow)
    _cfg.prods[prod].stop =
        static_cast<std::uint16_t>(_cfg.stopCuts.size());
  lr_close_region(_cfg, prod, trailing);
  _p.stopNt.push_back(static_cast<std::uint16_t>(aux + 1u));
  return aux;
}

LrShapeId LrLowerer::build_stop(unsigned cfg, ImageId e, const Cut& cut) {
  const ImageId trigger = child_at(at(e), 0);
  const auto [at_, fresh] =
      _p.stopCut.try_emplace(key(Key::stop, cfg, trigger),
                             static_cast<std::uint16_t>(_p.stopNt.size()));
  if (fresh) {
    DynSet terms;
    descend(cut_terms(cfg, cut.token, terms));
    stop_rule(std::move(terms), 0);
  }
  return push_sym(nt_sym(_p.stopNt[at_->second] - 1u), cfg);
}

std::uint16_t LrLowerer::closing_stop(std::uint16_t cut,
                                      std::uint8_t trailing) {
  const auto [at, fresh] = _p.stopTail.try_emplace({cut, trailing}, 0);
  if (fresh) at->second = stop_rule(_cfg.stopCuts[cut - 1u], trailing);
  return at->second;
}

Descent<LrShapeId> LrLowerer::build_implied(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  check_implied_token(cfg, child_at(n, 0));
  const std::uint16_t k = gap_cond(cfg, child_at(n, 1));
  co_return push_sym(implied_term(e, k), cfg);
}

}  // namespace cursiv::engine
