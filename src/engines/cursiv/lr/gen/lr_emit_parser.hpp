// One grammar's parser, emitted as C++ source: a shared header and the state
// bodies. Reads no grammar type.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cursiv::engine {

class LrFinalTable;

// The lines each function of states aims at: GCC's compile time grows faster
// than a function's size, so the states split into groups of about this weight.
// A state or a shared dispatch never splits, so one group may hold more.
inline constexpr unsigned kLrGroupLines = 1u << 12;

// The parser's sources by file name: `ParserStates.cpp`, `ParserGroups.hpp` and
// the units; a lone group stays in `ParserStates.cpp`. `probeCuts` counts short
// entry probes. A zero `groupLines` throws.
std::vector<std::pair<std::string, std::string>> lr_emit_parser_files(
    const LrFinalTable& done, std::string_view ns,
    unsigned* probeCuts = nullptr, unsigned groupLines = kLrGroupLines);

std::string lr_emit_parser_decl(const LrFinalTable& done, std::string_view ns);

}  // namespace cursiv::engine
