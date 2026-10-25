// One question a condition asks at the token in hand: whether the gap before
// it holds or is an expression, whether a phrase opens on the token, whether
// the input ends there, or whether the token is one of a set.
#pragma once

#include <cstdint>
#include <limits>

#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

enum class GapLeafKind : std::uint8_t { has, is, end, head, phrase, cut };

struct GapLeaf {
  GapLeafKind kind = GapLeafKind::has;
  // The expression's root in the lexical pool; for a cut, the token or the
  // choice of tokens the rule reads, whose terminals are the set.
  std::uint16_t node = 0;
  // The skip a head or a phrase reads before its first token, `kNoLexNode`
  // where its configuration skips nothing.
  std::uint16_t skip = kNoLexNode;
  // The configuration it is read under, whose section its text ends with; the
  // document's for the leaf that looks for that end.
  std::uint8_t cfg = 0;
  // The exit of a commit: its first token must be one the rule reads there.
  bool trigger = false;
  constexpr bool reads_tokens() const {
    return kind == GapLeafKind::head || kind == GapLeafKind::phrase;
  }
  constexpr bool operator==(const GapLeaf&) const = default;
};

// A condition is a postfix program over its leaves: a leaf index, or one of
// these followed by the count of operands it takes.
inline constexpr std::uint16_t kGapNot = 0xffffu;
inline constexpr std::uint16_t kGapOr = 0xfffeu;
inline constexpr std::uint16_t kGapAnd = 0xfffdu;

// A table names a condition plus one in a byte, zero standing for none.
inline constexpr std::uint16_t kGapConds =
    std::numeric_limits<std::uint8_t>::max();

template <class Cfg>
DynSet gap_cut_terms(const Cfg& c, const GapLeaf& leaf) {
  DynSet out;
  const LexNode& e = c.lex[leaf.node];
  if (e.kind == LexKind::token) {
    out.set(e.a);
  } else {
    for (unsigned i = 0; i < e.b; ++i) out.set(c.lex[c.lexKid[e.a + i]].a);
  }
  return out;
}

// Whether condition `k` asks only that the token in hand be none of a set, and
// which.
template <class Cfg>
bool gap_cut_alone(const Cfg& c, unsigned k, DynSet* terms = nullptr) {
  const std::uint32_t at = c.gapCondAt[k];
  if (c.gapCondAt[k + 1u] - at != 3u || c.gapOps[at + 1u] != kGapNot)
    return false;
  const GapLeaf& leaf = c.gapLeaves[c.gapOps[at]];
  if (leaf.kind != GapLeafKind::cut) return false;
  if (terms != nullptr) *terms = gap_cut_terms(c, leaf);
  return true;
}

}  // namespace cursiv
