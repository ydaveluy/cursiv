// The frozen table of one grammar, emitted as C++ source. Reads no grammar
// type, so it compiles once for every language.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"

namespace cursiv {

struct LrProgramLayout;

std::string lr_emit_table(const LrBuildTable& a, std::string_view ns,
                          const LrProgramLayout& programs);

}  // namespace cursiv
