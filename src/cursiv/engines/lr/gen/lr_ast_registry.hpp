// The AST registries of an LR grammar.
#pragma once

#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/AstRowOf.hpp"

namespace cursiv {

template <class G>
AstRegistry lr_ast_registry(std::string_view grammar = detail::raw_type_name<G>()) {
  return ast_registry<G, LrLowering<G>>(grammar);
}

}  // namespace cursiv
