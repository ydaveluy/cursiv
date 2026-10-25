// The grammar's own text and rule data, as C++ source: what a language
// answers when asked for its grammar.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/grammar/GrammarDump.hpp"

namespace cursiv {

std::string emit_grammar_source(std::string_view ns, const GrammarDump& dump);

}  // namespace cursiv
