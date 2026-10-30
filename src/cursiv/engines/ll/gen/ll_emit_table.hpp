// The frozen table of one grammar, written as C++ source.
#pragma once

#include <string>

namespace cursiv {

class LlFinalProgram;

// The header every unit of the parser reads -- the data, and the functions it
// points at declared -- and those functions themselves, with what they
// include, for the parser's own unit: each unit that reads the table would
// otherwise compile its own copy of every one of them.
struct LlTableSource {
  std::string header;
  std::string includes;
  std::string functions;
};

LlTableSource ll_emit_table(const LlFinalProgram& done, const std::string& ns);

}  // namespace cursiv
