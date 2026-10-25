// A language's fuzz lane: documents written from the declared grammar, and
// the tree the parser built weighed against the tree the grammar states. It
// names no engine and no table -- whichever one generated the language, the
// lane asks it the same question through the erased door.
#pragma once

#include <cstdint>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/fuzz/FuzzRun.hpp"

namespace cursiv::fuzzer {

template <class G>
void lane(const char* lang, const cursiv::Parser& parser,
          const cursiv::AstReflection& reflection, unsigned docs,
          std::uint64_t seed) {
  run<G>(lang, parser, reflection, docs, seed);
}

}  // namespace cursiv::fuzzer
