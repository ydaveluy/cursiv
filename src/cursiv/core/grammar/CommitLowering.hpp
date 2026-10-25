// A rule's commits written out: the quantifier of `commit(prefix, body)` reads
// `prefix + body` and stops only where `!prefix` holds, so every exit it can
// take is guarded by that condition and the exit at Max is not.
#pragma once

#include <cstddef>
#include <type_traits>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/typelist.hpp"

namespace cursiv::detail {

template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool holds_commit = false;
template <class E, class... Ks>
inline constexpr bool holds_commit<E, TypeList<Ks...>> =
    (holds_commit<Ks> || ...);
template <class P, class B>
inline constexpr bool holds_commit<Commit<P, B>, TypeList<P, B>> = true;
template <class T, class C>
inline constexpr bool holds_commit<Implied<T, C>, TypeList<>> = holds_commit<C>;

template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool stray_commit = false;
template <class E, class... Ks>
inline constexpr bool stray_commit<E, TypeList<Ks...>> =
    (stray_commit<Ks> || ...);
template <class P, class B>
inline constexpr bool stray_commit<Commit<P, B>, TypeList<P, B>> = true;
template <std::size_t Mn, std::size_t Mx, class P, class B>
inline constexpr bool
    stray_commit<Repeat<Mn, Mx, Commit<P, B>>, TypeList<Commit<P, B>>> =
        stray_commit<P> || stray_commit<B>;
template <class T, class C>
inline constexpr bool stray_commit<Implied<T, C>, TypeList<>> = stray_commit<C>;

template <class E, bool Holds = holds_commit<E>>
struct CommitLowering {
  using type = E;
};
template <class E>
using commit_free_t = typename CommitLowering<E>::type;

template <class E, class Kids>
struct commit_rebuilt;
template <class E, class... Ks>
struct commit_rebuilt<E, TypeList<Ks...>> {
  using type = typename ExprShape<E>::template rebind<commit_free_t<Ks>...>;
};
template <class E>
struct CommitLowering<E, true>
    : commit_rebuilt<E, typename ExprShape<E>::kids> {};
template <class... Es>
struct CommitLowering<Seq<Es...>, true> {
  using type = cat_seq_t<commit_free_t<Es>...>;
};
template <class T, class C>
struct CommitLowering<Implied<T, C>, true> {
  using type = Implied<T, commit_free_t<C>>;
};

template <class Item, class Stop, std::size_t Turns>
struct commit_ladder {
  using type = Choice<
      cat_seq_t<Item, typename commit_ladder<Item, Stop, Turns - 1>::type>,
      Stop>;
};
template <class Item, class Stop>
struct commit_ladder<Item, Stop, 1> {
  using type = Choice<Item, Stop>;
};

template <std::size_t Mn, class Item>
struct commit_lead {
  using type = cond_t<Mn == 1, Item, Repeat<Mn, Mn, Item>>;
};

template <std::size_t Mn, std::size_t Mx, class Item, class Stop,
          int Kind = Mn == Mx           ? 0
                     : Mx == kUnbounded ? 1
                     : Mn == 0          ? 2
                                        : 3>
struct commit_exits;
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 0> {
  using type = Repeat<Mn, Mx, Item>;
};
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 1> {
  using type = cat_seq_t<Repeat<Mn, kUnbounded, Item>, Stop>;
};
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 2> : commit_ladder<Item, Stop, Mx> {};
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 3> {
  using type = cat_seq_t<typename commit_lead<Mn, Item>::type,
                         typename commit_ladder<Item, Stop, Mx - Mn>::type>;
};

template <std::size_t Mn, std::size_t Mx, class P, class B>
struct CommitLowering<Repeat<Mn, Mx, Commit<P, B>>, true>
    : commit_exits<Mn, Mx, cat_seq_t<commit_free_t<P>, commit_free_t<B>>,
                   Not<Trigger<commit_free_t<P>>>> {};

// A write over an option writes only where its item is kept: it moves under
// the choice the commit makes of the option. A flag stays above it and always
// sets.
template <class Directive, class P, class B>
struct commit_option_under {
  using item = cat_seq_t<commit_free_t<P>, commit_free_t<B>>;
  using type = Choice<typename ExprShape<Directive>::template rebind<item>,
                      Not<Trigger<commit_free_t<P>>>>;
};
template <auto Sp, class P, class B>
struct CommitLowering<Assign<Sp, Repeat<0, 1, Commit<P, B>>>, true>
    : commit_option_under<Assign<Sp, Repeat<0, 1, Commit<P, B>>>, P, B> {};
template <auto Sp, class P, class B>
struct CommitLowering<Append<Sp, Repeat<0, 1, Commit<P, B>>>, true>
    : commit_option_under<Append<Sp, Repeat<0, 1, Commit<P, B>>>, P, B> {};

}  // namespace cursiv::detail
