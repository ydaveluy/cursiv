// The walk over sets of configurations reads the automaton by its view: one
// compilation serves every language.
#include "cursiv/ll/table/LlSimulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "cursiv/ll/table/LlAtnView.hpp"

namespace cursiv::engine {

LlSimulation::LlSimulation(const LlAtnView& atn, std::uint32_t closureLimit)
    : _atn(atn), _limit(closureLimit) {}

LlSimulation::~LlSimulation() = default;

bool LlSimulation::leave(std::uint32_t depth, std::uint32_t ret) {
  _next.clear();
  forget_visits();
  for (const LlConfig& c : _cur) {
    std::vector<std::uint32_t> level{c.ctx}, next;
    for (std::uint32_t d = 1; d <= depth; ++d) {
      next.clear();
      for (const std::uint32_t ctx : level) {
        if (ctx == kLlCtxNone) continue;
        for (unsigned j = 0; j < _ctx.count(ctx); ++j) {
          const std::uint32_t parent = _ctx.parent(ctx, j);
          if (d == depth) {
            const std::uint32_t to = _ctx.ret(ctx, j);
            if (_atn.same_return(to, ret))
              closure(LlConfig{to, parent, c.alt}, _next);
          } else if (std::ranges::find(next, parent) == next.end()) {
            next.push_back(parent);
          }
        }
      }
      level.swap(next);
    }
  }
  _cur.swap(_next);
  std::sort(_cur.begin(), _cur.end());
  return !_cur.empty();
}

void LlSimulation::start(std::uint32_t decisionState, std::uint32_t stack) {
  reset();
  const LlAtnState& s = _atn.state(decisionState);
  for (std::uint32_t i = 0; i < s.nedges; ++i)
    feed(LlConfig{.state = _atn.edge(s.edge + i).target,
                  .ctx = stack,
                  .alt = static_cast<std::uint16_t>(i)});
  expand_sources();
  _cur.swap(_next);
}

bool LlSimulation::step(const std::uint16_t* terms, std::size_t n) {
  const auto walk = [&] {
    return advance(Labelled{terms, n, labels(terms, n)});
  };
  return _keepSteps ? kept_step(terms, n, walk) : walk();
}

const std::vector<std::uint64_t>* LlSimulation::labels(
    const std::uint16_t* terms, std::size_t n) {
  if (n <= kLinearLabels) return nullptr;
  _labels.assign(*std::max_element(terms, terms + n) / 64u + 1u, 0u);
  for (std::size_t m = 0; m < n; ++m)
    _labels[terms[m] >> 6] |= std::uint64_t{1} << (terms[m] & 63u);
  return &_labels;
}

void LlSimulation::expand_sources() {
  if (!_shareEpsilon) return;
  _split = false;
  _visits = 0;
  _stopAtSplit = true;
  drain(_next);
  _stopAtSplit = false;
  // The first walk stops where the stacks part: the one-at-a-time walk wins
  // when it ends within a few times what the first had read by then, and
  // the first is read to its end only when it does not.
  if (_split && !one_at_a_time(kSerialRatio * _visits + kSerialRoom)) {
    _split = false;
    _visits = 0;
    _next.clear();
    forget_visits();
    for (const LlConfig& c : _sources) _pending.push_back({c, kNoFollow});
    drain(_next);
    std::vector<LlConfig> joined = std::move(_next);
    if (!one_at_a_time(kSerialRatio * _visits + kSerialRoom))
      _next = std::move(joined);
  }
  forget_deltas();
  _sources.clear();
  // One order for one set, so the step kept for it is found again.
  std::sort(_next.begin(), _next.end());
}

bool LlSimulation::one_at_a_time(std::uint64_t budget) {
  _budget = budget;
  _next.clear();
  forget_visits();
  _visits = 0;
  bool done = true;
  for (const LlConfig& c : _sources)
    if (!(done = closure(c, _next))) break;
  _budget = 0;
  return done;
}

bool LlSimulation::drain(std::vector<LlConfig>& out,
                         std::vector<LlConfig>* conditions) {
  for (;;) {
    while (!_pending.empty() && !_capped && !(_stopAtSplit && _split)) {
      _stop.poll();
      Pending next = _pending.back();
      _pending.pop_back();
      if (next.follow != kNoFollow)
        next.config.ctx = _ctx.push(next.config.ctx, next.follow, _callFrom);
      visit(next.config, out, conditions);
      if (_immediate && _budget != 0 && _visits > _budget) return false;
    }
    if (_ready.empty() || _capped || (_stopAtSplit && _split)) return true;
    std::pop_heap(_ready.begin(), _ready.end(), std::greater<>{});
    const auto at = static_cast<std::uint32_t>(_ready.back());
    _ready.pop_back();
    const LlConfig held{
        .state = out[at].state, .ctx = _delta[at], .alt = out[at].alt};
    _delta[at] = kLlCtxApart;
    expand(held);
  }
}

void LlSimulation::rank_states() {
  const std::uint32_t n = _atn.nstates;
  _rank.assign(n, 0);
  std::vector<std::uint8_t> seen(n, 0);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> path;
  std::uint32_t post = 0;
  for (std::uint32_t root = 0; root < n; ++root) {
    if (seen[root]) continue;
    seen[root] = 1;
    path.emplace_back(root, 0);
    while (!path.empty()) {
      const auto [state, from] = path.back();
      const LlAtnState& q = _atn.state(state);
      std::uint32_t next = kNoFollow, at = from;
      if (q.kind == LlAtnKind::ruleStop) {
        const std::uint32_t base = _atn.callerBase[q.rule];
        if (base + at < _atn.callerBase[q.rule + 1])
          next = _atn.callerAt[base + at++];
      } else {
        while (at < q.nedges && next == kNoFollow) {
          const LlAtnEdge& e = _atn.edge(q.edge + at++);
          if (e.kind == LlEdgeKind::epsilon || e.kind == LlEdgeKind::rule)
            next = e.target;
        }
      }
      path.back().second = at;
      if (next == kNoFollow) {
        _rank[state] = n - 1 - post++;
        path.pop_back();
      } else if (!seen[next]) {
        seen[next] = 1;
        path.emplace_back(next, 0);
      }
    }
  }
}

void LlSimulation::defer(std::uint32_t at, const LlConfig& cur,
                         std::size_t entries) {
  if (_delta.size() < entries) _delta.resize(entries, kLlCtxApart);
  std::uint32_t& held = _delta[at];
  if (held == kLlCtxApart) {
    held = cur.ctx;
    _ready.push_back(std::uint64_t{_rank[cur.state]} << 32 | at);
    std::push_heap(_ready.begin(), _ready.end(), std::greater<>{});
    return;
  }
  const std::uint32_t joined = _ctx.merge(held, cur.ctx);
  if (joined == kLlCtxApart) {
    _split = true;
    expand(cur);
  } else {
    held = joined;
  }
}

void LlSimulation::visit(const LlConfig& cur, std::vector<LlConfig>& out,
                         std::vector<LlConfig>* conditions) {
  ++_visits;
  ++_total;
  const LlAtnState& s = _atn.state(cur.state);
  bool ready = s.kind == LlAtnKind::ruleStop && cur.ctx == kLlCtxNone;
  bool conditional = false;
  if (!_shareEpsilon || conditions != nullptr)
    for (std::uint32_t i = 0; i < s.nedges; ++i) {
      const auto kind = _atn.edge(s.edge + i).kind;
      conditional = conditional || kind == LlEdgeKind::condition;
      ready =
          ready || kind == LlEdgeKind::atom || kind == LlEdgeKind::condition;
    }
  bool first = false, joined = false;
  std::uint32_t* latest = nullptr;
  std::uint32_t where = 0;
  const bool shared = ready || _shareEpsilon;
  if (shared) {
    const std::uint64_t key =
        (static_cast<std::uint64_t>(cur.state) << 16) | cur.alt;
    latest = &_at.at(key, first);
    if (!first) {
      unsigned tried = 0;
      for (std::uint32_t at = *latest;
           at != kNoFollow && tried++ < kEntriesTried;
           at = _shareEpsilon ? _nextAtPosition[at] : kNoFollow) {
        const std::uint32_t m = _ctx.merge(out[at].ctx, cur.ctx);
        if (m == kLlCtxApart) {
          _split =
              _split || (out[at].ctx != kLlCtxNone && cur.ctx != kLlCtxNone);
          continue;
        }
        if (m == out[at].ctx) return;
        out[at].ctx = m;
        // Publish the union, traverse only the new contexts. Only repair
        // weighing joins at epsilon positions: elsewhere joining stacks of
        // different depth is quadratic.
        joined = true;
        where = at;
        break;
      }
    }
  }
  // A first shared state is necessarily new. A context union that widened
  // its published state necessarily contributes new returns. Only the
  // unmergeable arrivals and unshared bookkeeping need a second set.
  if (!shared || (!first && !joined)) {
    bool fresh = false;
    _seen.at(cur, fresh);
    if (!fresh) return;
  }
  if (_limit != 0 && ++_visited > _limit) {
    _capped = true;
    return;
  }
  if (!shared) {
    out.push_back(cur);
  } else if (first || !joined) {
    const auto at = static_cast<std::uint32_t>(out.size());
    where = at;
    if (_shareEpsilon) {
      _nextAtPosition.resize(out.size() + 1, kNoFollow);
      _nextAtPosition[at] = first ? kNoFollow : *latest;
    }
    if (first || _shareEpsilon) *latest = at;
    out.push_back(cur);
  }
  if (conditions != nullptr && conditional) conditions->push_back(cur);
  if (_shareEpsilon && !_immediate)
    defer(where, cur, out.size());
  else
    expand(cur);
}

void LlSimulation::expand(const LlConfig& cur) {
  const LlAtnState& s = _atn.state(cur.state);
  if (s.kind == LlAtnKind::ruleStop) {
    if (cur.ctx != kLlCtxNone) {
      // Each return a reduction can take is a stack of its own.
      for (unsigned k = _ctx.count(cur.ctx); k-- > 0;)
        _pending.push_back({LlConfig{.state = _ctx.ret(cur.ctx, k),
                                     .ctx = _ctx.parent(cur.ctx, k),
                                     .alt = cur.alt},
                            kNoFollow});
    } else {
      const std::uint32_t from = _atn.callerBase[s.rule];
      _unbound =
          _unbound || (_truncated && _atn.callerBase[s.rule + 1] != from);
      for (std::uint32_t i = _atn.callerBase[s.rule + 1]; i-- > from;)
        _pending.push_back(
            {LlConfig{
                 .state = _atn.callerAt[i], .ctx = kLlCtxNone, .alt = cur.alt},
             kNoFollow});
    }
    return;
  }
  for (std::uint32_t i = s.nedges; i-- > 0;) {
    const LlAtnEdge& e = _atn.edge(s.edge + i);
    if (e.kind == LlEdgeKind::epsilon)
      _pending.push_back(
          {LlConfig{.state = e.target, .ctx = cur.ctx, .alt = cur.alt},
           kNoFollow});
    else if (e.kind == LlEdgeKind::rule)
      _pending.push_back(
          {LlConfig{.state = e.target, .ctx = cur.ctx, .alt = cur.alt},
           e.follow});
  }
}

}  // namespace cursiv::engine
