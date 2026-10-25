// Which rules can start one another before any token is consumed, and the
// components that closes: a rule in a cyclic one is left-recursive.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/StrongComponents.hpp"

namespace cursiv::detail {

// Only a repetition that may take no turn lets the next element start too.
template <class E>
struct lead_empty : std::false_type {};
template <std::size_t N, std::size_t M, class E>
struct lead_empty<Repeat<N, M, E>> : std::bool_constant<N == 0> {};

template <class E, class Kids = typename ExprShape<E>::kids>
struct lead_refs;
template <class E, class... Ks>
struct lead_refs<E, TypeList<Ks...>> : tl_cat<typename lead_refs<Ks>::type...> {
};
template <class R>
struct lead_refs<Ref<R>, TypeList<>> {
  using type = TypeList<declared_of_t<R>>;
};
template <class... Es>
struct lead_seq;
template <>
struct lead_seq<> {
  using type = TypeList<>;
};
template <class E, class... Es>
struct lead_seq<E, Es...> {
  using type = tl_cat_t<
      typename lead_refs<E>::type,
      cond_t<lead_empty<E>::value, typename lead_seq<Es...>::type, TypeList<>>>;
};
template <class... Es>
struct lead_refs<Seq<Es...>, TypeList<Es...>> : lead_seq<Es...> {};

template <class S, class R,
          bool Terminal = is_terminal_rule<typename S::template rule<R>>>
struct lead_rule_refs {
  using type = dedupe_t<typename lead_refs<typename S::template body<R>>::type>;
};
template <class S, class R>
struct lead_rule_refs<S, R, true> {
  using type = TypeList<>;
};

template <class S, class Rules>
struct LeadGraph;
template <class S, class... Rs>
struct LeadGraph<S, TypeList<Rs...>> {
  using rules = TypeList<Rs...>;
  using refs = tl_cat_t<typename lead_rule_refs<S, Rs>::type...>;
  template <class... Xs>
  static consteval auto edge_list(TypeList<Xs...>) {
    return std::array<std::uint32_t, sizeof...(Xs)>{
        std::uint32_t(index_of<Xs, rules>::value)...};
  }
  static consteval auto offset_list() {
    const std::array<std::uint32_t, sizeof...(Rs)> counts{
        std::uint32_t(tl_size<typename lead_rule_refs<S, Rs>::type>::value)...};
    std::array<std::uint32_t, sizeof...(Rs) + 1> result{};
    for (std::size_t i = 0; i < counts.size(); ++i)
      result[i + 1] = result[i] + counts[i];
    return result;
  }
  static constexpr auto strong =
      StrongComponents<sizeof...(Rs)>::build(offset_list(), edge_list(refs{}));
  template <class A>
  static constexpr std::size_t at = index_of<declared_of_t<A>, rules>::value;
  template <class A, class B>
  static constexpr bool same =
      at<A> < sizeof...(Rs) && at<B> < sizeof...(Rs) &&
      strong.component[at<A>] == strong.component[at<B>];
  template <class A>
  static constexpr bool cyclic = at<A> < sizeof...(Rs) && strong.cyclic[at<A>];
  template <std::uint32_t C, class L>
  struct keep;
  template <std::uint32_t C, class... Bs>
  struct keep<C, TypeList<Bs...>>
      : tl_cat<cond_t<strong.component[at<Bs>] == C, TypeList<Bs>,
                      TypeList<>>...> {};
  template <class A>
  using component = typename keep<strong.component[at<A>], rules>::type;
};

}  // namespace cursiv::detail
