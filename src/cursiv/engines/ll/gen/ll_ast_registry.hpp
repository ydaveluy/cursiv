// The AST registries of an LL grammar.
#pragma once

#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/shared/gen/AstRowOf.hpp"

namespace cursiv {

template <class G>
AstRegistry ll_ast_registry(std::string_view grammar = detail::raw_type_name<G>()) {
  return ast_registry<G, LlLowering<G>>(grammar);
}

}  // namespace cursiv
