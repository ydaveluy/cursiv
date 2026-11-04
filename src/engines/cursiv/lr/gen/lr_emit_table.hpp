// The frozen table of one grammar, emitted as C++ source. Reads no grammar
// type, so it compiles once for every language.
#pragma once

#include <string>
#include <string_view>

namespace cursiv {

class LrFinalTable;

std::string lr_emit_table(const LrFinalTable& done, std::string_view ns);

}  // namespace cursiv
