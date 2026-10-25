// An infix that stands in a left recursion counts as its levels, tightest
// first: each level reads the one below it as its operand, and the primary is
// the level under all. A call of the infix is a call of its top level.
#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/shared/gen/LeadGraph.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"

namespace cursiv::detail {

template <class I, std::size_t K>
struct InfixLevel;

// The rule that reads level `K` of `I`: the primary below the first, `I` itself
// at the top.
template <class I, std::size_t K, bool Top = (K == I::levels::size)>
struct level_of {
  using type = InfixLevel<I, K>;
};
template <class I>
struct level_of<I, 0, false> {
  using type = declared_of_t<typename I::primary>;
};
template <class I, std::size_t K>
struct level_of<I, K, true> {
  using type = declared_of_t<I>;
};
template <class I, std::size_t K>
using level_t = typename level_of<I, K>::type;

template <class I, std::size_t K, class Left, class Right>
struct level_arms {
  using op = typename I::levels::template at<K - 1>::op_expr;
  using type = Choice<
      Create<typename I::node_type,
             Seq<Assign<I::left_pointer, Ref<Left>>, Assign<I::op_pointer, op>,
                 Assign<I::right_pointer, Ref<Right>>>>,
      Ref<level_t<I, K - 1>>>;
};
template <class I, std::size_t K,
          Association A = I::levels::template at<K - 1>::kGroup>
struct level_body
    : level_arms<I, K,
                 std::conditional_t<A == Association::left, level_t<I, K>,
                                    level_t<I, K - 1>>,
                 std::conditional_t<A == Association::right, level_t<I, K>,
                                    level_t<I, K - 1>>> {};

template <class I, std::size_t K>
struct InfixLevel : I {
  using kind = node_kind;
  using operator_infix = declared_of_t<I>;
  static constexpr std::size_t operator_level = K;
  static constexpr auto body = typename level_body<I, K>::type{};
  static constexpr bool minted_body = true;
};

template <class S, class G, class R, bool Levels = false>
struct level_pick {
  using type = typename S::template rule<R>;
};
template <class S, class G, class R>
struct level_pick<S, G, R, true> {
  using I = typename S::template rule<R>;
  struct type : InfixLevel<I, I::levels::size> {
    using declared = I;
    using inherited = InfixLevel<I, I::levels::size>;
  };
};
// Only an infix asks the graph, so a grammar with none never builds it.
template <class S, class G, class R,
          bool Infix = is_infix_rule<typename S::template rule<R>>>
struct level_wanted : std::false_type {};
template <class S, class G, class R>
struct level_wanted<S, G, R, true> : std::bool_constant<G::template cyclic<R>> {
};
template <class S, class G, class R>
struct level_rule : level_pick<S, G, R, level_wanted<S, G, R>::value> {};
template <class S, class G, class I, std::size_t K>
struct level_rule<S, G, InfixLevel<I, K>> {
  using type = InfixLevel<I, K>;
};

template <class S, class Rules = reached_t<S>>
struct InfixLevelStage : S {
  using graph = LeadGraph<S, Rules>;
  template <class R>
  using rule = typename level_rule<S, graph, R>::type;
  template <class R>
  using body = typename ov::stage_body<rule<R>>::type;
  using entry = rule<typename S::entry>;
};

template <class I, std::size_t K>
struct home_of<InfixLevel<I, K>> : home_of<I> {};
template <class I, std::size_t K>
inline constexpr std::string_view kTypeNameView<InfixLevel<I, K>> =
    type_name<declared_of_t<I>>();

}  // namespace cursiv::detail
