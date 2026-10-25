// Recursive lexical calls resolve after all terminal roots exist. Their
// zero-progress graph must be acyclic, independently of ordinary recursion.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexRightLinear.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/StrongComponents.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

namespace detail {

template <class Cfg, class Visit>
void lex_children(const Cfg& g, std::uint16_t at, Visit&& visit,
                  bool calls = true) {
  const LexNode e = g.lex[at];
  switch (e.kind) {
    case LexKind::seq:
    case LexKind::alt:
    case LexKind::skip:
      for (unsigned i = 0; i < e.b; ++i) visit(g.lexKid[e.a + i]);
      break;
    case LexKind::call:
      if (calls) visit(e.a);
      break;
    case LexKind::rep:
      if (e.max != 0) visit(e.a);
      break;
    case LexKind::positive:
    case LexKind::negative:
    case LexKind::mark:
    case LexKind::through_lit:
    case LexKind::gap_has:
    case LexKind::gap_is:
      visit(e.a);
      break;
    case LexKind::through_same:
    case LexKind::through_nested:
      visit(e.a);
      for (unsigned i = 0; i < (e.kind == LexKind::through_nested ? 2u : e.min);
           ++i)
        if (g.lexKid[e.b + i] != kNoLexNode) visit(g.lexKid[e.b + i]);
      break;
    case LexKind::token:
      visit(e.b);
      break;
    default:
      break;
  }
}

template <class Cfg, class Facts>
bool lex_derives(const Cfg& g, std::uint16_t at, Facts&& facts,
                 bool productive) {
  const LexNode e = g.lex[at];
  switch (e.kind) {
    case LexKind::chars:
      return productive && g.sets[e.a].any();
    case LexKind::seq:
      for (unsigned i = 0; i < e.b; ++i)
        if (!facts(g.lexKid[e.a + i])) return false;
      return true;
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (facts(g.lexKid[e.a + i])) return true;
      return false;
    case LexKind::call:
    case LexKind::mark:
    case LexKind::through_nested:
      return facts(e.a);
    case LexKind::through_lit:
      return facts(e.a) && (productive || e.max != 0);
    case LexKind::through_same:
      return productive && facts(e.a);
    case LexKind::token:
      return facts(e.b);
    case LexKind::rep:
      return e.min == 0 || facts(e.a);
    case LexKind::indent:
    case LexKind::dedent:
    case LexKind::implied:
      return productive;
    default:
      return true;
  }
}

}  // namespace detail

template <class Cfg>
void normalize_lex_calls(Cfg& g) {
  for (std::uint16_t at = 0; at < g.nlex; ++at) {
    LexNode& e = g.lex[at];
    // Internal calls to normalized regular closures already name their node.
    if (e.kind == LexKind::call && e.b != kNoLexNode)
      e.a = g.termBuild[e.b].lex;
  }
}

template <class Cfg>
std::vector<std::uint8_t> lex_call_ancestors(const Cfg& g) {
  std::vector<std::uint16_t> todo;
  for (std::uint16_t at = 0; at < g.nlex; ++at)
    if (g.lex[at].kind == LexKind::call) todo.push_back(at);
  if (todo.empty()) return {};
  std::vector<std::uint8_t> calls(g.nlex);
  std::vector<std::vector<std::uint16_t>> parents(g.nlex);
  for (const auto at : todo) calls[at] = 1;
  for (std::uint16_t at = 0; at < g.nlex; ++at)
    detail::lex_children(g, at, [&](auto kid) { parents[kid].push_back(at); });
  while (!todo.empty()) {
    const auto at = todo.back();
    todo.pop_back();
    for (const auto parent : parents[at])
      if (!calls[parent]) {
        calls[parent] = 1;
        todo.push_back(parent);
      }
  }
  return calls;
}

namespace detail {

template <class Cfg>
bool lex_may_be_empty(const Cfg& g, std::uint16_t at,
                      std::unordered_map<std::uint16_t, bool>& known) {
  if (const auto it = known.find(at); it != known.end()) return it->second;
  const bool empty = lex_derives(
      g, at, [&](auto k) { return lex_may_be_empty(g, k, known); }, false);
  known.emplace(at, empty);
  return empty;
}

}  // namespace detail

// Whether a lexical node may match nothing, for a graph with no cycle.
template <class Cfg>
bool lex_may_be_empty(const Cfg& g, std::uint16_t at) {
  std::unordered_map<std::uint16_t, bool> known;
  return detail::lex_may_be_empty(g, at, known);
}

// The lexical nodes that may match nothing.
template <class Cfg>
std::vector<std::uint8_t> lex_empty_nodes(const Cfg& g) {
  std::vector<std::uint8_t> empty(g.nlex);
  detail::settle_empty([&](auto&& set) {
    for (std::uint16_t at = 0; at < g.nlex; ++at)
      set(empty[at], detail::lex_derives(
                         g, at, [&](auto k) { return empty[k] != 0; }, false));
  });
  return empty;
}

