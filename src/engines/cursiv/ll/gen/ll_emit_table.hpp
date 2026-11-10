// The frozen table of one grammar, written as C++ source.
#pragma once

#include <string>

namespace cursiv::engine {

class LlFinalProgram;

// The header every parser unit reads, with the view's functions declared, and
// their definitions for the parser's own unit only.
struct LlTableSource {
  std::string header;
  std::string includes;
  std::string functions;
};

LlTableSource ll_emit_table(const LlFinalProgram& done, const std::string& ns);

}  // namespace cursiv::engine
