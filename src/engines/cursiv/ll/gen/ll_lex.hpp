// The lexer construction: it reads the program's terminals and what every
// position admits, and writes one automaton for all the modes.
#pragma once

#include <cstdint>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"

namespace cursiv::engine {
struct LexAlphabet;
}

namespace cursiv::engine {

// The automaton over the token graphs and what each point admits. The token
// graphs also serve the earlier trivia-boundary checks. `walks` counts the pairs
// of terminals walked for what reads over what.
void ll_build_lexer(LlBuiltProgram& b, LexAlphabet& alpha,
                    std::uint64_t* walks = nullptr);

}  // namespace cursiv::engine
