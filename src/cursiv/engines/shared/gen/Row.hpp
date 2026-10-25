// The published row: an action is a type here -- what it does, to which class,
// through which member -- and its id is its position in the list. A reader
// builds its own table of thunks from that same list, in the same order, with
// nothing to reconcile.
#pragma once

#include <type_traits>

#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv::detail {

template <auto P>
struct Wrote {};

// `N` is the candidates a reference narrows to and `Src` what the write reads.
// Both belong to the directive and not to the position: two directives writing
// one member through different narrowings -- or from payloads two different
// converters read -- do two different things, so they are two actions.
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

}  // namespace cursiv::detail
