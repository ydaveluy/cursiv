// The lexer's walk of one grammar, written as C++ source.
#pragma once

#include <string>

namespace cursiv {

struct LlBuiltProgram;

// The includes the walk reads, then, inside the grammar's namespace, the walk
// of every mode, `walk_all`, the table names as its `walk`.
std::string ll_emit_walk_includes(const LlBuiltProgram& a);
std::string ll_emit_walk(const LlBuiltProgram& a);

}  // namespace cursiv