template <class Cfg>
void validate_lex_recursion(Cfg& g) {
  if (g.lowerFail()) return;
  const auto calls = lex_call_ancestors(g);
  if (calls.empty()) return;

  // A refusal names the terminal whose body holds the node, not the first
  // caller to reach it; an internal call's body falls to its caller.
  std::vector<std::uint16_t> body(g.nlex), owner(g.nlex);
  std::vector<std::uint16_t> todo;
  const auto reach = [&](std::vector<std::uint16_t>& to, std::uint16_t t,
                         bool viaCalls) {
    todo.push_back(g.termBuild[t].lex);
    while (!todo.empty()) {
      const auto at = todo.back();
      todo.pop_back();
      if (to[at] != 0) continue;
      to[at] = viaCalls && body[at] != 0 ? body[at] : t;
      detail::lex_children(
          g, at, [&](auto kid) { todo.push_back(kid); }, viaCalls);
    }
  };
  for (std::uint16_t t = 1; t < g.nterms; ++t) reach(body, t, false);
  for (std::uint16_t t = 1; t < g.nterms; ++t)
    if (calls[g.termBuild[t].lex]) reach(owner, t, true);
  std::vector<std::uint8_t> empty(g.nlex), productive(g.nlex);
  const auto settle = [&](auto& facts, bool any_text) {
    detail::settle_empty([&](auto&& set) {
      for (std::uint16_t at = 0; at < g.nlex; ++at)
        if (owner[at] != 0)
          set(facts[at],
              detail::lex_derives(
                  g, at, [&](auto k) { return facts[k] != 0; }, any_text));
    });
  };
  settle(empty, false);
  settle(productive, true);

  std::vector<std::uint32_t> starts{0}, edges;
  for (std::uint16_t at = 0; at < g.nlex; ++at) {
    if (owner[at] != 0) {
      const LexNode e = g.lex[at];
      if (e.kind == LexKind::seq) {
        for (unsigned i = 0; i < e.b; ++i) {
          const auto kid = g.lexKid[e.a + i];
          edges.push_back(kid);
          if (!empty[kid]) break;
        }
      } else {
        detail::lex_children(g, at, [&](auto kid) { edges.push_back(kid); });
      }
    }
    starts.push_back(static_cast<std::uint32_t>(edges.size()));
  }
  const auto components = StrongComponents<>::build(starts, edges);
  for (std::uint16_t at = 0; at < g.nlex; ++at) {
    if (owner[at] == 0) continue;
    const LexNode e = g.lex[at];
    if (components.cyclic[at]) {
      lower_refuse(g, LowerFail::terminalCycle, owner[at]);
      return;
    }
    if (e.kind == LexKind::call && !productive[at]) {
      lower_refuse(g, LowerFail::terminalUnproductive, owner[at]);
      return;
    }
    if (e.kind == LexKind::rep && e.max > 1 &&
        empty[e.a]) {
      lower_refuse(g, LowerFail::terminalNullableRepetition, owner[at]);
      return;
    }
  }
  for (std::uint16_t t = 1; t < g.nterms; ++t)
    if (!g.termBuild[t].predicateOnly && owner[g.termBuild[t].lex] != 0 &&
        empty[g.termBuild[t].lex]) {
      lower_refuse(g, LowerFail::terminalEmpty, t);
      return;
    }
  detail::LexRightLinear<Cfg>{g}.run();
}

template <class Cfg>
std::pair<ByteSet, bool> lex_first_bytes(const Cfg& g, std::uint16_t node) {
  const LexNode& e = g.lex[node];
  switch (e.kind) {
    case LexKind::chars:
      return {g.sets[e.a] | lex_decode_bytes(g, e.a), false};
    case LexKind::call:
    case LexKind::mark:
      return lex_first_bytes(g, e.a);
    case LexKind::seq:
    case LexKind::alt: {
      const bool sequence = e.kind == LexKind::seq;
      ByteSet bytes;
      bool empty = sequence;
      for (unsigned i = 0; i < e.b; ++i) {
        const auto [part, nullable] = lex_first_bytes(g, g.lexKid[e.a + i]);
        bytes |= part;
        if (sequence && !nullable) return {bytes, false};
        empty = empty || nullable;
      }
      return {bytes, empty};
    }
    case LexKind::rep: {
      if (e.max == 0) return {{}, true};
      const auto [bytes, empty] = lex_first_bytes(g, e.a);
      return {bytes, empty || e.min == 0};
    }
    case LexKind::through_lit:
    case LexKind::through_same:
    case LexKind::through_nested: {
      const auto [bytes, empty] = lex_first_bytes(g, e.a);
      if (!empty) return {bytes, false};
      const bool nullable = e.kind == LexKind::through_nested ||
                            (e.kind == LexKind::through_lit && e.max != 0);
      return {kAllBytes, nullable};
    }
    case LexKind::same:
      return {kAllBytes, true};
    default:
      return {{}, true};
  }
}

}  // namespace cursiv
