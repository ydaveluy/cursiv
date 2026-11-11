// A consuming, observation-free right-linear terminal is a regular positive
// closure: a noncyclic call to it stays a call, so the recognizer backend
// keeps it and no NFA expands it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

template <class Cfg>
class LexRightLinear {
 public:
  // `empty` and `productive` say, per node, whether it may read nothing and
  // whether it may read a text.
  LexRightLinear(Cfg& g, const std::vector<std::uint8_t>& empty,
                 const std::vector<std::uint8_t>& productive)
      : _g(g), _empty(empty), _productive(productive), _regular(g.nlex) {}

  void run() {
    std::vector<bool> visited(_g.nlex);
    for (std::uint16_t t = 1; t < _g.nterms; ++t) {
      const auto root = _g.termBuild[t].lex;
      if (root >= visited.size() || visited[root]) continue;
      visited[root] = true;
      std::vector<std::uint16_t> prefix;
      if (const LexNode e = _g.lex[root]; e.kind == LexKind::seq) {
        if (!tail(root, root, true, prefix)) continue;
      } else if (e.kind == LexKind::alt && e.b == 2) {
        const auto a = _g.lexChild[e.a], b = _g.lexChild[e.a + 1];
        if (!alternative(root, a, b, prefix) &&
            !alternative(root, b, a, prefix))
          continue;
      } else {
        continue;
      }
      if (!std::ranges::all_of(prefix,
                               [&](auto child) {
                                 return regular(child) && _productive[child];
                               }) ||
          std::ranges::all_of(prefix,
                              [&](auto child) { return _empty[child] != 0; }))
        continue;
      // A full graph keeps its original recognizer; an optimization must not
      // introduce a new size refusal or leave a partly rewritten prefix.
      if (const unsigned needed = prefix.size() == 1 ? 1 : 2;
          std::size_t{_g.nlex} + needed > kNoLexNode)
        continue;
      std::uint16_t one = prefix[0];
      if (prefix.size() != 1) {
        // The prefix is an existing contiguous row.
        const LexNode seq = _g.lex[_prefixSeq];
        one = _g.push_lex(LexNode{LexKind::seq, seq.a,
                                  static_cast<std::uint16_t>(seq.b - 1)});
      }
      const auto closure =
          _g.push_lex(LexNode{LexKind::rep, one, kNoLexNode, 1, kEndless});
      // An internal call: the root reads the closure, not the terminal.
      _g.lex[root] = LexNode{LexKind::call, closure, kNoLexNode};
    }
  }

 private:
  Cfg& _g;
  const std::vector<std::uint8_t>& _empty;
  const std::vector<std::uint8_t>& _productive;
  // Per node, whether it is regular: 0 not asked, 1 no, 2 yes.
  std::vector<std::uint8_t> _regular;
  std::uint16_t _prefixSeq = 0;

  // Whether `at` holds classes, sequences, choices and plain repetitions
  // alone: calls, captures, lookaheads, guards, searches, virtual tokens and
  // skipper regions are not proven.
  bool regular(std::uint16_t at) { return descend(regular_walk(at)); }
  Descent<bool> regular_walk(std::uint16_t at) {
    if (at >= _regular.size()) co_return false;
    if (_regular[at] != 0) co_return _regular[at] == 2;
    _regular[at] = 1;
    const LexNode e = _g.lex[at];
    bool yes = e.kind == LexKind::chars;
    if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
      yes = true;
      for (unsigned i = 0; i < e.b; ++i)
        if (!co_await regular_walk(_g.lexChild[e.a + i])) yes = false;
    } else if (e.kind == LexKind::rep && e.b == kNoLexNode) {
      // A phrase's repetition may have trivia between iterations. A
      // nullable repeated body must retain the original recursion refusal.
      yes = co_await regular_walk(e.a) && (e.max <= 1 || !_empty[e.a]);
    }
    if (yes) _regular[at] = 2;
    co_return yes;
  }

  bool tail(std::uint16_t at, std::uint16_t root, bool optional,
            std::vector<std::uint16_t>& prefix) {
    prefix.clear();
    const LexNode e = _g.lex[at];
    if (e.kind != LexKind::seq || e.b < 2) return false;
    auto last = _g.lexChild[e.a + e.b - 1];
    if (optional) {
      const LexNode opt = _g.lex[last];
      if (opt.kind != LexKind::rep || opt.min != 0 || opt.max != 1 ||
          opt.b != kNoLexNode)
        return false;
      last = opt.a;
    }
    if (const LexNode call = _g.lex[last];
        call.kind != LexKind::call || lex_callee(_g, call) != root)
      return false;
    for (unsigned i = 0; i + 1 < e.b; ++i)
      prefix.push_back(_g.lexChild[e.a + i]);
    _prefixSeq = at;
    return true;
  }


  bool same(std::uint16_t a, std::uint16_t b) const {
    return lex_same_graph(_g, a, b, [](const LexNode& x, const LexNode& y) {
      if (x.kind != y.kind) return false;
      if (x.kind == LexKind::chars) return x.a == y.a;
      if (x.kind == LexKind::rep) return x.min == y.min && x.max == y.max;
      return (x.kind == LexKind::seq || x.kind == LexKind::alt) && x.b == y.b;
    });
  }

  bool alternative(std::uint16_t root, std::uint16_t base,
                   std::uint16_t recursive,
                   std::vector<std::uint16_t>& prefix) {
    if (!tail(recursive, root, false, prefix)) return false;
    std::vector<std::uint16_t> x, y;
    lex_parts(_g, base, x);
    for (const auto child : prefix) lex_parts(_g, child, y);
    return x.size() == y.size() &&
           std::ranges::equal(x, y, [&](std::uint16_t p, std::uint16_t q) {
             return regular(p) && regular(q) && same(p, q);
           });
  }
};

}  // namespace cursiv::engine
