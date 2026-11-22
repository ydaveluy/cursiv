// The writes of a body made one per alternative before the walk lowers it.
#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

template <class D, class Children = typename detail::ExprShape<D>::children>
struct Distribute {
  using type = D;
};
template <class D, class A>
using Over = Distribute<typename detail::ExprShape<D>::template rebind<A>>;
// A directive over a choice is one directive per alternative: left whole, no
// record could say which alternative the value came from.
template <class D, class... As>
  requires detail::is_directive<D>
struct Distribute<D, TypeList<detail::Choice<As...>>> {
  using type = detail::Choice<typename Over<D, As>::type...>;
};
// Trivia scopes and conditions around a single reader preserve its choice of value.
template <class E, bool Write>
inline constexpr bool value_choice = false;
template <bool Write, class... As>
inline constexpr bool value_choice<detail::Choice<As...>, Write> = true;
template <class I, class H, class E, bool Write>
inline constexpr bool value_choice<detail::Skip<I, H, E>, Write> = value_choice<E, Write>;
template <std::size_t Min, std::size_t Max, class E>
inline constexpr bool value_choice<detail::Repeat<Min, Max, E>, true> = true;
template <class... Es>
  requires (detail::tl_size<detail::readers_t<Es...>>::value == 1)
inline constexpr bool value_choice<detail::Seq<Es...>, true> =
    value_choice<detail::tl_at_t<0, detail::readers_t<Es...>>, true>;

template <class D, class I, class H, class E>
  requires (detail::is_directive<D> && value_choice<E, detail::writes_value<D>>)
struct Distribute<D, TypeList<detail::Skip<I, H, E>>> {
  using type = detail::Skip<I, H, typename Over<D, E>::type>;
};
template <class D, class... Es>
  requires (detail::writes_value<D> && value_choice<detail::Seq<Es...>, true>)
struct Distribute<D, TypeList<detail::Seq<Es...>>> {
  using type = detail::Seq<typename detail::cond_t<detail::is_condition<Es>, std::type_identity<Es>,
                                           Over<D, Es>>::type...>;
};

template <class D, std::size_t Min, std::size_t Max, class E>
  requires detail::writes_value<D>
struct Distribute<D, TypeList<detail::Repeat<Min, Max, E>>> {
  static_assert(Max == 1, "put assign or append inside the repetition to write each matched item");
  using type = detail::Repeat<Min, Max, typename Over<D, E>::type>;
};

// Children first: an inner directive can reveal a choice the outer write
// distributes over too.
template <class E, bool Writes = detail::holds_directive<E>>
struct NormalizeWrites {
  using type = E;
};
template <class E>
using normalize_writes_t = typename NormalizeWrites<E>::type;
template <class E>
struct NormalizeWrites<E, true> : Distribute<typename detail::rebuild<E, normalize_writes_t>::type> {};

}  // namespace cursiv::engine
