// The rows a rule names: its converter, its close, and what its body writes;
// an infix rule's climb and its three members. A grammar's rows, deduplicated,
// are the published list: an action's id is its position in it.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/engine/grammar/Distribute.hpp"
#include "cursiv/engine/grammar/InfixOp.hpp"
#include "cursiv/engine/grammar/Row.hpp"
#include "cursiv/engine/grammar/LowerDesc.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

template <class St, class E, class Children = typename detail::ExprShape<E>::children>
struct RowsOf;
template <class St, class E>
using rows_of_t = typename RowsOf<St, E>::type;
template <class St, class E, class... Ks>
struct RowsOf<St, E, TypeList<Ks...>> : detail::tl_cat<rows_of_t<St, Ks>...> {};
template <class St, class D, class E>
  requires detail::is_directive<D>
struct RowsOf<St, D, TypeList<E>>
    : detail::tl_cat<TypeList<typename directive_row<St, D>::type>, rows_of_t<St, E>> {
};

// What turns a span into a value, named by the rule that owns it: two rules
// returning one type may declare two converters.
template <class R>
struct ConvertOf {
  // The rule as written goes in the source slot: an override that restates
  // the converter is what the text path runs, and `declared_of_t` drops it.
  using row = Row<WriteKind::convert, detail::declared_of_t<R>, void, void, R>;
  using type = TypeList<row>;
};

// A rule may need a record of its own where its body states rows: the
// creation may sit deeper than the rule's own position.
template <class S, class R>
using body_rows_t =
    rows_of_t<S, normalize_writes_t<typename S::template body<R>>>;
// A close names the family the node it adopts must satisfy.
template <class RR>
using close_row_t = Row<WriteKind::close, detail::rule_type_t<RR>>;
template <class RR>
using close_rows_t =
    std::conditional_t<detail::is_node_rule<RR>, TypeList<close_row_t<RR>>, TypeList<>>;

template <class S, class R,
          bool Infix = detail::is_infix_rule<typename S::template rule<R>>>
struct RuleRows {
  using type =
      detail::tl_cat_t<typename ConvertOf<typename S::template rule<R>>::type,
               close_rows_t<typename S::template rule<R>>, body_rows_t<S, R>>;
};
// A binary operator writes the value of its own source.
template <class S, class T, class Ops>
struct op_rows;
template <class S, class T, class... Ops>
struct op_rows<S, T, TypeList<Ops...>> {
  using type = TypeList<owned_row<T::op_pointer, infix_src<S, Ops, T::op_pointer>>...>;
};
// A prefix or postfix level builds its own node and writes its own members.
template <class S, class T, class L, bool Unary = detail::is_unary_level<L>>
struct level_rows : op_rows<S, T, typename detail::alts_list<typename L::op_expr>::type> {};
template <class S, class T, class L>
struct level_rows<S, T, L, true> {
  using type = detail::tl_cat_t<
      TypeList<Row<WriteKind::climb, typename L::node_type>, owned_row<L::operand_pointer>>,
      typename op_rows<S, L, typename detail::alts_list<typename L::op_expr>::type>::type>;
};
template <class S, class T, class Lvls = typename T::levels>
struct infix_level_rows;
template <class S, class T, class... Ls>
struct infix_level_rows<S, T, detail::Levels<Ls...>>
    : detail::tl_cat<typename level_rows<S, T, Ls>::type...> {};

template <class S, class R>
struct RuleRows<S, R, true> {
  using T = typename S::template rule<R>;
  using type = detail::tl_cat_t<TypeList<Row<WriteKind::climb, typename T::node_type>,
                                 owned_row<T::left_pointer>>,
                        typename infix_level_rows<S, T>::type,
                        TypeList<owned_row<T::right_pointer>>>;
};

template <class S, class Rules>
struct AllRows;
template <class S, class... Rs>
struct AllRows<S, TypeList<Rs...>>
    : detail::dedupe<detail::tl_cat_t<typename RuleRows<S, Rs>::type...>> {};

}  // namespace cursiv::engine
