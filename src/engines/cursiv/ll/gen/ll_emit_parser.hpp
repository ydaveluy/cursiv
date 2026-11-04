// The descent of one grammar, written as C++ source.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cursiv {

class LlFinalProgram;
struct LlLowCfg;
struct LlTableSource;

// How many plain rules, called in place with no frame, a native call chain
// stacks at most: past it, a rule goes through the machine, so the native
// stack does not grow with the grammar.
inline constexpr std::uint8_t kLlPlainDepth = 64;

// How many plain rules stand on the native stack from each rule down; zero for
// a rule with a frame.
std::vector<std::uint8_t> ll_plain_depths(const LlLowCfg& cfg);

std::string ll_emit_parser_decl(const std::string& ns);
// The parser's units; the parser's own unit defines the table's functions.
std::vector<std::pair<std::string, std::string>> ll_emit_parser_files(
    const LlFinalProgram& done, const std::string& ns,
    const LlTableSource& table);

}  // namespace cursiv
