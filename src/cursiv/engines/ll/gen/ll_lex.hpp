// The lexer construction, declared apart: it reads the program's terminals and
// what every position admits, and writes one automaton for all the modes.
#pragma once

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"

namespace cursiv::detail {

// The whole of the lexer, in the order the three passes owe each other: the
// token graphs, then what each point admits, then the automaton over both.
void ll_build_lexer(LlBuiltProgram& b);

}  // namespace cursiv::detail
