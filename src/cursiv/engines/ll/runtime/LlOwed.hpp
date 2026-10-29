// What a position still owes before its rule can end, read once per table off
// the graph: the tokens of the shortest way out. A repair that invents them
// one after the other has made one gesture, not as many as it has tokens.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <const auto& T>
class LlOwed {
 public:
  LlOwed() : _owes(T.atn.nstates, kNone) {
    settle_distances();
    // A rule not yet begun owes nothing: the positions its entry reaches
    // without reading are the ones it has not left.
    std::vector<std::uint8_t> entry(T.atn.nstates, 0);
    std::vector<std::uint32_t> todo;
    for (std::uint16_t r = 0; r < T.nrules; ++r) {
      todo.assign(1, T.atn.ruleStart[r]);
      entry[todo.back()] = 1;
      while (!todo.empty()) {
        const LlAtnState& state = T.atn.state(todo.back());
        todo.pop_back();
        for (std::uint32_t i = 0; i < state.nedges; ++i) {
          const LlAtnEdge& e = T.atn.edge(state.edge + i);
          if (e.kind == LlEdgeKind::epsilon && entry[e.target] == 0) {
            entry[e.target] = 1;
            todo.push_back(e.target);
          }
        }
      }
    }
    _stands.assign(T.atn.nstates, 0);
    for (std::uint32_t s = 0; s < T.atn.nstates; ++s)
      _stands[s] = stands(s) ? (entry[s] == 0 ? 1 : 2) : 0;
  }

  // A completed alternative owes nothing in that call. It rules out optional
  // continuations there, but not a closer owed by a different, enclosing call.
  template <class Begun>
  void due(const LlSimulation& sim, std::vector<std::uint16_t>& out,
           Begun&& begun) const {
    out.clear();
    const auto call = [](const LlConfig& x) {
      return (std::uint64_t{T.atn.state(x.state).rule} << 32) | x.ctx;
    };
    // An optional prefix may reach the same state with or without reading.
    // Its call's start, rather than that state's shape, decides which it did.
    const auto standing = [&](const LlConfig& x) {
      return _stands[x.state] == 1 ||
             (_stands[x.state] == 2 && begun(x.ctx));
    };
    thread_local std::vector<std::uint64_t> complete;
    complete.clear();
    for (const LlConfig& x : sim.configs())
      if (standing(x) && _owes[x.state] == 0)
        complete.push_back(call(x));
    std::ranges::sort(complete);
    const auto pending = [&](const LlConfig& x) {
      return standing(x) &&
             !std::ranges::binary_search(complete, call(x));
    };
    unsigned fewest = kNone;
    for (const LlConfig& x : sim.configs())
      if (pending(x)) fewest = std::min<unsigned>(fewest, _owes[x.state]);
    if (fewest == kNone) return;
    begin(sim, out, [&](const LlConfig& x) {
      return pending(x) && _owes[x.state] == fewest;
    });
  }

  // The shortest unfinished way names what an abandon left absent, including
  // a rule whose guarded entry has not read anything. It grants no free run.
  // Only one terminal is a name: the walk stops at the second.
  SymbolId first(const LlSimulation& sim) const {
    unsigned fewest = kNone;
    for (const LlConfig& x : sim.configs())
      if (_owes[x.state] != 0)
        fewest = std::min<unsigned>(fewest, _owes[x.state]);
    if (fewest == kNone) return kNoElement;
    std::vector<std::uint16_t> out;
    begin(sim, out, [&](const LlConfig& x) { return _owes[x.state] == fewest; },
          2);
    return out.size() == 1 ? out.front() : kNoElement;
  }

  bool standing(std::uint32_t s) const { return _stands[s] != 0; }

 private:
  static constexpr unsigned kNone = 0xFFFFu;

