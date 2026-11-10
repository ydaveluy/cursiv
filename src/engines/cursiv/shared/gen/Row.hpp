// The published row: an action is a type (what it does, to which class,
// through which member), and its id is its position in the list a reader
// builds its thunks from.
#pragma once

#include <type_traits>

#include "cursiv/syntax-tree/MemberTraits.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

template <auto P>
struct Wrote {};

// `N` is the candidates a reference narrows to and `Src` what the write reads:
// two directives that differ in either are two actions.
template <WriteKind K, class T, class M = void, class N = void,
          class Src = void>
struct Row {};

// A static member's `decltype` carries its `const`, which no pointer-to-member
// trait matches.
template <auto P, class Src = void>
using owned_row =
    Row<WriteKind::assign,
        typename member_traits<std::remove_cv_t<decltype(P)>>::class_type,
        Wrote<P>, void, Src>;

}  // namespace cursiv::engine
