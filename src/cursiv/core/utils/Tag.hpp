// The identity of a type and of a member is its address: one inline variable
// per template argument, no two of them sharing one. A name is decoration --
// nothing compares one, here or downstream.
#pragma once

namespace cursiv::detail {

struct Tag {};

template <class T>
inline constexpr Tag type_tag{};

}  // namespace cursiv::detail
