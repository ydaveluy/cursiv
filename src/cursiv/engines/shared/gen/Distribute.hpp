// The writes of a body made one per alternative before the walk lays it out.
#pragma once

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

template <class D, class Kids = typename ExprShape<D>::kids>
struct Distribute {
  using type = D;
};
template <class D>
using distribute_t = typename Distribute<D>::type;
template <class D, class A>
using over_t = distribute_t<typename ExprShape<D>::template rebind<A>>;
// A directive over a choice is one directive per alternative: left whole, no
// record could say which alternative the value came from.
template <class D, class... As>
  requires is_directive<D>
struct Distribute<D, TypeList<Choice<As...>>> {
  using type = Choice<over_t<D, As>...>;
};
// ...and the same choice one region deep: the region stays put.
template <class D, class I, class H, class... As>
  requires is_directive<D>
struct Distribute<D, TypeList<Skip<I, H, Choice<As...>>>> {
  using type = Skip<I, H, Choice<over_t<D, As>...>>;
};

// Children first: an inner directive can reveal a choice the outer write
// distributes over too.
template <class E, class Kids = typename ExprShape<E>::kids>
struct NormalizeWrites;
template <class E, class... Ks>
struct NormalizeWrites<E, TypeList<Ks...>> {
  using rebuilt = typename ExprShape<E>::template rebind<
      typename NormalizeWrites<Ks>::type...>;
  using type = distribute_t<rebuilt>;
};
template <class E>
using normalize_writes_t = typename NormalizeWrites<E>::type;

}  // namespace cursiv::detail
