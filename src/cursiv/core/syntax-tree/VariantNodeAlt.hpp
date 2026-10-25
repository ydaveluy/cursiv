// The alternatives of a variant member that hold an AST node, in declaration
// order -- a reader indexes this list against the field's targets.
#pragma once

#include <type_traits>
#include <variant>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

template <class A>
inline constexpr bool ast_pointer_alt =
    std::is_pointer_v<A> &&
    std::is_base_of_v<AstNode, std::remove_pointer_t<std::remove_cv_t<A>>>;
template <class... As>
struct node_alts
    : tl_cat<cond_t<ast_pointer_alt<As>, TypeList<As>, TypeList<>>...> {};
template <class M>
struct variant_node_alt {
  using type = TypeList<>;
};
template <class... As>
struct variant_node_alt<std::variant<As...>> : node_alts<As...> {};

}  // namespace cursiv::detail
