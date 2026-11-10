// A language's fuzz lane: documents written from the declared grammar, and
// the parser's tree compared with the one the grammar states, whatever the
// engine.
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
