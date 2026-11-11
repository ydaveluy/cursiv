// Recursive lexical calls resolve after all terminal roots exist. Their
// zero-progress graph must be acyclic, independently of ordinary recursion.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LexRightLinear.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/utils/StrongComponents.hpp"

namespace cursiv::engine {

template <class Cfg, class Visit>
void lex_children(const Cfg& g, std::uint16_t at, Visit&& visit,
                  bool calls = true) {
  const LexNode e = g.lex[at];
  switch (e.kind) {
    case LexKind::seq:
    case LexKind::alt:
    case LexKind::skip:
      for (unsigned i = 0; i < e.b; ++i) visit(g.lexChild[e.a + i]);
      break;
    case LexKind::call:
      if (calls) visit(lex_callee(g, e));
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
        if (g.lexChild[e.b + i] != kNoLexNode) visit(g.lexChild[e.b + i]);
      break;
    case LexKind::token:
      visit(lex_token_body(g, e));
      break;
    default:
      break;
  }
}

// Walks from `root` depth first, each node once: `enter(at, push)` answers
// false for a node already seen, else pushes its children in reading order;
// `leave(at)` runs once they are done.
template <class Enter, class Leave>
void lex_post_order(std::uint16_t root, Enter&& enter, Leave&& leave) {
  std::vector<std::pair<std::uint16_t, bool>> todo{{root, false}};
  const auto push = [&todo](std::uint16_t child) {
    todo.emplace_back(child, false);
  };
  while (!todo.empty()) {
    const auto [at, done] = todo.back();
    todo.pop_back();
    if (done) {
      leave(at);
      continue;
    }
    todo.emplace_back(at, true);
    const std::size_t from = todo.size();
    if (!enter(at, push)) {
      todo.pop_back();
      continue;
    }
    std::reverse(todo.begin() + static_cast<std::ptrdiff_t>(from), todo.end());
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
        if (!facts(g.lexChild[e.a + i])) return false;
      return true;
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (facts(g.lexChild[e.a + i])) return true;
      return false;
    case LexKind::call:
      return facts(lex_callee(g, e));
    case LexKind::mark:
    case LexKind::through_nested:
      return facts(e.a);
    case LexKind::through_lit:
      return facts(e.a) && (productive || e.max != 0);
    case LexKind::through_same:
      return productive && facts(e.a);
    case LexKind::token:
      return facts(lex_token_body(g, e));
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
    lex_children(g, at, [&](auto child) { parents[child].push_back(at); });
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

// Per node, whether it may read nothing; with `productive`, whether it may
// read a text at all.
template <class Cfg>
std::vector<std::uint8_t> lex_empty_nodes(const Cfg& g,
                                          bool productive = false) {
  std::vector<std::uint8_t> facts(g.nlex);
  const auto known = [&](auto k) { return facts[k] != 0; };
  least_fixpoint([&](auto&& set) {
    for (std::uint16_t at = 0; at < g.nlex; ++at)
      set(facts[at], lex_derives(g, at, known, productive));
  });
  return facts;
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
      lex_children(g, at, [&](auto child) { todo.push_back(child); }, viaCalls);
    }
  };
  for (std::uint16_t t = 1; t < g.nterms; ++t) reach(body, t, false);
  for (std::uint16_t t = 1; t < g.nterms; ++t)
    if (calls[g.termBuild[t].lex]) reach(owner, t, true);
  const auto empty = lex_empty_nodes(g), productive = lex_empty_nodes(g, true);

  std::vector<std::uint32_t> starts{0}, edges;
  for (std::uint16_t at = 0; at < g.nlex; ++at) {
    if (owner[at] != 0) {
      const LexNode e = g.lex[at];
      if (e.kind == LexKind::seq) {
        for (unsigned i = 0; i < e.b; ++i) {
          const auto child = g.lexChild[e.a + i];
          edges.push_back(child);
          if (!empty[child]) break;
        }
      } else {
        lex_children(g, at, [&](auto child) { edges.push_back(child); });
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
  LexRightLinear<Cfg>{g, empty, productive}.run();
}

// The bytes `node` may open on, and whether it may read nothing.
template <class Cfg>
Descent<std::pair<ByteSet, bool>> lex_first_bytes_walk(const Cfg& g,
                                                       std::uint16_t node) {
  const LexNode& e = g.lex[node];
  switch (e.kind) {
    case LexKind::chars:
      co_return std::pair{g.sets[e.a] | lex_decode_bytes(g, e.a), false};
    case LexKind::call:
      co_return co_await lex_first_bytes_walk(g, lex_callee(g, e));
    case LexKind::mark:
      co_return co_await lex_first_bytes_walk(g, e.a);
    case LexKind::seq:
    case LexKind::alt: {
      const bool sequence = e.kind == LexKind::seq;
      ByteSet bytes;
      bool empty = sequence;
      for (unsigned i = 0; i < e.b; ++i) {
        const auto [part, nullable] =
            co_await lex_first_bytes_walk(g, g.lexChild[e.a + i]);
        bytes |= part;
        if (sequence && !nullable) co_return std::pair{bytes, false};
        empty = empty || nullable;
      }
      co_return std::pair{bytes, empty};
    }
    case LexKind::rep: {
      if (e.max == 0) co_return std::pair{ByteSet{}, true};
      const auto [bytes, empty] = co_await lex_first_bytes_walk(g, e.a);
      co_return std::pair{bytes, empty || e.min == 0};
    }
    case LexKind::through_lit:
    case LexKind::through_same:
    case LexKind::through_nested: {
      const auto [bytes, empty] = co_await lex_first_bytes_walk(g, e.a);
      if (!empty) co_return std::pair{bytes, false};
      const bool nullable = e.kind == LexKind::through_nested ||
                            (e.kind == LexKind::through_lit && e.max != 0);
      co_return std::pair{kAllBytes, nullable};
    }
    case LexKind::same:
      co_return std::pair{kAllBytes, true};
    default:
      co_return std::pair{ByteSet{}, true};
  }
}

template <class Cfg>
std::pair<ByteSet, bool> lex_first_bytes(const Cfg& g, std::uint16_t node) {
  return descend(lex_first_bytes_walk(g, node));
}

}  // namespace cursiv::engine
