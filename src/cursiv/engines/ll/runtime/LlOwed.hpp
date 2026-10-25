// What a position still owes before its rule can end, read once per table off
// the graph: the tokens of the shortest way out. A repair that invents them
// one after the other has made one gesture, not as many as it has tokens.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <const auto& T>
class LlOwed {
 public:
  LlOwed() : _owes(T.atn.nstates, kNone) {
    for (bool moved = true; moved;) {
      moved = false;
      for (std::uint32_t s = 0; s < T.atn.nstates; ++s) {
        const LlAtnState& state = T.atn.state(s);
        unsigned least = state.kind == LlAtnKind::ruleStop ? 0 : kNone;
        for (std::uint32_t i = 0; i < state.nedges; ++i)
          least = std::min(least, way(T.atn.edge(state.edge + i)));
        if (least < _owes[s]) {
          _owes[s] = static_cast<std::uint16_t>(least);
          moved = true;
        }
      }
    }
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
  void due(const LlSimulation& sim, std::vector<std::uint16_t>& out) const {
    due(sim, out, [](std::uint32_t) { return false; });
  }
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
    thread_local std::vector<std::uint32_t> seen;
    thread_local std::uint32_t round = 0;
    if (seen.size() < _owes.size()) seen.assign(_owes.size(), 0u);
    if (++round == 0) {
      std::ranges::fill(seen, 0u);
      round = 1;
    }
    for (const LlConfig& x : sim.configs())
      if (pending(x) && _owes[x.state] == fewest)
        begin(x.state, out, seen, round);
  }

  // The shortest unfinished way names what an abandon left absent, including
  // a rule whose guarded entry has not read anything. It grants no free run.
  SymbolId first(const LlSimulation& sim) const {
    unsigned fewest = kNone;
    for (const LlConfig& x : sim.configs())
      if (_owes[x.state] != 0)
        fewest = std::min<unsigned>(fewest, _owes[x.state]);
    if (fewest == kNone) return kNoElement;
    std::vector<std::uint16_t> out;
    std::vector<std::uint32_t> seen(_owes.size(), 0u);
    for (const LlConfig& x : sim.configs())
      if (_owes[x.state] == fewest) begin(x.state, out, seen, 1u);
    return out.size() == 1 ? out.front() : kNoElement;
  }

  bool standing(std::uint32_t s) const { return _stands[s] != 0; }

 private:
  static constexpr unsigned kNone = 0xFFFFu;

  bool stands(std::uint32_t s) const {
    const LlAtnState& state = T.atn.state(s);
    if (state.kind == LlAtnKind::ruleStop) return true;
    for (std::uint32_t i = 0; i < state.nedges; ++i)
      if (T.atn.edge(state.edge + i).kind != LlEdgeKind::epsilon) return true;
    return false;
  }

  // The terminals that begin the shortest ways out of state `s`.
  void begin(std::uint32_t s, std::vector<std::uint16_t>& out,
             std::vector<std::uint32_t>& seen, std::uint32_t round) const {
    if (seen[s] == round) return;
    seen[s] = round;
    const LlAtnState& state = T.atn.state(s);
    for (std::uint32_t i = 0; i < state.nedges; ++i) {
      const LlAtnEdge& e = T.atn.edge(state.edge + i);
      if (way(e) != _owes[s]) continue;
      if (e.kind == LlEdgeKind::atom) {
        if (e.label != kEofSymbol && T.terms[e.label].laid != kLaidImplied &&
            std::ranges::find(out, e.label) == out.end())
          out.push_back(e.label);
      } else if (e.kind == LlEdgeKind::rule) {
        begin(T.atn.ruleStart[e.label], out, seen, round);
        if (_owes[T.atn.ruleStart[e.label]] == 0)
          begin(e.follow, out, seen, round);
      } else {
        begin(e.target, out, seen, round);
      }
    }
  }

  unsigned way(const LlAtnEdge& e) const {
    unsigned there = 0;
    if (e.kind == LlEdgeKind::atom)
      there = 1u + _owes[e.target];
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
