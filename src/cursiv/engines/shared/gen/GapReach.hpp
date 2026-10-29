// How far each gap condition reads the token in hand and what stands past it,
// worked out of the lexical expressions its leaves test.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

template <class Cfg>
class GapReach {
 public:
  explicit GapReach(const Cfg& g) : _g(g) {}

  // A phrase whose skipper reads trivia: how far it reads, only the text
  // says.
  bool skips(unsigned leaf) const {
    return _g.gapLeaves[leaf].reads_tokens() &&
           bounds(_g.gapLeaves[leaf].node).looked >= 0xffffffffu;
  }

  // The leaves condition `k` reads.
  std::vector<std::uint16_t> leaves(unsigned k) const {
    std::vector<std::uint16_t> out;
    for (std::uint32_t i = _g.gapCondAt[k]; i < _g.gapCondAt[k + 1]; ++i) {
      const std::uint16_t v = _g.gapOps[i];
      if (v == kGapNot || v == kGapOr || v == kGapAnd)
        ++i;
      else
        out.push_back(v);
    }
    return out;
  }

  bool skips_any(unsigned k) const {
    return std::ranges::any_of(leaves(k),
                               [&](std::uint16_t v) { return skips(v); });
  }
  bool skips_any() const {
    for (unsigned k = 0; k < _g.ngapConds; ++k)
      if (skips_any(k)) return true;
    return false;
  }

  // Unbounded phrases count whole tokens, whose widths the text determines.
  CondReach of(unsigned k) const {
    std::uint64_t look = 0, tail = 0;
    std::uint32_t tokens = 0;
    bool phrase = false;
    for (const std::uint16_t v : leaves(k)) {
      const GapLeaf& leaf = _g.gapLeaves[v];
      if (!leaf.reads_tokens()) continue;
      const auto reach = bounds(leaf.node);
      if (reach.looked >= 0xffffffffu) {
        phrase = true;
        tokens = std::max(tokens, count(leaf.node));
      } else {
        look = std::max(look, reach.looked);
        tail = std::max(tail, leaf.kind == GapLeafKind::phrase
                                 ? reach.looked
                                 : reach.overhang);
      }
    }
    return CondReach{static_cast<TextOffset>(look),
                     static_cast<TextOffset>(std::min(tail, look)), phrase,
                     static_cast<std::uint16_t>(tokens)};
  }

  // The widest of every condition: what a completion that splices a candidate
  // in must keep of the text for any of them to be asked.
  CondReach widest() const {
    CondReach out;
    for (unsigned k = 0; k < _g.ngapConds; ++k) {
      const CondReach r = of(k);
      out.look = std::max(out.look, r.look);
      out.tail = std::max(out.tail, r.tail);
      out.phrase = out.phrase || r.phrase;
      out.tokens = std::max(out.tokens, r.tokens);
    }
    return out;
  }

 private:
  struct Bounds {
    std::uint64_t consumed = 0, looked = 0, overhang = kScalarBytes;
  };

  // Maximum consumption, inspection and look past the consumed bytes,
  // computed in one walk of the expression.
  Bounds bounds(std::uint16_t node) const {
    static constexpr std::uint64_t kAll = 0xffffffffu;
    const auto sat = [](std::uint64_t v) { return std::min(v, kAll); };
    const LexNode& e = _g.lex[node];
    switch (e.kind) {
      case LexKind::token:
        return bounds(lex_token_body(_g, e));
      case LexKind::positive:
      case LexKind::negative: {
        const auto kid = bounds(e.a);
        return {0, kid.looked, kid.looked};
      }
      case LexKind::gap_has:
      case LexKind::gap_is:
      case LexKind::end_mark:
        return {0, 0, 0};
      case LexKind::chars:
        return {1, 1};
      case LexKind::nguard:
        return {0, e.min + kScalarBytes, e.min + kScalarBytes};
      // Trivia read to any length, or none at all.
      case LexKind::skip:
        return e.b == 0 ? Bounds{0, 0, 0} : Bounds{kAll, kAll};
      case LexKind::seq:
      case LexKind::alt: {
        Bounds out;
        const bool sequence = e.kind == LexKind::seq;
        for (unsigned i = 0; i < e.b; ++i) {
          const auto kid = bounds(_g.lexKid[e.a + i]);
          out.looked =
              std::max(out.looked,
                       sequence ? sat(out.consumed + kid.looked) : kid.looked);
          out.consumed = sequence ? sat(out.consumed + kid.consumed)
                                  : std::max(out.consumed, kid.consumed);
          out.overhang = std::max(out.overhang, kid.overhang);
        }
        if (sequence) out.looked = std::max(out.consumed, out.looked);
        return out;
      }
      case LexKind::rep: {
        auto kid = bounds(e.a);
        if (e.b != kNoLexNode) {
          const auto skip = bounds(e.b);
          kid = {sat(skip.consumed + kid.consumed),
                 std::max(skip.looked, sat(skip.consumed + kid.looked)),
                 std::max(skip.overhang, kid.overhang)};
        }
        if (e.max == 0) return {0, 0, kid.overhang};
        if (kid.consumed == 0) return {0, kid.looked, kid.overhang};
        if (e.max == 0xffffffffu) return {kAll, kAll, kid.overhang};
        return {sat(kid.consumed * e.max),
                sat(kid.consumed * (e.max - 1u) + kid.looked), kid.overhang};
      }
      default:
        return {kAll, kAll};
    }
  }

  // The whole terminals one round of a phrase reads, the token in hand among
  // them: a run inside a terminal is that terminal's, whatever its length, and
  // trivia between two of them are no token of the tape's.
  std::uint32_t count(std::uint16_t node) const {
    static constexpr std::uint32_t kAll = kUnboundedTokens;
    const auto sat = [](std::uint64_t v) {
      return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, kAll));
    };
    const LexNode& e = _g.lex[node];
    switch (e.kind) {
      case LexKind::token:
      case LexKind::chars:
      case LexKind::nguard:
        return 1;
      case LexKind::positive:
      case LexKind::negative:
        return count(e.a);
      case LexKind::gap_has:
      case LexKind::gap_is:
      case LexKind::end_mark:
      case LexKind::skip:
        return 0;
      case LexKind::seq:
      case LexKind::alt: {
        std::uint64_t out = 0;
        for (unsigned i = 0; i < e.b; ++i) {
          const std::uint32_t kid = count(_g.lexKid[e.a + i]);
          out = e.kind == LexKind::seq ? out + kid
                                       : std::max<std::uint64_t>(out, kid);
        }
        return sat(out);
      }
      case LexKind::rep: {
        std::uint64_t kid = count(e.a);
        if (e.b != kNoLexNode) kid += count(e.b);
        if (e.max == 0 || kid == 0) return 0;
        return e.max == 0xffffffffu ? kAll : sat(kid * e.max);
      }
      default:
        return kAll;
    }
  }

  const Cfg& _g;
};

}  // namespace cursiv
