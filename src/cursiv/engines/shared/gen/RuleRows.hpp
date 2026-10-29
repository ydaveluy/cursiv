// The rows a rule names: its converter, its close, and what its body writes;
// an infix rule's climb and its three members. A grammar's rows, deduplicated,
// are the published list: an action's id is its position in it.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/Distribute.hpp"
#include "cursiv/engines/shared/gen/Row.hpp"
#include "cursiv/engines/shared/gen/spec_row.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv::detail {

template <class St, class E, class Kids = typename ExprShape<E>::kids>
struct RowsOf;
template <class St, class E>
using rows_of_t = typename RowsOf<St, E>::type;
template <class St, class E, class... Ks>
struct RowsOf<St, E, TypeList<Ks...>> : tl_cat<rows_of_t<St, Ks>...> {};
template <class St, class D, class E>
  requires is_directive<D>
struct RowsOf<St, D, TypeList<E>>
    : tl_cat<TypeList<typename directive_row<St, D>::type>, rows_of_t<St, E>> {
};

// What turns a span into a value, named by the rule that owns it: two rules
// returning one type may declare two converters.
template <class R>
struct ConvertOf {
  // The rule as written rides in the source slot: an override that restates
  // the converter is what the text path runs, and `declared_of_t` drops it.
  using row = Row<WriteKind::convert, declared_of_t<R>, void, void, R>;
  using type = TypeList<row>;
};

// A rule may need a record of its own where its body states rows: the
// creation may sit deeper than the rule's own position.
template <class S, class R>
using body_rows_t =
    rows_of_t<S, normalize_writes_t<typename S::template body<R>>>;
// A close names the family the node it adopts must satisfy.
template <class RR>
using close_row_t = Row<WriteKind::close, rule_type_t<RR>>;
template <class RR>
using close_rows_t =
    std::conditional_t<is_node_rule<RR>, TypeList<close_row_t<RR>>, TypeList<>>;

template <class S, class R, template <class, class, auto> class InfixSrc,
          bool Infix = is_infix_rule<typename S::template rule<R>>>
struct RuleRows {
  using type =
      tl_cat_t<typename ConvertOf<typename S::template rule<R>>::type,
               close_rows_t<typename S::template rule<R>>, body_rows_t<S, R>>;
};
// One `op` row per operator of a binary level: what it hands over rides its
// own converter. A unary level writes what its rule writes: its class, its
// operator and its operand.
template <class S, class T, template <class, class, auto> class InfixSrc,
          class L, bool Unary = is_unary_level<L>>
struct level_rows {
  template <class Ops>
  struct op_rows;
  template <class... Ops>
  struct op_rows<TypeList<Ops...>> {
    using type =
        TypeList<owned_row<T::op_pointer, InfixSrc<S, Ops, T::op_pointer>>...>;
  };
  using type =
      typename op_rows<typename alts_list<typename L::op_expr>::type>::type;
};
template <class S, class T, template <class, class, auto> class InfixSrc,
          class L>
struct level_rows<S, T, InfixSrc, L, true> {
  using type = rows_of_t<
      S, normalize_writes_t<
             Create<typename L::node_type,
                    Seq<Assign<L::op_pointer, typename L::op_expr>,
                        Assign<L::operand_pointer, Ref<declared_of_t<T>>>>>>>;
};
template <class S, class T, template <class, class, auto> class InfixSrc,
          class Lvls = typename T::levels>
struct infix_level_rows;
template <class S, class T, template <class, class, auto> class InfixSrc,
          class... Ls>
struct infix_level_rows<S, T, InfixSrc, Levels<Ls...>>
    : tl_cat<typename level_rows<S, T, InfixSrc, Ls>::type...> {};

template <class S, class R, template <class, class, auto> class InfixSrc>
struct RuleRows<S, R, InfixSrc, true> {
  using T = typename S::template rule<R>;
  using type = tl_cat_t<TypeList<Row<WriteKind::climb, typename T::node_type>,
                                 owned_row<T::left_pointer>>,
                        typename infix_level_rows<S, T, InfixSrc>::type,
                        TypeList<owned_row<T::right_pointer>>>;
};

template <class S, class Rules, template <class, class, auto> class InfixSrc>
struct AllRows;
template <class S, class... Rs, template <class, class, auto> class InfixSrc>
struct AllRows<S, TypeList<Rs...>, InfixSrc>
    : dedupe<tl_cat_t<typename RuleRows<S, Rs, InfixSrc>::type...>> {};

}  // namespace cursiv::detail
