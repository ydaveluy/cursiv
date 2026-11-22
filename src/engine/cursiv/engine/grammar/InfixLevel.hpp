// An infix with a prefix or postfix level, or in a left recursion, is built as
// its levels, tightest first, each reading the one below as its operand.
// Elsewhere, a run of binary levels stays one climb.
#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/Association.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/engine/grammar/LeadGraph.hpp"
#include "cursiv/engine/grammar/reached.hpp"
#include "cursiv/grammar/InGrammar.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/utils/TypeName.hpp"

namespace cursiv::engine {

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
struct unary_levels<detail::Levels<Ls...>> {
  static constexpr bool any = (detail::is_unary_level<Ls> || ...);
  static constexpr bool at[] = {detail::is_unary_level<Ls>...};
};

// The rule that reads level `K` of `I`: the primary below the first, `I` itself
// at the top. A climbed binary level is read by the run it ends.
template <class I, std::size_t K, bool Climb, bool Top = (K == I::levels::size)>
struct level_of {
  using type = detail::cond_t<Climb && !detail::is_unary_level<level_at<I, K>>, InfixRun<I, K>,
                      InfixLevel<I, K, Climb>>;
};
template <class I, bool Climb>
struct level_of<I, 0, Climb, false> {
  using type = detail::declared_of_t<typename I::primary>;
};
template <class I, std::size_t K, bool Climb>
struct level_of<I, K, Climb, true> {
  using type = detail::declared_of_t<I>;
};
template <class I, std::size_t K, bool Climb>
using level_t = typename level_of<I, K, Climb>::type;

// A level reads its operator next to an operand of its own level or of the
// one below, as its grouping says, else hands over the level below.
template <class I, std::size_t K, bool Climb, class L = level_at<I, K>,
          Association A = L::kGroup>
struct level_body {
  using Left = detail::cond_t<A == Association::Left, level_t<I, K, Climb>,
                      level_t<I, K - 1, Climb>>;
  using Right = detail::cond_t<A == Association::Right, level_t<I, K, Climb>,
                       level_t<I, K - 1, Climb>>;
  using type = detail::Choice<detail::Create<typename I::node_type,
                             detail::Seq<detail::Assign<I::left_pointer, detail::Ref<Left>>,
                                 detail::Assign<I::op_pointer, typename L::op_expr>,
                                 detail::Assign<I::right_pointer, detail::Ref<Right>>>>,
                      detail::Ref<level_t<I, K - 1, Climb>>>;
};
template <class I, std::size_t K, bool Climb, class L>
struct level_body<I, K, Climb, L, Association::Prefix> {
  using type =
      detail::Choice<detail::Create<typename L::node_type,
                    detail::Seq<detail::Assign<L::op_pointer, typename L::op_expr>,
                        detail::Assign<L::operand_pointer, detail::Ref<level_t<I, K, Climb>>>>>,
             detail::Ref<level_t<I, K - 1, Climb>>>;
};
template <class I, std::size_t K, bool Climb, class L>
struct level_body<I, K, Climb, L, Association::Postfix> {
  using type =
      detail::Choice<detail::Create<typename L::node_type,
                    detail::Seq<detail::Assign<L::operand_pointer, detail::Ref<level_t<I, K, Climb>>>,
                        detail::Assign<L::op_pointer, typename L::op_expr>>>,
             detail::Ref<level_t<I, K - 1, Climb>>>;
};

template <class I, std::size_t K, bool Climb>
struct InfixLevel : I {
  using kind = detail::node_kind;
  static constexpr auto body = typename level_body<I, K, Climb>::type{};
  static constexpr bool produced_body = true;
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
  using type = detail::Levels<typename Lvls::template at<From + Is>...>;
};
template <class I, std::size_t K>
struct InfixRun : I {
  using primary = level_t<I, run_floor<I, K>, true>;
  using levels = typename levels_slice<
      typename I::levels, run_floor<I, K>,
      std::make_index_sequence<K - run_floor<I, K>>>::type;
  static constexpr auto body = typename detail::infix_body<primary, levels>::type{};
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
          bool Infix = detail::is_infix_rule<typename S::template rule<R>>>
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

// The stage of `S` whose infix rules are read as their levels, asked of the
// lead graph of the rules `S` reaches.
template <class S, class Reached = reached_of<S>>
struct InfixStage : S {
  // A class of its own, so the rules keyed on the graph are named by the stage.
  struct graph : LeadGraph<S, Reached> {};
  template <class R>
  using rule = typename level_rule<S, graph, R>::type;
  template <class R>
  using body = typename detail::ov::stage_body<rule<R>>::type;
  using entry = rule<typename S::entry>;
};

}  // namespace cursiv::engine

namespace cursiv::detail {

template <class I, std::size_t K, bool Climb>
struct owner_grammar_of<engine::InfixLevel<I, K, Climb>> : owner_grammar_of<I> {
};
template <class I, std::size_t K>
struct owner_grammar_of<engine::InfixRun<I, K>> : owner_grammar_of<I> {};
template <class I, std::size_t K, bool Climb>
inline constexpr std::string_view
    kTypeNameView<engine::InfixLevel<I, K, Climb>> =
        type_name<declared_of_t<I>>();
template <class I, std::size_t K>
inline constexpr std::string_view kTypeNameView<engine::InfixRun<I, K>> =
    type_name<declared_of_t<I>>();

}  // namespace cursiv::detaildetail
