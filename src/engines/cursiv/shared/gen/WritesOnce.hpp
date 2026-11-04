// One write per single-valued member on any path of a rule body: `=`
// overwrites, so a second write loses the first, and a node loses its place in
// the tree while keeping the parent it was given.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/utils/typelist.hpp"
#include "cursiv/shared/gen/GrammarRefusals.hpp"

namespace cursiv::detail {

template <auto P>
struct wrote {};
template <auto P>
struct adopted_wrote {};

// What a path did wrong first: a member written again, written on every turn
// of a repetition, or written over the node a called rule built.
struct NoWriteFault {};
template <class Tag>
struct WriteAgain {};
template <class Tag>
struct WriteEachTurn {};
template <class Tag>
struct WriteOverAdopted {};

// The first of a pack that is a fault, without a recursion as deep as the
// pack is long: a choice may hold a thousand alternatives.
template <class... Fs>
struct first_fault_of {
  static constexpr std::size_t at = [] {
    constexpr bool clean[] = {std::is_same_v<Fs, NoWriteFault>..., false};
    std::size_t i = 0;
    while (clean[i]) ++i;
    return i;
  }();
  using type = pack_at_t<at, Fs..., NoWriteFault>;
};

// A climb's node has its three members written, or a unary level's two.
template <class L, bool Unary = is_unary_level<L>>
struct unary_writes {
  using type = TypeList<>;
};
template <class L>
struct unary_writes<L, true> {
  using type = TypeList<wrote<L::op_pointer>, wrote<L::operand_pointer>>;
};
template <class R, class Lvls = typename R::levels>
struct infix_writes;
template <class R, class... Ls>
struct infix_writes<R, Levels<Ls...>> {
  using type =
      dedupe_t<tl_cat_t<TypeList<wrote<R::left_pointer>, wrote<R::op_pointer>,
                                 wrote<R::right_pointer>>,
                        typename unary_writes<Ls>::type...>>;
};

// What can still be written on the node an expression returns, and the calls
// that can supply that node. `keeps` means some path leaves the incoming node
// in hand. A nest kills that prefix; a directive's operand is another context.
template <class E, class Kids = typename ExprShape<E>::kids>
struct node_effects {
  using writes = TypeList<>;
  using calls = TypeList<>;
  static constexpr bool keeps = true;
};
template <class E, class... Ks>
  requires transparent<E>
struct node_effects<E, TypeList<Ks...>> {
  using writes = tl_cat_t<typename node_effects<Ks>::writes...>;
  using calls = tl_cat_t<typename node_effects<Ks>::calls...>;
  static constexpr bool keeps = (node_effects<Ks>::keeps && ...);
};
template <class... Es>
struct effect_sequence {
  using writes = TypeList<>;
  using calls = TypeList<>;
  static constexpr bool keeps = true;
};
template <class E, class... Es>
struct effect_sequence<E, Es...> {
  using head = node_effects<E>;
  using tail = effect_sequence<Es...>;
  using writes = dedupe_t<tl_cat_t<
      cond_t<tail::keeps, typename head::writes, TypeList<>>,
      typename tail::writes>>;
  using calls = dedupe_t<tl_cat_t<
      cond_t<tail::keeps, typename head::calls, TypeList<>>,
      typename tail::calls>>;
  static constexpr bool keeps = head::keeps && tail::keeps;
};
template <class... Es>
struct node_effects<Seq<Es...>, TypeList<Es...>> : effect_sequence<Es...> {};
template <class... Es>
struct node_effects<Choice<Es...>, TypeList<Es...>> {
  using writes = dedupe_t<tl_cat_t<typename node_effects<Es>::writes...>>;
  using calls = dedupe_t<tl_cat_t<typename node_effects<Es>::calls...>>;
  static constexpr bool keeps = (node_effects<Es>::keeps || ...);
};
template <class... Es>
struct node_effects<Unordered<Es...>, TypeList<Es...>> {
  // Any item can stand last. The incoming node survives only if every item
  // has a path that keeps it.
  using writes = dedupe_t<tl_cat_t<typename node_effects<Es>::writes...>>;
  using calls = dedupe_t<tl_cat_t<typename node_effects<Es>::calls...>>;
  static constexpr bool keeps = (node_effects<Es>::keeps && ...);
};
template <std::size_t Mn, std::size_t Mx, class E>
struct node_effects<Repeat<Mn, Mx, E>, TypeList<E>> : node_effects<E> {
  static constexpr bool keeps = Mn == 0 || node_effects<E>::keeps;
};
template <class T, class E>
struct node_effects<Create<T, E>, TypeList<E>> : node_effects<E> {};
template <class T, auto P, class E>
struct node_effects<Nest<T, P, E>, TypeList<E>> : node_effects<E> {
  using writes = dedupe_t<tl_cat_t<
      cond_t<node_effects<E>::keeps, TypeList<wrote<P>>, TypeList<>>,
      typename node_effects<E>::writes>>;
  static constexpr bool keeps = false;
};
template <auto S, class E>
struct node_effects<Assign<S, E>, TypeList<E>> {
  static constexpr auto P = spec_ptr<S>;
  using writes = cond_t<field_tag<P>::storage != FieldStorage::vector,
                        TypeList<wrote<P>>, TypeList<>>;
  using calls = TypeList<>;
  static constexpr bool keeps = true;
};
template <auto S, class E>
struct node_effects<Append<S, E>, TypeList<E>> {
  using writes = TypeList<>;
  using calls = TypeList<>;
  static constexpr bool keeps = true;
};
template <auto P, class E>
struct node_effects<Flag<P, E>, TypeList<E>> : node_effects<E> {
  using writes = tl_cat_t<typename node_effects<E>::writes, TypeList<wrote<P>>>;
};
template <class R0>
struct node_effects<Ref<R0>, TypeList<>> {
  using R = named_rule_t<R0>;
  using writes = TypeList<>;
  using calls = cond_t<is_node_rule<R> && !is_grammar<R0>,
                       TypeList<declared_of_t<R>>, TypeList<>>;
  static constexpr bool keeps = !is_node_rule<R> || is_grammar<R0>;
};
template <class R0>
  requires is_infix_rule<named_rule_t<R0>>
struct node_effects<Ref<R0>, TypeList<>> {
  using writes = typename infix_writes<named_rule_t<R0>>::type;
  using calls = TypeList<declared_of_t<typename named_rule_t<R0>::primary>>;
  static constexpr bool keeps = false;
};
template <class St, class R,
          bool Infix = is_infix_rule<typename St::template rule<R>>>
struct body_effects
    : node_effects<std::remove_cvref_t<typename St::template body<R>>> {};
template <class St, class R>
struct body_effects<St, R, true>
    : node_effects<Ref<typename St::template rule<R>>> {};
template <class St>
struct writes_calls {
  template <class R>
  struct of : dedupe<typename body_effects<St, R>::calls> {};
};

// The single-valued members a rule's node can end up with written, over every
// path: what its body writes and what every rule whose node it can adopt
// writes, a cycle included. The rules come from the closure of those calls,
// so each rule has one answer whatever path asks for it.
template <class St, class Rs>
struct writes_of;
template <class St, class... Rs>
struct writes_of<St, TypeList<Rs...>>
    : dedupe<tl_cat_t<typename body_effects<St, Rs>::writes...>> {};
template <class St, class R>
struct rule_writes
    : writes_of<St, tl_cat_t<TypeList<R>,
                             typename reached_by<writes_calls<St>::template of,
                                                 R>::type>> {};
template <class St, class R0, class R = named_rule_t<R0>,
          bool Node = (is_node_rule<R> || is_infix_rule<R>) && !is_grammar<R0>>
struct call_writes {
  using type = typename node_effects<Ref<R0>>::writes;
};
template <class St, class R0, class R>
struct call_writes<St, R0, R, true> : rule_writes<St, declared_of_t<R>> {};

template <class L>
struct as_adopted;
template <auto... Ps>
struct as_adopted<TypeList<wrote<Ps>...>> {
  using type = TypeList<adopted_wrote<Ps>...>;
};

// `Seen` holds writes possible on the node in hand; `seen` holds those possible
// on the node left by E. Per-member unions suffice: any duplicate concerns one
// member, so choices need no product of whole-node states.
template <class St, class E, class Seen>
struct writes_once {
  using seen = Seen;
  using fault = NoWriteFault;
};

template <class St, class Es, class Seen, class Fault>
struct writes_once_seq {
  using seen = Seen;
  using fault = Fault;
};
template <class St, class E0, class... Es, class Seen, class Fault>
struct writes_once_seq<St, TypeList<E0, Es...>, Seen, Fault> {
  using step = writes_once<St, E0, Seen>;
  using rest =
      writes_once_seq<St, TypeList<Es...>, typename step::seen,
                      typename first_fault_of<Fault, typename step::fault>::type>;
  using seen = typename rest::seen;
  using fault = typename rest::fault;
};

template <class St, class... Es, class Seen>
struct writes_once<St, Seq<Es...>, Seen>
    : writes_once_seq<St, TypeList<Es...>, Seen, NoWriteFault> {};
// Each branch is a path of its own; what follows has met any of them.
template <class St, class... As, class Seen>
struct writes_once<St, Choice<As...>, Seen> {
  using seen = dedupe_t<tl_cat_t<typename writes_once<St, As, Seen>::seen...>>;
  using fault = typename first_fault_of<
      typename writes_once<St, As, Seen>::fault...>::type;
};

// An offending pair of writes is either within one item or crosses two
// adjacent items. Every ordered pair can be adjacent in an unordered group;
// checking them avoids enumerating all permutations of its other items.
template <class St, class L, class Seen, class Js>
struct unordered_writes;
template <class St, class... Es, class Seen, std::size_t... Js>
struct unordered_writes<St, TypeList<Es...>, Seen,
                        std::index_sequence<Js...>> {
  template <std::size_t I>
  struct after {
    using item = writes_once<St, pack_at_t<I, Es...>, TypeList<>>;
    template <std::size_t J>
    using fault = cond_t<I == J, NoWriteFault,
                        typename writes_once<St, pack_at_t<J, Es...>,
                                             typename item::seen>::fault>;
    using type = typename first_fault_of<fault<Js>...>::type;
  };
  using seen = dedupe_t<tl_cat_t<
      cond_t<(node_effects<Es>::keeps && ...), Seen, TypeList<>>,
      typename writes_once<St, Es, TypeList<>>::seen...>>;
  using fault = typename first_fault_of<
      typename writes_once<St, Es, Seen>::fault...,
      typename after<Js>::type...>::type;
};
template <class St, class... Es, class Seen>
struct writes_once<St, Unordered<Es...>, Seen>
    : unordered_writes<St, TypeList<Es...>, Seen,
                        std::index_sequence_for<Es...>> {};

template <class Fault>
struct repeated_write {
  using type = Fault;
};
template <class Tag>
struct repeated_write<WriteAgain<Tag>> {
  using type = WriteEachTurn<Tag>;
};
template <class St, class E, class Seen, bool Repeats>
struct next_turn {
  using type = NoWriteFault;
};
template <class St, class E, class Seen>
struct next_turn<St, E, Seen, true>
    : repeated_write<typename writes_once<St, E, Seen>::fault> {};

// A node shared by two turns is the preceding turn's result. Two turns cover
// every collision: a later one can only keep this node or replace it with a
// nest, whose writes are already independent of the incoming state.
template <class St, std::size_t Mn, std::size_t Mx, class E, class Seen>
struct writes_once<St, Repeat<Mn, Mx, E>, Seen> {
  using inner = writes_once<St, E, Seen>;
  using again = typename next_turn<St, E, typename inner::seen, Mx != 1>::type;
  using seen = dedupe_t<tl_cat_t<cond_t<Mn == 0, Seen, TypeList<>>,
                                 typename inner::seen>>;
  using fault = typename first_fault_of<typename inner::fault, again>::type;
};

template <class St, class T, class E, class Seen>
struct writes_once<St, Create<T, E>, Seen> : writes_once<St, E, Seen> {};
template <class St, class T, auto P, class E, class Seen>
struct writes_once<St, Nest<T, P, E>, Seen>
    : writes_once<St, E, TypeList<wrote<P>>> {};

// A directive's operand builds a node apart. One that builds it in place, in
// a creation, is walked here on that node; a rule of its own is checked as the
// rule it is.
template <class St, class E, bool InPlace = holds_directive<E>>
struct operand_fault {
  using type = NoWriteFault;
};
template <class St, class E>
struct operand_fault<St, E, true> {
  using type = typename writes_once<St, E, TypeList<>>::fault;
};

// A directive writes the node in hand.
template <class St, auto S, class E, class Seen>
struct writes_once<St, Assign<S, E>, Seen> {
  static constexpr auto P = spec_ptr<S>;
  static constexpr bool single = field_tag<P>::storage != FieldStorage::vector;
  using added = cond_t<single, TypeList<wrote<P>>, TypeList<>>;
  using seen = tl_cat_t<Seen, added>;
  using fault = typename first_fault_of<
      cond_t<single && tl_has<wrote<P>, Seen>::value, WriteAgain<field_tag<P>>,
             cond_t<single && tl_has<adopted_wrote<P>, Seen>::value,
                    WriteOverAdopted<field_tag<P>>, NoWriteFault>>,
      typename operand_fault<St, E>::type>::type;
};

template <class St, auto S, class E, class Seen>
struct writes_once<St, Append<S, E>, Seen> {
  using seen = Seen;
  using fault = typename operand_fault<St, E>::type;
};

// A flag stays in its operand's construction context and writes the node the
// operand leaves in hand, after any adoption or nest it performed.
template <class St, auto P, class E, class Seen>
struct writes_once<St, Flag<P, E>, Seen> {
  using inner = writes_once<St, E, Seen>;
  using write = writes_once<St, Assign<P, Epsilon>, typename inner::seen>;
  using seen = typename write::seen;
  using fault = typename first_fault_of<typename inner::fault,
                                        typename write::fault>::type;
};

// A call adopts the node it returns: the members that node's rule may have
// written are written already.
template <class St, class R0, class Seen>
struct writes_once<St, Ref<R0>, Seen> {
  using seen = tl_cat_t<
      Seen, typename as_adopted<typename call_writes<St, R0>::type>::type>;
  using fault = NoWriteFault;
};
template <class St, class E, class Seen>
  requires transparent<E>
struct writes_once<St, E, Seen> : writes_once<St, through_t<E>, Seen> {};

template <class R, class Fault>
inline constexpr bool written_once = true;
template <class R, class Tag>
inline constexpr bool written_once<R, WriteAgain<Tag>> =
    ::WrittenAgain<declared_of_t<R>, Tag::ptr>;
template <class R, class Tag>
inline constexpr bool written_once<R, WriteEachTurn<Tag>> =
    ::WrittenEachTurn<declared_of_t<R>, Tag::ptr>;
template <class R, class Tag>
inline constexpr bool written_once<R, WriteOverAdopted<Tag>> =
    ::WrittenOverAdopted<declared_of_t<R>, Tag::ptr>;

// A rule body walked, its message fired on instantiation.
template <class St, class R, class Body>
inline constexpr bool writes_once_ok =
    written_once<R, typename writes_once<St, Body, TypeList<>>::fault>;

}  // namespace cursiv::detail
