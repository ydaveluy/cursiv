#include <algorithm>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/engines/ll/gen/LlLowerer.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

std::uint16_t LlLowerer::lay_repeat(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const auto bound = [](std::uint64_t v) {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, kEndless - 1));
  };
  const std::uint16_t k = lay(cfg, kid(n, 0));
  return mark(cfg, _p.push(Node{Shape::rep, k, 0, bound(n.m),
                                n.n == kUnbounded ? kEndless : bound(n.n)}));
}

std::uint16_t LlLowerer::lay_create(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const std::uint16_t inner = lay(cfg, kid(n, 0));
  const std::uint16_t wrapper = wrap(cfg, inner);
  _p.e[wrapper].made.push_back(row_id(n.row));
  return wrapper;
}

std::uint16_t LlLowerer::lay_nest(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  std::uint16_t at_ = lay(cfg, kid(n, 0));
  if (!_p.e[at_].row.empty() ||
      (_p.e[at_].kind == Shape::sym && is_term(_p.e[at_].sym)))
    at_ = wrap(cfg, at_);
  _p.e[at_].made.push_back(row_id(n.row));
  return at_;
}

std::uint16_t LlLowerer::lay_closed(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const bool step = (n.flags & 1) != 0;
  const bool hands = (n.flags & 2) != 0;
  const ImageId inner = kid(n, 0);
  const std::uint16_t child = lay(cfg, inner);
  const ImageStageDesc& st = stage(n.a);
  const bool data = st.kind == ImageRuleKind::data;
  if (data && at(inner).kind == ImageKind::closed && _p.e[child].layered &&
      _p.e[child].foldStep == step) {
    _p.e[child].made.push_back(row_id(st.convertRow));
    return child;
  }
  if (!data && !(hands && st.kind == ImageRuleKind::node)) return child;
  const std::uint16_t wrapper = wrap(cfg, child);
  Node& w = _p.e[wrapper];
  w.foldStep = step;
  w.layered = data;
  w.made.push_back(row_id(data ? st.convertRow : st.closeRow));
  return wrapper;
}

std::uint16_t LlLowerer::lay_write(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  std::uint16_t at_ = lay(cfg, kid(n, 0));
  std::uint16_t parent = kNoRow;
  std::uint16_t site = at_;
  while (_p.e[site].kind == Shape::rep && _p.e[site].max == 1) {
    parent = site;
    site = _p.e[site].sym;
  }
  if (!_p.e[site].made.empty()) {
    const std::uint16_t wrapper = wrap(cfg, site);
    if (parent == kNoRow)
      at_ = wrapper;
    else
      _p.e[parent].sym = wrapper;
    site = wrapper;
  }
  _p.e[site].row.push_back(row_id(n.row));
  return at_;
}

std::uint16_t LlLowerer::lay_flag(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  std::uint16_t at_ = lay(cfg, kid(n, 0));
  // The absent arm is a position of its own: its record stands before the
  // gap, as an implied token does.
  if (_p.e[at_].kind == Shape::rep && _p.e[at_].min == 0 &&
      _p.e[at_].max == 1) {
    const std::uint16_t item = _p.e[at_].sym;
    at_ = gather(cfg, Shape::alt, {item, _p.push(Node{Shape::none})});
  }
  _p.e[at_].row.push_back(row_id(n.row));
  return at_;
}

std::uint16_t LlLowerer::lay_condition(unsigned cfg, ImageId e) {
  const std::uint16_t k = gap_cond(cfg, e);
  return mark(cfg, _p.push(Node{Shape::condition, k}));
}

std::uint16_t LlLowerer::lay_implied(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  check_implied_token(cfg, kid(n, 0));
  const std::uint16_t sym = _p.e[lay(cfg, kid(n, 0))].sym;
  const std::uint16_t k = gap_cond(cfg, kid(n, 1));
  return mark(cfg, _p.push(Node{Shape::implied, sym, 0, k}));
}

std::uint16_t LlLowerer::lay(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::ref:
      return lay_ref(cfg, n.a);
    case ImageKind::epsilon:
      return _p.push(Node{Shape::none});
    case ImageKind::eof: {
      if (_cfgs[cfg].end == kNoImage)
        return mark(cfg, _p.push(Node{Shape::end}));
      const std::uint16_t close = lay(cfg, _cfgs[cfg].end);
      return mark(cfg, _p.push(Node{Shape::end, _p.e[close].sym}));
    }
    case ImageKind::embed:
      return lay_embed(cfg, e);
    case ImageKind::seq:
      return gap(e) ? lay_condition(cfg, e) : lay_seq(cfg, e);
    case ImageKind::choice:
      return gap(e) ? lay_condition(cfg, e)
                    : lay_kids(cfg, Shape::alt, kids_of(n));
    case ImageKind::unordered: {
      const std::uint16_t node = lay_unordered(cfg, e);
      _p.e[node].unorderedTurns = buries(e);
      return node;
    }
    case ImageKind::repeat:
      return lay_repeat(cfg, e);
    case ImageKind::skip:
      return lay(region_of(cfg, n), kid(n, 0));
    case ImageKind::create:
      return lay_create(cfg, e);
    case ImageKind::nest:
      return lay_nest(cfg, e);
    case ImageKind::closed:
      return lay_closed(cfg, e);
    case ImageKind::turns: {
      const std::uint16_t node = lay(cfg, kid(n, 0));
      _p.e[node].leftFold = true;
      return node;
    }
    case ImageKind::assign:
    case ImageKind::append:
      return lay_write(cfg, e);
    case ImageKind::flag:
      return lay_flag(cfg, e);
    case ImageKind::gapHas:
    case ImageKind::gapIs:
    case ImageKind::andOf:
    case ImageKind::notOf:
      return lay_condition(cfg, e);
    case ImageKind::implied:
      return lay_implied(cfg, e);
    default:  // a token, or a combinator `terminal_sym` refuses
      return push_sym(terminal_sym(cfg, e), cfg);
  }
}

}  // namespace cursiv