  void settle_distances() {
    // An edge depends on its target, or on both the called rule and its
    // continuation. Settle the cheapest positions first; a sum cannot be
    // cheaper than either of its non-negative operands.
    struct Into {
      std::uint32_t from, edge;
    };
    std::vector<std::uint32_t> base(T.atn.nstates + 1u, 0);
    const auto dependencies = [&](auto&& each) {
      for (std::uint32_t s = 0; s < T.atn.nstates; ++s) {
        const auto& state = T.atn.state(s);
        for (std::uint32_t i = 0; i < state.nedges; ++i) {
          const auto at = state.edge + i;
          const auto& e = T.atn.edge(at);
          each(e.kind == LlEdgeKind::rule ? T.atn.ruleStart[e.label] : e.target,
               Into{s, at});
          if (e.kind == LlEdgeKind::rule) each(e.follow, Into{s, at});
        }
      }
    };
    dependencies([&](std::uint32_t to, Into) { ++base[to + 1]; });
    for (std::uint32_t s = 0; s < T.atn.nstates; ++s) base[s + 1] += base[s];
    std::vector<Into> into(base.back());
    {
      auto next = base;
      dependencies(
          [&](std::uint32_t to, Into from) { into[next[to]++] = from; });
    }
    using Distance = std::pair<unsigned, std::uint32_t>;
    std::priority_queue<Distance, std::vector<Distance>, std::greater<>>
        pending;
    for (std::uint32_t s = 0; s < T.atn.nstates; ++s)
      if (T.atn.state(s).kind == LlAtnKind::ruleStop) {
        _owes[s] = 0;
        pending.emplace(0u, s);
      }
    while (!pending.empty()) {
      const auto [distance, s] = pending.top();
      pending.pop();
      if (distance != _owes[s]) continue;
      for (auto i = base[s]; i < base[s + 1]; ++i) {
        const Into from = into[i];
        const auto least = way(T.atn.edge(from.edge));
        if (least >= _owes[from.from]) continue;
        _owes[from.from] = static_cast<std::uint16_t>(least);
        pending.emplace(least, from.from);
      }
    }
  }

  bool stands(std::uint32_t s) const {
    const LlAtnState& state = T.atn.state(s);
    if (state.kind == LlAtnKind::ruleStop) return true;
    for (std::uint32_t i = 0; i < state.nedges; ++i)
      if (T.atn.edge(state.edge + i).kind != LlEdgeKind::epsilon) return true;
    return false;
  }

  // The terminals that begin the shortest ways out of the positions `from`
  // picks, in their order, until `out` holds `most` of them. A state is walked
  // once: a new round of marks clears the last one's.
  template <class From>
  void begin(const LlSimulation& sim, std::vector<std::uint16_t>& out,
             From&& from, std::size_t most = SIZE_MAX) const {
    thread_local std::vector<std::uint32_t> seen;
    thread_local std::uint32_t round = 0;
    if (seen.size() < _owes.size()) seen.assign(_owes.size(), 0u);
    if (++round == 0) {
      std::ranges::fill(seen, 0u);
      round = 1;
    }
    struct Frame {
      std::uint32_t state, next = 0;
    };
    std::vector<Frame> todo;
    for (auto x = sim.configs().rbegin(); x != sim.configs().rend(); ++x)
      if (from(*x)) todo.push_back({x->state});
    while (!todo.empty()) {
      Frame& f = todo.back();
      const auto& state = T.atn.state(f.state);
      if (f.next == 0) {
        if (seen[f.state] == round) {
          todo.pop_back();
          continue;
        }
        seen[f.state] = round;
      }
      if (f.next == state.nedges) {
        todo.pop_back();
        continue;
      }
      const auto& e = T.atn.edge(state.edge + f.next++);
      if (way(e) != _owes[f.state]) continue;
      if (e.kind == LlEdgeKind::atom) {
        if (e.label != kEofSymbol && T.terms[e.label].laid != kLaidImplied &&
            std::ranges::find(out, e.label) == out.end()) {
          out.push_back(e.label);
          if (out.size() == most) return;
        }
      } else if (e.kind == LlEdgeKind::rule) {
        if (_owes[T.atn.ruleStart[e.label]] == 0) todo.push_back({e.follow});
        todo.push_back({T.atn.ruleStart[e.label]});
      } else {
        todo.push_back({e.target});
      }
    }
  }

  // The end of the text is no token a repair writes: a way out at it owes
  // nothing more than the rule's own end does.
  unsigned way(const LlAtnEdge& e) const {
    unsigned there = 0;
    if (e.kind == LlEdgeKind::atom)
      there = (e.label == kEofSymbol ? 0u : 1u) + _owes[e.target];
    else if (e.kind == LlEdgeKind::rule)
      there = _owes[T.atn.ruleStart[e.label]] + _owes[e.follow];
    else
      there = _owes[e.target];
    return std::min(there, kNone);
  }

  std::vector<std::uint16_t> _owes;
  // Per state: 0 a route, 1 inside its rule, 2 also reachable at its entry.
  std::vector<std::uint8_t> _stands;
};

template <const auto& T>
inline const LlOwed<T>& ll_owed() {
  static const LlOwed<T> owed;
  return owed;
}

}  // namespace cursiv
