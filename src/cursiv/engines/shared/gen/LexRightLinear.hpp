// A consuming, observation-free right-linear terminal is a regular positive
// closure. Keep a noncyclic call to the closure, preserving the recognizer
// backend: an equivalent NFA may expand counted repeats or need an exponential
// DFA. Other calls keep their scope and every endpoint remains available.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv::detail {

template <class Cfg>
class LexRightLinear {
 public:
  explicit LexRightLinear(Cfg& g) : _g(g), _facts(g.nlex) {}

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
        const auto a = _g.lexKid[e.a], b = _g.lexKid[e.a + 1];
        if (!alternative(root, a, b, prefix) &&
            !alternative(root, b, a, prefix))
          continue;
      } else {
        continue;
      }
      if (!std::ranges::all_of(prefix,
                               [&](auto kid) {
                                 const auto f = facts(kid);
                                 return f.regular && f.productive;
                               }) ||
          std::ranges::all_of(prefix,
                              [&](auto kid) { return facts(kid).nullable; }))
        continue;
      // A full graph keeps its original recognizer; an optimization must not
      // introduce a new size refusal or leave a partly rewritten prefix.
      if (const unsigned needed = prefix.size() == 1 ? 1 : 2;
          std::size_t{_g.nlex} + needed > kNoLexNode)
        continue;
      std::uint16_t one;
      if (prefix.size() == 1) {
        one = prefix[0];
      } else {
        // The prefix is an existing contiguous row.
        const LexNode seq = _g.lex[_prefixSeq];
        one = _g.nlex;
        _g.lex[_g.nlex++] =
            LexNode{LexKind::seq, seq.a, static_cast<std::uint16_t>(seq.b - 1)};
      }
      const auto closure = _g.nlex;
      _g.lex[_g.nlex++] = LexNode{LexKind::rep, one, kNoLexNode, 1, kEndless};
      // An internal call: the root reads the closure, not the terminal.
      _g.lex[root] = LexNode{LexKind::call, closure, kNoLexNode};
    }
  }

 private:
  struct Facts {
    bool regular = false;
    bool productive = false;
    bool nullable = false;
    std::uint8_t state = 0;
  };

  Cfg& _g;
  std::vector<Facts> _facts;
  std::uint16_t _prefixSeq = 0;

  Facts facts(std::uint16_t at) {
    if (at >= _facts.size()) return {};
    if (_facts[at].state == 2) return _facts[at];
    if (_facts[at].state == 1) return {};
    _facts[at].state = 1;
    const LexNode e = _g.lex[at];
    Facts f;
    switch (e.kind) {
      case LexKind::chars:
        f = {true, _g.sets[e.a].any(), false};
        break;
      case LexKind::seq:
      case LexKind::alt: {
        const bool seq = e.kind == LexKind::seq;
        f = {true, seq, seq};
        for (unsigned i = 0; i < e.b; ++i) {
          const auto kid = facts(_g.lexKid[e.a + i]);
          f.regular = f.regular && kid.regular;
          f.productive = seq ? f.productive && kid.productive
                             : f.productive || kid.productive;
          f.nullable =
              seq ? f.nullable && kid.nullable : f.nullable || kid.nullable;
        }
        break;
      }
      case LexKind::rep: {
        const auto kid = facts(e.a);
        // A phrase's repetition may have trivia between iterations. A
        // nullable repeated body must retain the original recursion refusal.
        f.regular =
            kid.regular && e.b == kNoLexNode && (e.max <= 1 || !kid.nullable);
        f.productive = e.min == 0 || kid.productive;
        f.nullable = e.min == 0 || kid.nullable;
        break;
      }
      default:
        // Calls, captures, lookaheads, word/end guards, searches, virtual
        // tokens and skipper regions are not proven.
        break;
    }
    f.state = 2;
    _facts[at] = f;
    return f;
  }

  bool tail(std::uint16_t at, std::uint16_t root, bool optional,
            std::vector<std::uint16_t>& prefix) {
    prefix.clear();
    const LexNode e = _g.lex[at];
    if (e.kind != LexKind::seq || e.b < 2) return false;
    auto last = _g.lexKid[e.a + e.b - 1];
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
    for (unsigned i = 0; i + 1 < e.b; ++i) prefix.push_back(_g.lexKid[e.a + i]);
    _prefixSeq = at;
    return true;
  }

  void sequence(std::uint16_t at, std::vector<std::uint16_t>& out) const {
    const LexNode e = _g.lex[at];
    if (e.kind == LexKind::seq)
      for (unsigned i = 0; i < e.b; ++i) sequence(_g.lexKid[e.a + i], out);
    else
      out.push_back(at);
  }

  bool same(std::uint16_t a, std::uint16_t b) const {
    if (a == b) return true;
    const LexNode x = _g.lex[a], y = _g.lex[b];
    if (x.kind != y.kind) return false;
    if (x.kind == LexKind::chars) return x.a == y.a;
    if (x.kind == LexKind::rep)
      return x.min == y.min && x.max == y.max && same(x.a, y.a);
    if (x.kind != LexKind::seq && x.kind != LexKind::alt) return false;
    if (x.b != y.b) return false;
    for (unsigned i = 0; i < x.b; ++i)
      if (!same(_g.lexKid[x.a + i], _g.lexKid[y.a + i])) return false;
    return true;
  }

  bool alternative(std::uint16_t root, std::uint16_t base,
                   std::uint16_t recursive,
                   std::vector<std::uint16_t>& prefix) {
    if (!tail(recursive, root, false, prefix)) return false;
    std::vector<std::uint16_t> x, y;
    sequence(base, x);
    for (const auto kid : prefix) sequence(kid, y);
    return x.size() == y.size() &&
           std::ranges::equal(x, y, [&](std::uint16_t p, std::uint16_t q) {
             return facts(p).regular && facts(q).regular && same(p, q);
           });
  }
};

}  // namespace cursiv::detail
