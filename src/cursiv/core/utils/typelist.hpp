#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/core/utils/macros.hpp"

// The type-list toolkit every layer shares. A conditional branch is lazy only
// when it is a class template-id whose `::type` is asked afterwards -- an
// alias, or a branch containing `typename X::type`, is instantiated on both
// sides regardless; dispatch on a bool parameter instead.

namespace cursiv {

template <class... Ts>
struct TypeList {};

namespace detail {

// False, but only once the template argument is known: a plain
// `static_assert(false)` fires at definition time.
template <class...>
inline constexpr bool dependent_false = false;

template <bool Yes, class T, class F>
using cond_t = std::conditional_t<Yes, T, F>;

template <std::size_t I, class... Ts>
using pack_at_t = CURSIV_PACK_ELEMENT(I, Ts...);

// Past sixteen lists, runs of sixteen are joined side by side: the depth is
// that of a tree, not of a chain -- a grammar concatenates thousands at once.
template <class... Ls>
struct tl_cat {
  using type = TypeList<>;
};
template <class... Ts>
struct tl_cat<TypeList<Ts...>> {
  using type = TypeList<Ts...>;
};
template <class... As, class... Bs>
struct tl_cat<TypeList<As...>, TypeList<Bs...>> {
  using type = TypeList<As..., Bs...>;
};
template <class... As, class... Bs, class... Rest>
  requires(sizeof...(Rest) < 15)
struct tl_cat<TypeList<As...>, TypeList<Bs...>, Rest...>
    : tl_cat<TypeList<As..., Bs...>, Rest...> {};
template <class... A0, class... A1, class... A2, class... A3, class... A4,
          class... A5, class... A6, class... A7, class... Rest>
  requires(sizeof...(Rest) < 9)
struct tl_cat<TypeList<A0...>, TypeList<A1...>, TypeList<A2...>,
              TypeList<A3...>, TypeList<A4...>, TypeList<A5...>,
              TypeList<A6...>, TypeList<A7...>, Rest...>
    : tl_cat<TypeList<A0..., A1..., A2..., A3..., A4..., A5..., A6..., A7...>,
             Rest...> {};

template <class... Ls>
struct tl_cat16;
template <class... A0, class... A1, class... A2, class... A3, class... A4,
          class... A5, class... A6, class... A7, class... A8, class... A9,
          class... AA, class... AB, class... AC, class... AD, class... AE,
          class... AF>
struct tl_cat16<
    TypeList<A0...>, TypeList<A1...>, TypeList<A2...>, TypeList<A3...>,
    TypeList<A4...>, TypeList<A5...>, TypeList<A6...>, TypeList<A7...>,
    TypeList<A8...>, TypeList<A9...>, TypeList<AA...>, TypeList<AB...>,
    TypeList<AC...>, TypeList<AD...>, TypeList<AE...>, TypeList<AF...>> {
  using type = TypeList<A0..., A1..., A2..., A3..., A4..., A5..., A6..., A7...,
                        A8..., A9..., AA..., AB..., AC..., AD..., AE..., AF...>;
};
// The index M names the padding, past the end of the M lists.
template <std::size_t K, std::size_t M, class Js, class... Ls>
struct tl_cat_run;
template <std::size_t K, std::size_t M, std::size_t... Js, class... Ls>
struct tl_cat_run<K, M, std::index_sequence<Js...>, Ls...>
    : tl_cat16<pack_at_t<(16 * K + Js < M ? 16 * K + Js : M), Ls...,
                         TypeList<>>...> {};
template <class Ks, class... Ls>
struct tl_cat_runs;
template <std::size_t... Ks, class... Ls>
struct tl_cat_runs<std::index_sequence<Ks...>, Ls...>
    : tl_cat<typename tl_cat_run<
          Ks, sizeof...(Ls), std::make_index_sequence<16>, Ls...>::type...> {};
template <class... Ls>
  requires(sizeof...(Ls) > 16)
struct tl_cat<Ls...>
    : tl_cat_runs<std::make_index_sequence<(sizeof...(Ls) + 15) / 16>, Ls...> {
};
template <class... Ls>
using tl_cat_t = typename tl_cat<Ls...>::type;

template <class T, class L>
struct tl_has;
// The class trait, not `is_same_v`: a variable gets a symbol per pair asked,
// and this is asked of every rule against every list it may be in.
template <class T, class... Ts>
struct tl_has<T, TypeList<Ts...>>
    : std::bool_constant<(std::is_same<T, Ts>::value || ...)> {};

// The elements of `L` that `Held` does not hold, in order.
template <class L, class Held>
struct tl_minus;
template <class... Ts, class Held>
struct tl_minus<TypeList<Ts...>, Held>
    : tl_cat<cond_t<tl_has<Ts, Held>::value, TypeList<>, TypeList<Ts>>...> {};

// First occurrence wins, so a rule that names an element another rule already
// named keeps the earlier id.
template <class L>
struct dedupe;

#if CURSIV_HAS_DEDUP_PACK

template <class... Ts>
struct dedupe<TypeList<Ts...>> {
  using type = TypeList<CURSIV_DEDUP_PACK(Ts...)...>;
};

#else

// Each element keeps its place when no earlier one is the same type: a type
// is told by the address of its own tag, so one constant evaluation per pack
// answers and no function is named after the pack.
template <class T>
inline constexpr char dedupe_tag = 0;
template <class... Ts>
struct dedupe_pack {
  static constexpr std::size_t n = sizeof...(Ts);
  struct Kept {
    std::size_t at[n == 0 ? 1 : n]{};
    std::size_t size = 0;
  };
  static constexpr Kept kept = [] {
    Kept k;
    if constexpr (n != 0) {
      constexpr const char* tags[] = {&dedupe_tag<Ts>...};
      for (std::size_t i = 0; i < n; ++i) {
        bool fresh = true;
        for (std::size_t j = 0; fresh && j < k.size; ++j)
          fresh = tags[k.at[j]] != tags[i];
        if (fresh) k.at[k.size++] = i;
      }
    }
    return k;
  }();
  template <std::size_t... K>
  static auto pick(std::index_sequence<K...>)
      -> TypeList<CURSIV_PACK_ELEMENT(kept.at[K], Ts...)...>;
  using type = decltype(pick(std::make_index_sequence<kept.size>{}));
};
// A primary template, not the specialization itself: g++ pays member
// templates of a partial specialization twice over.
template <class... Ts>
struct dedupe<TypeList<Ts...>> : dedupe_pack<Ts...> {};

#endif

template <class L>
using dedupe_t = typename dedupe<L>::type;

// O(1) type -> index: one overload resolution answers what a linear fold
// re-evaluates per query. The ellipsis overload is the miss sentinel,
// preserving the `value < tl_size` contract. Requires a duplicate-free list --
// duplicates make the `using ...::at` set ambiguous; every caller passes one.
template <std::size_t I, class T>
struct idx_leaf {
  static constexpr std::integral_constant<std::size_t, I> at(
      std::type_identity<T>);
};
template <class S, class L>
struct idx_tree;
template <std::size_t... Is, class... Ts>
struct idx_tree<std::index_sequence<Is...>, TypeList<Ts...>>
    : idx_leaf<Is, Ts>... {
  using idx_leaf<Is, Ts>::at...;
  static constexpr std::integral_constant<std::size_t, sizeof...(Ts)> at(...);
};
template <class T, class L>
struct index_of;
// Inherited, not declared: a static member of its own would put one symbol
// per instantiation in the table, and this one is asked with lists of
// hundreds -- the list is spelled in the name. `integral_constant`'s member
// is named by the answer, so lists sharing a position share one symbol.
template <class T, class... Ts>
struct index_of<T, TypeList<Ts...>>
    : decltype(idx_tree<std::index_sequence_for<Ts...>, TypeList<Ts...>>::at(
          std::type_identity<T>{})){};
template <std::size_t I, class L>
struct tl_at;
template <std::size_t I, class... Ts>
struct tl_at<I, TypeList<Ts...>> {
  using type = pack_at_t<I, Ts...>;
};
template <std::size_t I, class L>
using tl_at_t = typename tl_at<I, L>::type;

template <class L>
struct tl_size;
template <class... Ts>
struct tl_size<TypeList<Ts...>> {
  static constexpr std::size_t value = sizeof...(Ts);
};

}  // namespace detail
}  // namespace cursiv
