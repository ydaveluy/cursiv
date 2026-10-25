// One grammar's parser, emitted as C++ source: a shared header and the state
// bodies. Reads no grammar type.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"

namespace cursiv {

struct LrProgramLayout;

// The lines one function of states holds at most: past them its compile
// time outgrows its size, and a jump between two such functions costs the
// parse, so a group is as large as that allows.
inline constexpr unsigned kLrGroupLines = 1u << 17;

// `probeCuts` counts the states whose entry probe stopped short.
std::vector<std::pair<std::string, std::string>> lr_emit_parser_files(
    const LrBuildTable& a, std::string_view ns, const LrProgramLayout& programs,
    unsigned* probeCuts = nullptr, unsigned groupLines = kLrGroupLines);

std::string lr_emit_parser_decl(const LrBuildTable& a, std::string_view ns);

}  // namespace cursiv
