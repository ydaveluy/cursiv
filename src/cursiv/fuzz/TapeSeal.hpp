// Every column of every record of one tree, folded into one number: two
// tapes are the same tape or they are not, cheap enough to carry per
// document instead of a walk that prints. Order is one of the columns --
// folding each record with its own id makes a permutation a difference.
#pragma once

#include <cstdint>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

namespace cursiv::fuzzer {

struct TapeSeal {
  std::uint64_t digest = 0xcbf29ce484222325ull;

  constexpr void fold(std::uint64_t v) {
    digest = (digest ^ v) * 0x100000001b3ull;
  }
};

// The count first, so a tape that is a prefix of another does not seal alike.
inline void seal_records(const Cst& c, TapeSeal& s) {
  s.fold(c.size());
  for (NodeId id = 0; id < c.size(); ++id) {
    const CstNode& n = c[id];
    s.fold(id);
    s.fold(n.begin);
    s.fold(n.end);
    s.fold(n.subtreeSize);
    s.fold(n.engine);
    s.fold(static_cast<std::uint64_t>(n.hidden) |
           (static_cast<std::uint64_t>(n.recovered) << 1));
  }
}

}  // namespace cursiv::fuzzer
