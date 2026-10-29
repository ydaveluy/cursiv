// An infix whose table holds a prefix or postfix level, or that stands in a
// left recursion, counts as its levels, tightest first: each level reads the
// one below it as its operand, and the primary is the level under all. A call
// of the infix is a call of its top level. Outside a left recursion, a run of
// binary levels stays one climb over the level below it.
#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/shared/gen/LeadGraph.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"

namespace cursiv::detail {

template <class I, std::size_t K, bool Climb = false>
struct InfixLevel;
template <class I, std::size_t K>
struct InfixRun;

// Level `K` of `I`, counted from 1.
template <class I, std::size_t K>
using level_at = typename I::levels::template at<K - 1>;

template <class Lvls>
struct unary_levels;
template <class... Ls>
struct unary_levels<Levels<Ls...>> {
  static constexpr bool any = (is_unary_level<Ls> || ...);
  static constexpr bool at[] = {is_unary_level<Ls>...};
};

// The rule that reads level `K` of `I`: the primary below the first, `I` itself
// at the top. A climbed binary level is read by the run it ends.
template <class I, std::size_t K, bool Climb, bool Top = (K == I::levels::size)>
struct level_of {
  using type = cond_t<Climb && !is_unary_level<level_at<I, K>>, InfixRun<I, K>,
                      InfixLevel<I, K, Climb>>;
};
template <class I, bool Climb>
struct level_of<I, 0, Climb, false> {
  using type = declared_of_t<typename I::primary>;
};
template <class I, std::size_t K, bool Climb>
struct level_of<I, K, Climb, true> {
  using type = declared_of_t<I>;
};
template <class I, std::size_t K, bool Climb>
using level_t = typename level_of<I, K, Climb>::type;

// A level reads its operator next to an operand of its own level or of the
// one below, as its grouping says, else hands over the level below.
template <class I, std::size_t K, bool Climb, class L = level_at<I, K>,
          Association A = L::kGroup>
struct level_body {
  using Left = cond_t<A == Association::left, level_t<I, K, Climb>,
                      level_t<I, K - 1, Climb>>;
  using Right = cond_t<A == Association::right, level_t<I, K, Climb>,
                       level_t<I, K - 1, Climb>>;
  using type = Choice<Create<typename I::node_type,
                             Seq<Assign<I::left_pointer, Ref<Left>>,
                                 Assign<I::op_pointer, typename L::op_expr>,
                                 Assign<I::right_pointer, Ref<Right>>>>,
                      Ref<level_t<I, K - 1, Climb>>>;
};
template <class I, std::size_t K, bool Climb, class L>
struct level_body<I, K, Climb, L, Association::prefix> {
  using type =
      Choice<Create<typename L::node_type,
                    Seq<Assign<L::op_pointer, typename L::op_expr>,
                        Assign<L::operand_pointer, Ref<level_t<I, K, Climb>>>>>,
             Ref<level_t<I, K - 1, Climb>>>;
};
template <class I, std::size_t K, bool Climb, class L>
struct level_body<I, K, Climb, L, Association::postfix> {
  using type =
      Choice<Create<typename L::node_type,
                    Seq<Assign<L::operand_pointer, Ref<level_t<I, K, Climb>>>,
                        Assign<L::op_pointer, typename L::op_expr>>>,
             Ref<level_t<I, K - 1, Climb>>>;
};

template <class I, std::size_t K, bool Climb>
struct InfixLevel : I {
  using kind = node_kind;
  using operator_infix = declared_of_t<I>;
  static constexpr std::size_t operator_level = K;
  static constexpr auto body = typename level_body<I, K, Climb>::type{};
  static constexpr bool minted_body = true;
};

// The binary levels `K` ends a run of, from the one above the nearest unary
// level below it, climbed as one table over that unary level.
template <class I, std::size_t K>
inline constexpr std::size_t run_floor = [] {
  std::size_t s = K - 1;
  while (s > 0 && !unary_levels<typename I::levels>::at[s - 1]) --s;
  return s;
}();
template <class Lvls, std::size_t From, class Is>
struct levels_slice;
template <class Lvls, std::size_t From, std::size_t... Is>
struct levels_slice<Lvls, From, std::index_sequence<Is...>> {
  using type = Levels<typename Lvls::template at<From + Is>...>;
};
template <class I, std::size_t K>
struct InfixRun : I {
  using primary = level_t<I, run_floor<I, K>, true>;
  using levels = typename levels_slice<
      typename I::levels, run_floor<I, K>,
      std::make_index_sequence<K - run_floor<I, K>>>::type;
  static constexpr auto body = typename infix_body<primary, levels>::type{};
};

template <class S, class G, class R, bool Levels = false>
struct level_pick {
  using type = typename S::template rule<R>;
};
template <class S, class G, class R>
struct level_pick<S, G, R, true> {
  using I = typename S::template rule<R>;
  using Top = typename level_of<I, I::levels::size, !G::template cyclic<R>,
                                false>::type;
  struct type : Top {
    using declared = I;
    using inherited = Top;
  };
};
// Only an infix asks the graph, so a grammar with none never builds it.
template <class S, class G, class R,
          bool Infix = is_infix_rule<typename S::template rule<R>>>
struct level_wanted : std::false_type {};
template <class S, class G, class R>
struct level_wanted<S, G, R, true>
    : std::bool_constant<
          unary_levels<typename S::template rule<R>::levels>::any ||
          G::template cyclic<R>> {};
template <class S, class G, class R>
struct level_rule : level_pick<S, G, R, level_wanted<S, G, R>::value> {};
template <class S, class G, class I, std::size_t K, bool Climb>
struct level_rule<S, G, InfixLevel<I, K, Climb>> {
  using type = InfixLevel<I, K, Climb>;
};
template <class S, class G, class I, std::size_t K>
struct level_rule<S, G, InfixRun<I, K>> {
  using type = InfixRun<I, K>;
};

template <class S, class Reached = reached_of<S>>
struct InfixLevelStage : S {
  // A class of its own, so the rules keyed on the graph are named by the stage.
  struct graph : LeadGraph<S, typename Reached::type> {};
  template <class R>
  using rule = typename level_rule<S, graph, R>::type;
  template <class R>
  using body = typename ov::stage_body<rule<R>>::type;
  using entry = rule<typename S::entry>;
};

template <class I, std::size_t K, bool Climb>
struct home_of<InfixLevel<I, K, Climb>> : home_of<I> {};
template <class I, std::size_t K>
struct home_of<InfixRun<I, K>> : home_of<I> {};
template <class I, std::size_t K, bool Climb>
inline constexpr std::string_view kTypeNameView<InfixLevel<I, K, Climb>> =
    type_name<declared_of_t<I>>();
template <class I, std::size_t K>
inline constexpr std::string_view kTypeNameView<InfixRun<I, K>> =
    type_name<declared_of_t<I>>();

}  // namespace cursiv::detail
