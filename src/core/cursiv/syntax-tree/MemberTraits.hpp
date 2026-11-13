#pragma once

#include <string_view>

#include "cursiv/utils/TypeName.hpp"

// What a pointer to member says about itself: the class it writes into, the
// type it holds, and the identifier it renders as.

namespace cursiv::detail {

// Splits a pointer-to-member into its class and member type.
template <class P>
struct member_traits;
template <class C, class M>
struct member_traits<M C::*> {
  using class_type = C;
  using type = M;
};

// Decoration, not a key: dispatch goes by field index. The pointer renders as
// `&Owner::field`, so the last `::`-component of it is the identifier.
template <auto P>
inline constexpr std::string_view kMemberIdentifierView =
    without_decorations(last_component(raw_member_name<P>()));
template <auto P>
constexpr std::string_view member_identifier() {
  return name_store<kMemberIdentifierView<P>>::view;
}

}  // namespace cursiv::detail
