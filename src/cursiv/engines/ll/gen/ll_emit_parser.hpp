// The descent of one grammar, written as C++ source.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlStampPlan.hpp"

namespace cursiv {

std::string ll_emit_parser_decl(const std::string& ns);
std::vector<std::pair<std::string, std::string>> ll_emit_parser_files(
    const LlBuiltProgram& a, const LlStampPlan& plan, const std::string& ns);

}  // namespace cursiv
