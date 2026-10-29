// One write per single-valued member on any path of a rule body: `=`
// overwrites, so a second write loses the first, and a node loses its place in
// the tree while keeping the parent it was given.
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/utils/typelist.hpp"
#include "cursiv/engines/shared/gen/GrammarRefusals.hpp"

namespace cursiv::detail {

template <auto P>
struct wrote {};
template <auto P>
struct adopted_wrote {};
template <class T>
inline constexpr bool is_wrote = false;
template <auto P>
inline constexpr bool is_wrote<wrote<P>> = true;

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

// What one body does to its node: the single-valued members it writes -- the
// members of its directives, the member each nest buries into, and a climb's
// -- and the calls whose node becomes the node in hand, a call building a node
// wherever the node in hand stands. A payload is another node's, and not
// descended into; what such a call hands over is what its rule writes, which
// `rule_writes` gathers.
template <class E, class Kids = typename ExprShape<E>::kids>
struct node_effects {
  using writes = TypeList<>;
  using calls = TypeList<>;
};
template <class E, class... Ks>
  requires(grouping<E> || transparent<E>)
struct node_effects<E, TypeList<Ks...>> {
  using writes = tl_cat_t<typename node_effects<Ks>::writes...>;
  using calls = tl_cat_t<typename node_effects<Ks>::calls...>;
};
template <class T, class E>
struct node_effects<Create<T, E>, TypeList<E>> : node_effects<E> {};
template <class T, auto P, class E>
struct node_effects<Nest<T, P, E>, TypeList<E>> : node_effects<E> {
  using writes = tl_cat_t<TypeList<wrote<P>>, typename node_effects<E>::writes>;
};
template <auto S, class E>
struct node_effects<Assign<S, E>, TypeList<E>> {
  static constexpr auto P = spec_ptr<S>;
  using writes = cond_t<field_tag<P>::storage != FieldStorage::vector,
                        TypeList<wrote<P>>, TypeList<>>;
  using calls = TypeList<>;
};
template <auto S, class E>
struct node_effects<Append<S, E>, TypeList<E>> {
  using writes = TypeList<>;
  using calls = TypeList<>;
};
template <class R0>
struct node_effects<Ref<R0>, TypeList<>> {
  using R = named_rule_t<R0>;
  using writes = TypeList<>;
  using calls = cond_t<is_node_rule<R> && !is_grammar<R0>,
                       TypeList<declared_of_t<R>>, TypeList<>>;
};
template <class R0>
  requires is_infix_rule<named_rule_t<R0>>
struct node_effects<Ref<R0>, TypeList<>> {
  using writes = typename infix_writes<named_rule_t<R0>>::type;
  using calls = TypeList<>;
};
template <class St, class R>
using body_effects =
    node_effects<std::remove_cvref_t<typename St::template body<R>>>;
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
          bool Node = is_node_rule<R> && !is_grammar<R0>>
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

template <class L>
struct own_wrote;
template <class... Ts>
struct own_wrote<TypeList<Ts...>>
    : tl_cat<cond_t<is_wrote<Ts>, TypeList<Ts>, TypeList<>>...> {};

// `Seen` is what the node in hand has been written so far on this path; `seen`
// is what it has after `E`, and `fault` the first mistake `E` made. A create
// or a nest gives the region it holds a node of its own, and a lookahead or a
// leaf writes nothing.
template <class St, class E, class Seen>
struct writes_once {
  using seen = Seen;
  using fault = NoWriteFault;
};

template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool buries = false;
template <class E, class... Ks>
inline constexpr bool buries<E, TypeList<Ks...>> = (buries<Ks> || ...);
template <class T, auto P, class E>
inline constexpr bool buries<Nest<T, P, E>, TypeList<E>> = true;

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
  using seen =
      tl_cat_t<Seen,
               typename tl_minus<typename writes_once<St, As, Seen>::seen,
                                  Seen>::type...>;
  using fault = typename first_fault_of<
      typename writes_once<St, As, Seen>::fault...>::type;
};

template <class St, class L, class Seen>
struct unordered_writes;
template <class St, class L, class Seen, class Js>
struct unordered_write_paths;
template <class St, class... Es, class Seen, std::size_t... Js>
struct unordered_write_paths<St, TypeList<Es...>, Seen,
                             std::index_sequence<Js...>> {
  template <std::size_t I>
  struct path {
    using first = writes_once<St, pack_at_t<I, Es...>, Seen>;
    using rest = unordered_writes<St, unordered_rest_t<I, TypeList<Es...>>,
                                  typename first::seen>;
    using seen = typename rest::seen;
    using fault =
        typename first_fault_of<typename first::fault, typename rest::fault>::type;
  };
  using seen = dedupe_t<tl_cat_t<typename path<Js>::seen...>>;
  using fault = typename first_fault_of<typename path<Js>::fault...>::type;
};
template <class St, class... Es, class Seen>
struct unordered_writes<St, TypeList<Es...>, Seen>
    : cond_t<(buries<Es> || ...),
             unordered_write_paths<St, TypeList<Es...>, Seen,
                                   std::index_sequence_for<Es...>>,
             writes_once_seq<St, TypeList<Es...>, Seen, NoWriteFault>> {};
template <class St, class... Es, class Seen>
struct writes_once<St, Unordered<Es...>, Seen>
    : unordered_writes<St, TypeList<Es...>, Seen> {};

template <class L>
struct first_written {
  using type = NoWriteFault;
};
template <auto P, class... Ws>
struct first_written<TypeList<wrote<P>, Ws...>> {
  using type = field_tag<P>;
};

// A turn that writes a member of the node in hand writes it again on the next.
// A nest hands the next turn another node, so a body that buries is not judged.
template <class St, std::size_t Mn, std::size_t Mx, class E, class Seen>
struct writes_once<St, Repeat<Mn, Mx, E>, Seen> {
  using turn =
      typename own_wrote<typename writes_once<St, E, TypeList<>>::seen>::type;
  using inner = writes_once<St, E, Seen>;
  using again =
      cond_t<Mx != 1 && !buries<E> && tl_size<turn>::value != 0,
             WriteEachTurn<typename first_written<turn>::type>, NoWriteFault>;
  using seen = typename inner::seen;
  using fault = typename first_fault_of<typename inner::fault, again>::type;
};

template <class St, class T, class E, class Seen>
struct writes_once<St, Create<T, E>, Seen> {
  using seen = Seen;
  using fault = typename writes_once<St, E, TypeList<>>::fault;
};
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
