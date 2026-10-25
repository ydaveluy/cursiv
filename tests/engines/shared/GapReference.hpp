// Reading a regular lexical expression over a trivia gap.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "cursiv/engines/shared/runtime/GapPositions.hpp"
#include "cursiv/engines/shared/runtime/LexBoundary.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv::test {

// The positions `node` reaches from any of `in`: one bit per position,
// `n + 1` of them. Classes and guards stay within the gap; a guard decodes
// a scalar where its set does.
struct GapReference {
  const LexNode* nodes;
  const std::uint16_t* kids;
  LexBoundary boundary;
  const char* s;
  TextOffset n;
  bool has(std::uint16_t set, TextOffset i) const {
    return boundary.at(set, s, i, n);
  }

  // Whether the word a guard refuses is spelled whole at `p`: its classes
  // in a row, then the end of the text or a byte its rescuing class does
  // not admit.
  bool refused_at(const LexNode& e, TextOffset p) const {
    if (e.min > n - p) return false;
    for (std::uint32_t k = 0; k < e.min; ++k)
      if (!has(kids[e.b + k], p + k)) return false;
    const TextOffset after = p + e.min;
    return e.a == kNoSet || after >= n || !has(e.a, after);
  }

  using Word = GapPositions::Word;
  using Span = GapPositions::Span;

  template <class Set>
  Set compose(const LexNode& e, const Set& in) const {
    Set out;
    switch (e.kind) {
      case LexKind::seq: {
        Set cur = in;
        for (unsigned i = 0; i < e.b && !cur.empty(); ++i)
          cur = step(kids[e.a + i], cur);
        return cur;
      }
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i) out.unite(step(kids[e.a + i], in));
        return out;
      default: {
        Set cur = in;
        std::uint32_t k = 0;
        for (; k < e.min; ++k) {
          Set next = step(e.a, cur);
          if (next == cur) return cur;
          cur = std::move(next);
        }
        // Past the minimum only the positions a round adds are stepped again.
        out = cur;
        for (; !cur.empty() && (e.max == 0xffffffffu || k < e.max); ++k) {
          cur = step(e.a, cur).without(out);
          out.unite(cur);
        }
        return out;
      }
    }
  }

  template <class Set>
  Set step(std::uint16_t node, const Set& in) const {
    const LexNode& e = nodes[node];
    Set out;
    switch (e.kind) {
      case LexKind::chars:
        in.each([&](TextOffset p) {
          if (p < n && boundary.sets[e.a].has(static_cast<unsigned char>(s[p])))
            out.set(p + 1);
        });
        return out;
      case LexKind::nguard:
        in.each([&](TextOffset p) {
          if (!refused_at(e, p)) out.set(p);
        });
        return out;
      case LexKind::seq:
      case LexKind::alt:
      case LexKind::rep:
        return compose(e, in);
      default:
        return out;
    }
  }
};

// Whether some run of the gap (`whole` false), or all of it, is read by
// `root`.
inline bool gap_reference(const LexNode* nodes, const std::uint16_t* kids,
                          LexBoundary boundary, std::uint16_t root,
                          const char* s, TextOffset n, bool whole) {
  const GapReference r{nodes, kids, boundary, s, n};
  const auto run = [&](auto in) {
    if (whole) {
      in.set(0);
    } else {
      for (TextOffset p = 0; p < n; p += gap_width(s + p, n - p)) in.set(p);
      in.set(n);
    }
    const auto out = r.step(root, in);
    return whole ? out.has(n) : !out.empty();
  };
  if (n < 64) return run(GapReference::Word{});
  return run(GapReference::Span{});
}

}  // namespace cursiv::test
