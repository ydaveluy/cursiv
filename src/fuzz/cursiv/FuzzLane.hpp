// A language's fuzz lane: documents written from the declared grammar, and
// the tree the parser built weighed against the tree the grammar states. It
// names no engine and no table -- whichever one generated the language, the
// lane asks it the same question through the erased door.
#pragma once

#include <cstdint>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/FuzzRun.hpp"

namespace cursiv::fuzzer {

// Answers the documents and the writes the walk could not model.
template <class G>
std::uint64_t lane(const char* lang, const cursiv::Parser& parser,
                   const cursiv::AstReflection& reflection, unsigned docs,
                   std::uint64_t seed) {
  return detail::run<G>(lang, parser, reflection, docs, seed);
}

}  // namespace cursiv::fuzzer
