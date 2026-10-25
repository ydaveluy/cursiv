// Reflection describes the author's types and members, independently of the
// parser that constructs them. Both generators emit these tables here.
#pragma once

#include <string>

#include "cursiv/engines/shared/gen/AstRegistry.hpp"

namespace cursiv {

std::string emit_ast_metadata(const AstRegistry& registry);

std::string emit_ast_metadata_record(const AstRegistry& registry);

}  // namespace cursiv
