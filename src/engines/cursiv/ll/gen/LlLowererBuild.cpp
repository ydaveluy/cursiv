#include <algorithm>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/ll/gen/LlLowerer.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

Descent<std::uint16_t> LlLowerer::build_repeat(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const auto bound = [](std::uint64_t v) {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, kEndless - 1));
  };
  const std::uint16_t k = co_await build(cfg, child_at(n, 0));
  co_return mark(cfg, _p.push(Node{Shape::rep, k, 0, bound(n.m),
                                   n.n == kUnbounded ? kEndless : bound(n.n)}));
}

Descent<std::uint16_t> LlLowerer::build_create(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const std::uint16_t inner = co_await build(cfg, child_at(n, 0));
  const std::uint16_t wrapper = wrap(cfg, inner);
  _p.e[wrapper].made.push_back(row_id(n.row));
  co_return wrapper;
}

Descent<std::uint16_t> LlLowerer::build_closed(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  const bool step = (n.flags & 1) != 0;
  const bool hands = (n.flags & 2) != 0;
  const ImageId inner = child_at(n, 0);
  const std::uint16_t child = co_await build(cfg, inner);
  const ImageStageDesc& st = stage(n.a);
  const bool data = st.kind == ImageRuleKind::data;
  if (data && at(inner).kind == ImageKind::closed && _p.e[child].layered &&
      _p.e[child].foldStep == step) {
    _p.e[child].made.push_back(row_id(st.convertRow));
    co_return child;
  }
  if (!data && !(hands && st.kind == ImageRuleKind::node)) co_return child;
  const std::uint16_t wrapper = wrap(cfg, child);
  Node& w = _p.e[wrapper];
  w.foldStep = step;
  w.layered = data;
  w.made.push_back(row_id(data ? st.convertRow : st.closeRow));
  co_return wrapper;
}

Descent<std::uint16_t> LlLowerer::build_write(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  std::uint16_t at_ = co_await build(cfg, child_at(n, 0));
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
  co_return at_;
}

Descent<std::uint16_t> LlLowerer::build_flag(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  std::uint16_t at_ = co_await build(cfg, child_at(n, 0));
  // The absent arm is a position of its own: its record stands before the
  // gap, as an implied token does.
  if (_p.e[at_].kind == Shape::rep && _p.e[at_].min == 0 &&
      _p.e[at_].max == 1) {
    const std::uint16_t item = _p.e[at_].sym;
    at_ = gather(cfg, Shape::alt, {item, _p.push(Node{Shape::none})});
  }
  _p.e[at_].row.push_back(row_id(n.row));
  co_return at_;
}

std::uint16_t LlLowerer::build_condition(unsigned cfg, ImageId e) {
  const std::uint16_t k = gap_cond(cfg, e);
  return mark(cfg, _p.push(Node{Shape::condition, k}));
}

Descent<std::uint16_t> LlLowerer::build_implied(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  check_implied_token(cfg, child_at(n, 0));
  const std::uint16_t sym = _p.e[co_await build(cfg, child_at(n, 0))].sym;
  const std::uint16_t k = gap_cond(cfg, child_at(n, 1));
  co_return mark(cfg, _p.push(Node{Shape::implied, sym, 0, k}));
}

Descent<std::uint16_t> LlLowerer::build(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::eof: {
      if (_cfgs[cfg].end == kNoImage)
        co_return mark(cfg, _p.push(Node{Shape::end}));
      const std::uint16_t close = co_await build(cfg, _cfgs[cfg].end);
      co_return mark(cfg, _p.push(Node{Shape::end, _p.e[close].sym}));
    }
    case ImageKind::skip:
      co_return co_await build(region_of(cfg, n), child_at(n, 0));
    case ImageKind::closed:
      co_return co_await build_closed(cfg, e);
    case ImageKind::turns: {
      const std::uint16_t node = co_await build(cfg, child_at(n, 0));
      _p.e[node].leftFold = true;
      co_return node;
    }
    case ImageKind::notOf:
      co_return build_condition(cfg, e);
    default:
      co_return co_await build_shared(cfg, e, n);
  }
}

}  // namespace cursiv::engine
