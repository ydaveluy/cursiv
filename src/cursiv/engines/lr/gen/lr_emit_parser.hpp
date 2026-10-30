// One grammar's parser, emitted as C++ source: a shared header and the state
// bodies. Reads no grammar type.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cursiv {

class LrFinalTable;

// The lines one function of states holds at most. GCC's compile time grows
// faster than a function's size, its global common-subexpression pass most
// of all, so the states split in small groups; a jump between two groups
// costs the parse little.
inline constexpr unsigned kLrGroupLines = 1u << 12;

// The parser's sources, by file name: `ParserStates.cpp`, then
// `ParserGroups.hpp` and the units. A lone group stays in `ParserStates.cpp`
// with its helpers, the header and the units then empty. `probeCuts` counts
// the states whose entry probe stopped short.
std::vector<std::pair<std::string, std::string>> lr_emit_parser_files(
    const LrFinalTable& done, std::string_view ns,
    unsigned* probeCuts = nullptr, unsigned groupLines = kLrGroupLines);

std::string lr_emit_parser_decl(const LrFinalTable& done, std::string_view ns);

}  // namespace cursiv
