// One grammar's parser, emitted as C++ source: a shared header and the state
// bodies. Reads no grammar type.
#pragma once

#include <string>
#include <string_view>

namespace cursiv {

class LrFinalTable;

// The lines one function of states holds at most: past them its compile
// time outgrows its size, and a jump between two such functions costs the
// parse, so a group is as large as that allows.
inline constexpr unsigned kLrGroupLines = 1u << 17;

// The state bodies, `ParserStates.cpp`; `probeCuts` counts the states whose
// entry probe stopped short.
std::string lr_emit_parser_states(const LrFinalTable& done, std::string_view ns,
                                  unsigned* probeCuts = nullptr,
                                  unsigned groupLines = kLrGroupLines);

std::string lr_emit_parser_decl(const LrFinalTable& done, std::string_view ns);

}  // namespace cursiv
