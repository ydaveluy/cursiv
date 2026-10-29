// The frozen table of one grammar, written as C++ source.
#pragma once

#include <string>

namespace cursiv {

class LlFinalProgram;

std::string ll_emit_table(const LlFinalProgram& done, const std::string& ns);

}  // namespace cursiv
