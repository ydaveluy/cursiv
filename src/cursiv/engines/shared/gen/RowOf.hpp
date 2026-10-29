// One published action, read off its type. The lowering states a run as a list
// of `Row<K, T, M, N, Src>`; these are the questions a reader asks of one, and
// every answer is a template argument.
#pragma once

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/gen/Row.hpp"

namespace cursiv::detail {

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

}  // namespace cursiv::detail
