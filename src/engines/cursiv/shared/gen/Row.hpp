// The published row: an action is a type (what it does, to which class,
// through which member), and its id is its position in the list a reader
// builds its thunks from. What a reader asks of one is a template argument.
#pragma once

#include <type_traits>

#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"

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

template <class R>
struct row_of;
template <WriteKind K, class T, class M, class N, class Src>
struct row_of<Row<K, T, M, N, Src>> {
  static constexpr WriteKind kind = K;
  using type = T;
  using narrow = N;
  using source = Src;
};

// The member a row names, as the tag the registries key on.
template <class R>
struct row_field {
  using type = void;
};
template <WriteKind K, class T, auto P, class N, class Src>
struct row_field<Row<K, T, Wrote<P>, N, Src>> {
  using type = field_tag<P>;
};

template <class R>
inline constexpr bool row_writes = row_of<R>::kind == WriteKind::assign ||
                                   row_of<R>::kind == WriteKind::append;

}  // namespace cursiv::engine
