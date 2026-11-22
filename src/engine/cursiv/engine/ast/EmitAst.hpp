// The source of a language's tree builder.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Pack.hpp"

namespace cursiv::engine::gen {

// Defines `ns::kAst`, the `AstTables` of the grammar `grammar` that `header`
// declares: per element of `packed`, the actions its record applies, read
// off the entries of `cfg`.
std::string emit_ast(const Cfg& cfg, const Packed& packed, std::string_view ns,
                     std::string_view header, std::string_view grammar);

}  // namespace cursiv::engine::gen
