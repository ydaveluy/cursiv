// What a reader of a refused token needs off the automaton, derived once it
// has stopped moving: the states that loop in a body, the terminal each state
// is on the way to, and the bytes a mode opens such a body with.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LexOwedRows {
  std::vector<std::uint8_t> bodies;
  std::vector<std::uint16_t> owed;
  std::vector<ByteSet> openers;
  // Per state, the states with an edge into it.
  std::vector<std::vector<std::uint16_t>> into;
};

// `Dfa` names the automaton: `states`, `successors(s)`, `accept(s)` (the
// terminal plus one, zero where it accepts nothing), `relay(s)` (`kNoRelay`
// where it holds none), `hops(s)` (the state each assertion launched at `s`
// resumes in), `modes`, `start(m)`, `next(s, byte)`; and `before(a, b)`, which
// of two accepting states elects the terminal the lexer ranks first.
template <class Dfa>
LexOwedRows lex_owed_rows(const Dfa& d) {
  const std::size_t n = d.states();
  LexOwedRows rows;
  rows.into.resize(n);
  rows.bodies.assign(n, 0);
  rows.owed.assign(n, kInvalidSymbol);
  std::vector<std::uint16_t> accepting;
  for (std::size_t s = 0; s < n; ++s) {
    if (d.accept(static_cast<std::uint16_t>(s)) != 0) {
      rows.owed[s] = static_cast<std::uint16_t>(d.accept(s) - 1u);
      accepting.push_back(static_cast<std::uint16_t>(s));
    }
    for (const auto to : d.successors(static_cast<std::uint16_t>(s))) {
      rows.into[to].push_back(static_cast<std::uint16_t>(s));
      if (to == s && d.accept(static_cast<std::uint16_t>(s)) == 0)
        rows.bodies[s] = 1;
    }
  }
  std::vector<std::uint16_t> todo;
  std::ranges::sort(accepting, [&](std::uint16_t x, std::uint16_t y) {
    return d.before(x, y);
  });
  // Ranked seeds claim each predecessor once; acceptances stop a branch.
  for (const auto root : accepting) {
    todo.push_back(root);
    while (!todo.empty()) {
      const auto s = todo.back();
      todo.pop_back();
      for (const auto from : rows.into[s])
        if (rows.owed[from] == kInvalidSymbol) {
          rows.owed[from] = rows.owed[s];
          todo.push_back(from);
        }
    }
  }
  std::vector<std::uint8_t> reachesBody(n, 0);
  for (std::size_t s = 0; s < n; ++s)
    if (rows.bodies[s] != 0 ||
        d.relay(static_cast<std::uint16_t>(s)) != kNoRelay) {
      reachesBody[s] = 1;
      todo.push_back(static_cast<std::uint16_t>(s));
    }
  while (!todo.empty()) {
    const auto s = todo.back();
    todo.pop_back();
    for (const auto from : rows.into[s])
      if (reachesBody[from] == 0) {
        reachesBody[from] = 1;
        todo.push_back(from);
      }
  }
  rows.openers.resize(d.modes());
  std::vector<std::uint16_t> heads;
  for (std::size_t m = 0; m < rows.openers.size(); ++m) {
    // A zero-width assertion opens onto the rest of its token: the bytes that
    // begin the rest open the mode as well.
    heads.assign(1, d.start(static_cast<std::uint16_t>(m)));
    for (std::size_t i = 0; i < heads.size(); ++i)
      for (const std::uint16_t to : d.hops(heads[i]))
        if (std::ranges::find(heads, to) == heads.end()) heads.push_back(to);
    for (const auto head : heads)
      for (unsigned b = 0; b < 256; ++b) {
        const auto to = d.next(head, static_cast<unsigned char>(b));
        if (to != kDeadState && reachesBody[to] != 0)
          rows.openers[m].add(static_cast<unsigned char>(b));
      }
  }
  return rows;
}

// An engine's automaton `View` (`LexFirstByte.hpp`, with `relay_of(k)` and
// `tier(s, t)`, the tier of its acceptance of `t`) as `lex_owed_rows` reads
// it: `n` states, the successors of `s` from `succ[base[s]]` on.
template <class View>
struct LexOwedView {
  View v;
  std::size_t n;
  std::span<const std::uint32_t> base;
  std::span<const std::uint16_t> succ;

  std::size_t states() const { return n; }
  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    return succ.subspan(base[s], base[s + 1] - base[s]);
  }
  std::uint16_t accept(std::uint16_t s) const { return v.accept(s); }
  std::uint16_t relay(std::uint16_t s) const { return v.relay(s); }
  std::vector<std::uint16_t> hops(std::uint16_t s) const {
    std::vector<std::uint16_t> to;
    for (auto k = v.relay(s); k != kNoRelay; k = v.relay_of(k).next)
      if (v.relay_of(k).predicate != kNoLexNode)
        to.push_back(v.relay_of(k).resume);
    return to;
  }
  std::size_t modes() const { return v.modes(); }
  std::uint16_t start(std::uint16_t m) const { return v.start(m); }
  std::uint16_t next(std::uint16_t s, unsigned char b) const {
    return v.next(s, v.cls(b));
  }
  bool before(std::uint16_t x, std::uint16_t y) const {
    const unsigned tx = v.accept(x) - 1u, ty = v.accept(y) - 1u;
    return lex_term_before(v.term(tx), v.tier(x, tx), tx, v.term(ty),
                           v.tier(y, ty), ty);
  }
};

}  // namespace cursiv
