// The frozen table of one grammar, written as C++ source.
#pragma once

#include <string>

#include "cursiv/engines/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/engines/ll/gen/LlStampPlan.hpp"

namespace cursiv {

std::string ll_emit_table(const LlBuiltProgram& a, const LlStampPlan& plan,
                          const std::string& ns);

}  // namespace cursiv
