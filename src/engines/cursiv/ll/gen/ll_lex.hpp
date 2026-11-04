// The lexer construction, declared apart: it reads the program's terminals and
// what every position admits, and writes one automaton for all the modes.
#pragma once

#include <cstdint>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"

namespace cursiv {
struct LexAlphabet;
}

namespace cursiv::detail {

// The automaton over the token graphs and what each point admits. The token
// graphs also serve the earlier trivia-boundary checks. `walks` counts the pairs
// of terminals walked for what reads over what.
void ll_build_lexer(LlBuiltProgram& b, LexAlphabet& alpha,
                    std::uint64_t* walks = nullptr);

}  // namespace cursiv::detail
