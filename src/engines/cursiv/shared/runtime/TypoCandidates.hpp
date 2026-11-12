// The re-readings of a misspelling as the walk emits them: a (terminal, end)
// once, at its least cost, sorted when they are asked for: the cheaper first,
// then the longer span, then the walk's own order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/utils/macros.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"
#include "cursiv/shared/table/FlatMap.hpp"

namespace cursiv::engine {

class TypoCandidates {
 public:
  // Out of line: inlined into the walk, it costs the walk the inlining of its
  // own table calls, which a search pays byte by byte.
  CURSIV_NOINLINE void add(const Typo& candidate);

  std::vector<Typo> ranked() const;

 private:
  std::vector<Typo> _found;
  std::vector<std::uint64_t> _emitted;
  std::uint64_t _clock = 0;
  FlatMap<std::size_t> _at;
};

}  // namespace cursiv::engine
