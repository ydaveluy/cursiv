// A rule's commits written out: the quantifier of `commit(prefix, body)` reads
// `prefix + body` and stops only where `!prefix` holds, so every exit it can
// take is guarded by that condition and the exit at Max is not.
#pragma once

#include <cstddef>
#include <type_traits>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::detail {

enum : unsigned { kPlacedCommit = 1, kMisplacedCommit = 2 };
template <class E, class Children = typename ExprShape<E>::children>
inline constexpr unsigned commits = 0;
template <class E, class... Ks>
inline constexpr unsigned commits<E, TypeList<Ks...>> = (0u | ... | commits<Ks>);
template <class P, class B>
inline constexpr unsigned commits<Commit<P, B>, TypeList<P, B>> = kMisplacedCommit;
template <std::size_t Mn, std::size_t Mx, class P, class B>
inline constexpr unsigned commits<Repeat<Mn, Mx, Commit<P, B>>, TypeList<Commit<P, B>>> =
    (Mn == Mx ? kMisplacedCommit : kPlacedCommit) | commits<P> | commits<B>;
template <class T, class C>
inline constexpr unsigned commits<Implied<T, C>, TypeList<>> = commits<C>;
template <class E>
inline constexpr bool holds_commit = commits<E> != 0;
template <class E>
inline constexpr bool misplaced_commit = (commits<E> & kMisplacedCommit) != 0;

template <class E, bool Holds = holds_commit<E>>
struct CommitLowering {
  using type = E;
};
template <class E>
using commit_free_t = typename CommitLowering<E>::type;

template <class E>
struct CommitLowering<E, true> : rebuild<E, commit_free_t> {};
template <class... Es>
struct CommitLowering<Seq<Es...>, true> {
  using type = cat_seq_t<commit_free_t<Es>...>;
};
template <class T, class C>
struct CommitLowering<Implied<T, C>, true> {
  using type = Implied<T, commit_free_t<C>>;
};

template <class Item, class Stop, std::size_t Turns>
struct commit_counted_tail {
  using type =
      Choice<cat_seq_t<Item, typename commit_counted_tail<Item, Stop,
                                                          Turns - 1>::type>,
             Stop>;
};
template <class Item, class Stop>
struct commit_counted_tail<Item, Stop, 1> {
  using type = Choice<Item, Stop>;
};

template <std::size_t Mn, class Item>
struct commit_lead {
  using type = cond_t<Mn == 1, Item, Repeat<Mn, Mn, Item>>;
};

template <std::size_t Mn, std::size_t Mx, class Item, class Stop,
          int Kind = Mx == kUnbounded ? 1
                     : Mn == 0        ? 2
                                      : 3>
struct commit_exits;
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 1> {
  using type = cat_seq_t<Repeat<Mn, kUnbounded, Item>, Stop>;
};
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 2>
    : commit_counted_tail<Item, Stop, Mx> {};
template <std::size_t Mn, std::size_t Mx, class Item, class Stop>
struct commit_exits<Mn, Mx, Item, Stop, 3> {
  using type =
      cat_seq_t<typename commit_lead<Mn, Item>::type,
                typename commit_counted_tail<Item, Stop, Mx - Mn>::type>;
};

template <std::size_t Mn, std::size_t Mx, class P, class B>
  requires(Mn < Mx)
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
