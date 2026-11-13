// The stages a declared grammar goes through before an engine reads it, and
// `Normalize`, which is all of them. Each stage takes the one before and
// answers per rule, from `entry` down.
#pragma once

#include <cstddef>
#include <type_traits>

#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::detail {
namespace ov {

// The grammars a grammar extends; empty at the root.
template <class G>
struct parents_of {
  using type = TypeList<>;
};
template <class G>
  requires requires { typename G::parents; }
struct parents_of<G> {
  using type = typename G::parents;
};

// A merely-declared `override<R>` is incomplete, so the answer is "no".
template <class G, class R>
concept HasOverride = requires { typename G::template override<R>::kind; };

template <class G, class R>
struct overrides_rule_of;
template <class L, class R>
inline constexpr bool parents_override = false;
template <class... Ps, class R>
inline constexpr bool parents_override<TypeList<Ps...>, R> =
    (overrides_rule_of<Ps, R>::value || ...);
template <class G, class R>
struct overrides_rule_of
    : std::bool_constant<HasOverride<G, R> ||
                         parents_override<typename parents_of<G>::type, R>> {};
template <class G, class R>
inline constexpr bool overrides_rule = overrides_rule_of<G, R>::value;

// The nearest declaration: it carries the slot and the converter.
template <class G, class R, bool Here = HasOverride<G, R>>
struct override_decl;

// The overriding parents, one per declaration they reach: the two sides of a
// diamond whose common ancestor overrides reach one override, not two.
template <class R, class Ws, class Decls = TypeList<>>
struct one_per_decl {
  using type = TypeList<>;
};
template <class R, class W, class... Ws, class Decls>
struct one_per_decl<R, TypeList<W, Ws...>, Decls> {
  using D = typename override_decl<W, R>::type;
  using rest = typename one_per_decl<R, TypeList<Ws...>,
                                     tl_cat_t<Decls, TypeList<D>>>::type;
  using type =
      cond_t<tl_has<D, Decls>::value, rest, tl_cat_t<TypeList<W>, rest>>;
};

// The parents that override, one per declaration.
template <class R, class L>
struct choose_parent {
  using winners = TypeList<>;
};
template <class R, class... Ps>
struct choose_parent<R, TypeList<Ps...>> {
  using winners = typename one_per_decl<
      R, tl_cat_t<cond_t<overrides_rule<Ps, R>, TypeList<Ps>, TypeList<>>...>>::
      type;
};
template <class R, class G>
using winners_t =
    typename choose_parent<R, typename parents_of<G>::type>::winners;

// The one parent whose rule applies when `G` does not override it; two are
// refused.
template <class R, class G>
struct standing_parent {
  using type = winners_t<R, G>;
  static_assert(tl_size<type>::value <= 1,
                "two parent grammars override the same rule: override it "
                "again in this grammar to choose one");
};
template <class R, class G>
using standing_t = typename standing_parent<R, G>::type;

// A set flattens as its operator does: `a | b | z` is one choice of three, so
// a choice spliced into a choice joins it rather than nesting.
template <template <class...> class Sh, class... Es>
struct flat_of;
template <template <class...> class Sh, class E>
struct flat_of<Sh, E> {
  using type = E;
};
template <template <class...> class Sh, class A, class B, class... Rest>
struct flat_of<Sh, A, B, Rest...>
    : flat_of<Sh, typename FlatCat<Sh, A, B>::type, Rest...> {};
template <template <class...> class Sh, class... Es>
using flat_of_t = typename flat_of<Sh, Es...>::type;

// The splice, the only walk that rebuilds a body: `super` becomes the body
// the parents made effective, and every other node is rebuilt over its
// children.
template <class B, class E, class Children = typename ExprShape<E>::children>
struct plug;
template <class B, class E>
using plug_t = typename plug<B, E>::type;
template <class B, class E, class... Ks>
struct plug<B, E, TypeList<Ks...>> {
  using type = typename ExprShape<E>::template rebind<plug_t<B, Ks>...>;
};
template <class B, class D>
struct plug<B, SuperT<D>, TypeList<>> {
  using type = typename B::type;
};
// A sequence and a choice rebuild through their own operator, which
// flattens. An unordered group does not: a group spliced in as one of its
// items stays one item.
template <class B, class... Es>
struct plug<B, Seq<Es...>, TypeList<Es...>> {
  using type = flat_seq_t<plug_t<B, Es>...>;
};
template <class B, class... Es>
struct plug<B, Choice<Es...>, TypeList<Es...>> {
  using type = flat_of_t<Choice, plug_t<B, Es>...>;
};

// On a bool, never a `conditional_t`: the absent branch would instantiate
// `override<R>` on a grammar that has none.
template <class G, class R, bool Has = HasOverride<G, R>>
struct eff_body;
template <class G, class R>
using eff_body_t = typename eff_body<G, R>::type;

// What the parents made effective; the declared body at the root.
template <class R, class W>
struct parent_body {
  using type = std::remove_cv_t<decltype(R::body)>;
};
template <class R, class P>
struct parent_body<R, TypeList<P>> {
  using type = eff_body_t<P, R>;
};

// Not overridden here: the parents' effective body applies.
template <class G, class R, bool Has>
struct eff_body {
  using type = typename parent_body<R, standing_t<R, G>>::type;
};
// What `super` stands for in `G`'s override: read only where one is spelled,
// so a body without it needs no choice between two parents.
template <class G, class R>
struct super_body {
  static_assert(tl_size<winners_t<R, G>>::value <= 1,
                "two parent grammars override this rule, so `super` in the "
                "override here names neither: write the body without it");
  using type = typename parent_body<R, winners_t<R, G>>::type;
};
// Overridden: the override's body, with `super` replaced by the previous body.
template <class G, class R>
struct eff_body<G, R, true> {
  using type =
      plug_t<super_body<G, R>,
             std::remove_cv_t<decltype(G::template override<R>::body)>>;
};

template <class W, class R>
struct override_decl_up;
template <class P, class R>
struct override_decl_up<TypeList<P>, R> : override_decl<P, R> {};
template <class G, class R, bool Here>
struct override_decl : override_decl_up<standing_t<R, G>, R> {};
template <class G, class R>
struct override_decl<G, R, true> {
  using type = typename G::template override<R>;
  using owner = G;
};

// An override keeps the rule's name: `declared` is the base rule, the rest is
// the override's. `I` is the rule it replaces, used for a slot it leaves empty.
template <class R, class O, auto B, class I>
struct Overridden : O {
  using declared = R;
  using inherited = I;
  static constexpr auto body = B;
  static_assert(std::is_same_v<typename O::kind, typename R::kind>,
                "an override must keep the kind of the rule it replaces: "
                "override a terminal with a terminal<>, a rule with a "
                "rule<>, and an infix rule with an infix<>");
};

template <class G, class R, bool Ov = overrides_rule<G, R>>
struct eff_rule {
  using type = R;
};
// What the parents of the declaring grammar made effective.
template <class R, class W>
struct parent_rule {
  using type = R;
};
template <class R, class P>
struct parent_rule<R, TypeList<P>> {
  using type = typename eff_rule<P, R>::type;
};
// Overridden over several parents, it replaces the first one's rule, whose
// converter must match the others' where the override states none.
template <class R, class P, class Q, class... Ps>
struct parent_rule<R, TypeList<P, Q, Ps...>> {
  using type = typename eff_rule<P, R>::type;
};
template <class R, class Ws>
struct slots_agree;
template <class G, class R>
struct eff_rule<G, R, true> {
  using D = override_decl<G, R>;
  using W = winners_t<R, typename D::owner>;
  static_assert(!slot_empty<typename D::type> || slots_agree<R, W>::value,
                "two parent grammars give this rule different converters: "
                "state one in the override here");
  using type = Overridden<R, typename D::type, eff_body_t<G, R>{},
                          typename parent_rule<R, W>::type>;
};

// The declaration whose slot a rule's converter comes from: the nearest
// override that fills its slot, void where none does.
template <class R>
struct slot_decl_of {
  using type = void;
};
template <class R, class O, auto B, class I>
struct slot_decl_of<Overridden<R, O, B, I>>
    : cond_t<slot_empty<O>, slot_decl_of<I>, std::type_identity<O>> {};
template <class R>
using slot_decl_t = typename slot_decl_of<R>::type;

template <class R, class Ws>
struct slots_agree : std::true_type {};
template <class R, class P, class... Ps>
struct slots_agree<R, TypeList<P, Ps...>>
    : std::bool_constant<(
          std::is_same_v<slot_decl_t<typename eff_rule<P, R>::type>,
                         slot_decl_t<typename eff_rule<Ps, R>::type>> &&
          ...)> {};

template <class X, bool Node = std::is_same_v<typename X::kind, node_kind>>
struct declared_stage_body {
  using type = rule_body_t<X>;
};
template <class X>
struct declared_stage_body<X, true> {
  using type = unnamed_data_operands_t<rule_body_t<X>>;
};

// A body a stage produced (`produced_body`) is read as it stands; only a
// declared body has its root create removed.
template <class X>
struct stage_body {
  using type = typename declared_stage_body<X>::type;
};
template <class X>
  requires requires { X::produced_body; }
struct stage_body<X> {
  using type = std::remove_cv_t<decltype(X::body)>;
};

// `ignored` and `hidden` each accept a rule or a list and resolve like any
// rule.
template <class G, class X>
struct as_rules {
  using type = TypeList<typename eff_rule<G, X>::type>;
};
template <class G, class... Ts>
struct as_rules<G, TypeList<Ts...>> {
  using type = TypeList<typename eff_rule<G, Ts>::type...>;
};

// The class a grammar declares as its word boundary; `w` by default.
template <class WB>
struct declared_bound {
  static_assert(
      sizeof(WB) == 0,
      "a grammar's `word_boundary` must be a character class, such as "
      R"(`static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;`, )"
      R"(`uc::XID_Continue`, or `cr<"a-zA-Z0-9_$"> | uc::XID_Continue`)");
};
template <std::uint64_t M, bool N, str S>
struct declared_bound<CpSet<M, N, S>> {
  using type = CpSet<M, N, S>;
};
template <class... Es>
struct declared_bound<Choice<Es...>> {
  static_assert(sizeof...(Es) == 0,
                "a grammar's `word_boundary` must be one character class, "
                "and this is a choice of other expressions: join classes "
                R"(only, with `|`, as in `cr<"a-z"> | cr<"_">` or )"
                R"(`uc::XID_Continue | cr<"_$">`)");
};
template <class G>
consteval auto bound_of() {
  if constexpr (requires { G::word_boundary; }) {
    return typename declared_bound<
        std::remove_cvref_t<decltype(G::word_boundary)>>::type{};
  } else {
    // Two parents hide the name only by ambiguity: every `GrammarSpec`
    // declares one.
    static_assert(tl_size<typename parents_of<G>::type>::value < 2,
                  "two parent grammars declare different `word_boundary` "
                  "classes: declare `word_boundary` in this grammar to choose "
                  "one");
    return std::remove_cvref_t<decltype(GrammarSpec::w)>{};
  }
}

// Whether a grammar declares its literals case-insensitive; no by default.
template <class G>
consteval bool case_of() {
  if constexpr (requires { G::case_insensitive; }) {
    return G::case_insensitive;
  } else {
    static_assert(tl_size<typename parents_of<G>::type>::value < 2,
                  "two parent grammars declare different `case_insensitive` "
                  "values: declare `case_insensitive` in this grammar to "
                  "choose one");
    return false;
  }
}

}  // namespace ov

// First stage: the rule a reference names in `G`, the declared one unless an
// override replaces it.
template <class G>
struct ResolveOverride {
  // Whether this grammar, or one it extends, overrides `R`.
  template <class R>
  static constexpr bool overrides = ov::overrides_rule<G, R>;

  // The rule a reference to `R` actually names here.
  template <class R>
  using rule = typename ov::eff_rule<G, R>::type;

  // That rule's body, with `super` replaced and a root `create` removed.
  template <class R>
  using body = typename ov::stage_body<rule<R>>::type;

  // Every stage republishes the whole surface: the next stage has no other
  // way to reach it.
  using entry = rule<typename G::entry>;
  using ignored = typename ov::as_rules<G, typename G::ignored>::type;
  using hidden = typename ov::as_rules<G, typename G::hidden>::type;
  static constexpr auto word_boundary = ov::bound_of<G>();
  static constexpr bool case_insensitive = ov::case_of<G>();
};

// Whether grammar G folds the case of literal `S`: G is case-insensitive and
// `S` holds a letter with another case.
template <class G, str S>
consteval bool folds_case() {
  if constexpr (ResolveOverride<G>::case_insensitive)
    return holds_a_letter<S>();
  else
    return false;
}

// A node rule's creation is either spelled in every alternative or carried by
// the rule's own element. Two shapes, one meaning; this leaves only the first.
namespace mat {

// What one expression is to the head of its alternative, in one walk:
//   heads    it gives the head on every path: a creation, or an adopted call
//   may      a head appears on some path
//   adopts   an adopted call appears on some path
//   writes   every path writes a member, so the node a nest wraps exists
//   touches  some path writes, creates or nests
//   wraps    it makes a new current node, so the head stops in front of it
//   inside   a second head can appear on one path
//   late     a call is adopted where the current node is not empty
// The default is none of them.
struct blank {
  static constexpr bool heads = false, may = false, adopts = false,
                        writes = false, touches = false, wraps = false,
                        inside = false, late = false;
};
template <class E>
struct look : blank {};

// A create absorbs what surrounds it, so a second head ends up inside it.
template <class T, class E>
struct look<Create<T, E>> : blank {
  static constexpr bool heads = true, may = true, touches = true,
                        inside = look<E>::may;
};

template <auto Sp, class E>
struct look<Assign<Sp, E>> : blank {
  static constexpr bool writes = true, touches = true;
};
template <auto Sp, class E>
struct look<Append<Sp, E>> : blank {
  static constexpr bool writes = true, touches = true;
};
template <auto P, class E>
struct look<Flag<P, E>> : look<E> {
  static constexpr bool writes = true, touches = true;
};

// The head comes from the callee and the rule adopts it.
template <class R>
struct look_ref : blank {
  static constexpr bool heads = is_node_rule<R> || is_infix_rule<R>;
  static constexpr bool may = heads, adopts = heads;
};
template <class R>
struct look<Ref<R>> : look_ref<named_rule_t<R>> {};

// A nest wraps the current node: it heads nothing, what follows is the
// wrapper's, and a head inside it replaces the wrapper.
template <class T, auto P, class E>
struct look<Nest<T, P, E>> : blank {
  static constexpr bool wraps = true, touches = true, inside = look<E>::may;
};

// Elements in order: a call is late once something before it touched the
// node.
template <bool Dirty, class L>
inline constexpr bool late_in = false;
template <bool Dirty, class E, class... Es>
inline constexpr bool late_in<Dirty, TypeList<E, Es...>> =
    (look<E>::adopts && Dirty) || look<E>::late ||
    late_in<Dirty || look<E>::touches, TypeList<Es...>>;

// A sequence heads with its head region, wherever the algebra put it.
template <class... Es>
struct look<Seq<Es...>> {
  static constexpr bool heads = (look<Es>::heads || ...);
  static constexpr bool may = (look<Es>::may || ...);
  static constexpr bool adopts = (look<Es>::adopts || ...);
  static constexpr bool writes = (look<Es>::writes || ...);
  static constexpr bool touches = (look<Es>::touches || ...);
  static constexpr bool wraps = (look<Es>::wraps || ...);
  static constexpr bool inside =
      (look<Es>::inside || ...) ||
      (0u + ... + static_cast<unsigned>(look<Es>::may)) > 1u;
  static constexpr bool late = late_in<false, TypeList<Es...>>;
};
template <class... Es>
struct look<Unordered<Es...>> : look<Seq<Es...>> {
  static constexpr unsigned headOperands =
      (0u + ... + static_cast<unsigned>(look<Es>::may));
  static constexpr unsigned wrapOperands =
      (0u + ... + static_cast<unsigned>(look<Es>::wraps));
  static constexpr bool inside =
      (look<Es>::inside || ...) || headOperands > 1u ||
      ((look<Es>::may &&
        wrapOperands > static_cast<unsigned>(look<Es>::wraps)) ||
       ...);
  static constexpr bool late =
      (look<Es>::late || ...) ||
      ((look<Es>::touches || ...) && (look<Es>::adopts || ...));
};

// A choice gives the head only if every branch does -- but one branch that
// creates is a second head all the same.
template <class... As>
struct look<Choice<As...>> {
  static constexpr bool heads = (look<As>::heads && ...);
  static constexpr bool may = (look<As>::may || ...);
  static constexpr bool adopts = (look<As>::adopts || ...);
  static constexpr bool writes = (look<As>::writes && ...);
  static constexpr bool touches = (look<As>::touches || ...);
  static constexpr bool wraps = (look<As>::wraps || ...);
  static constexpr bool inside = (look<As>::inside || ...);
  static constexpr bool late = (look<As>::late || ...);
};

// One that may match nothing heads nothing, and one that can repeat a head
// has a second. Either way each iteration wraps again.
template <std::size_t Mn, std::size_t Mx, class E>
struct look<Repeat<Mn, Mx, E>> : look<E> {
  static constexpr bool heads = Mn >= 1 && look<E>::heads;
  static constexpr bool writes = Mn >= 1 && look<E>::writes;
  static constexpr bool inside = look<E>::inside || (Mx > 1 && look<E>::may);
  static constexpr bool late =
      look<E>::late || (Mx > 1 && look<E>::touches && look<E>::adopts);
};

template <class I, class H, class E>
struct look<Skip<I, H, E>> : look<E> {};
template <class G, class E>
struct look<Embed<G, E>> : look<E> {};

// Whether E gives the head on every path.
template <class E>
inline constexpr bool look_heads = look<E>::heads;

// An alternative is one sequence, or one part that is not one.
template <class A>
struct alt_parts {
  using type = TypeList<A>;
};
template <class... Es>
struct alt_parts<Seq<Es...>> {
  using type = TypeList<Es...>;
};

// The creation claims only what stands before the first wrapper, as a spelled
// create does.
template <bool Stop, class Acc, class Rest>
struct cut_step;
template <class Acc, class Rest>
struct cut;
template <class... Ps>
struct cut<TypeList<Ps...>, TypeList<>> {
  using head = TypeList<Ps...>;
  using rest = TypeList<>;
};
template <class... Ps, class E, class... Rs>
struct cut<TypeList<Ps...>, TypeList<E, Rs...>>
    : cut_step<look<E>::wraps, TypeList<Ps...>, TypeList<E, Rs...>> {};
template <class... Ps, class E, class... Rs>
struct cut_step<true, TypeList<Ps...>, TypeList<E, Rs...>> {
  using head = TypeList<Ps...>;
  using rest = TypeList<E, Rs...>;
};
template <class... Ps, class E, class... Rs>
struct cut_step<false, TypeList<Ps...>, TypeList<E, Rs...>>
    : cut<TypeList<Ps..., E>, TypeList<Rs...>> {};

template <class A>
using parts = cut<TypeList<>, typename alt_parts<A>::type>;

// The head the parts imply is the most derived class declaring the members
// they write; the rule's class stands where they write nothing.
template <class E, class RT,
          bool Mute = std::is_same_v<typename cands<E>::type, TypeList<>> &&
                      std::is_same_v<typename adopt_cands<E, TypeList<>>::type,
                                     TypeList<>>>
struct head_type {
  using type = typename branch_type<E, TypeList<>>::type;
};
// Writes nothing, adopts nothing: it names no class, so the rule's class
// applies, as a nest after it still needs a node to wrap.
template <class E, class RT>
struct head_type<E, RT, true> {
  using type = RT;
};

// The root create that was removed: it tells `create<W> + some(nest<..>)`,
// which has a node to wrap, from `nest<..>` alone.
template <class B>
struct root_create_class {
  using type = void;
};
template <class T, class E>
struct root_create_class<Create<T, E>> {
  using type = T;
};

// One alternative, given the head it needs. Parts in front of the wrapper
// are claimed by the creation; with none, a spelled head claims the whole
// alternative; otherwise it is left as it is, and `checked` refuses it.
template <class RootClass, class Expr, class RT>
struct claimed_head {
  // Reading the candidates refuses a directive that never runs.
  using runs = typename cands<Expr>::type;
  using type = RootClass;
};
template <class Expr, class RT>
struct claimed_head<void, Expr, RT> : head_type<Expr, RT> {};

template <class RootClass, class RT, class A, class HeadL, class Rest>
struct head_of {
  using expr = typename seq_of<HeadL>::type;
  using T = typename claimed_head<RootClass, expr, RT>::type;
  using claimed =
      typename seq_of<tl_cat_t<TypeList<Create<T, expr>>, Rest>>::type;
  static constexpr bool buries = !std::is_same_v<Rest, TypeList<>>;
  using type = cond_t<std::is_void_v<RootClass> &&
                          (look<expr>::may || (buries && !look<expr>::writes)),
                      A, claimed>;
};
template <class RootClass, class RT, class A, class Rest>
struct head_of<RootClass, RT, A, TypeList<>, Rest> {
  using type = Create<RootClass, A>;
};
template <class RT, class A, class Rest>
struct head_of<void, RT, A, TypeList<>, Rest> {
  using type = A;
};

// A spelled creation claims the whole alternative, so a call in it is a second
// head even where the alternative would adopt it.
template <class RootClass, class RT, class A,
          bool Heads =
              look<A>::heads && (std::is_void_v<RootClass> || !look<A>::adopts)>
struct headed_of : head_of<RootClass, RT, A, typename parts<A>::head,
                           typename parts<A>::rest> {};
template <class RootClass, class RT, class A>
struct headed_of<RootClass, RT, A, true> {
  using type = A;
};
template <class RootClass, class RT, class A>
using headed = typename headed_of<RootClass, RT, A>::type;

// An alternative whose head the text may leave out, and which spells no
// creation to carry the rule over that: it hands a node over on some paths
// only.
template <class A, class Head = typename parts<A>::head>
inline constexpr bool optional_head =
    !look<A>::heads && look<typename seq_of<Head>::type>::may;
template <class A>
inline constexpr bool optional_head<A, TypeList<>> = false;

// The creation algebra names it:  [bare...] [Create<T, ...>] Nest Nest ...
// One head, then a chain of wrappers. Both faults below are spellable. A head
// stands on every path once `Head`, and on some path once `Any`.
template <class L, bool Head = false, bool Any = false>
struct chain;
template <bool Head, bool Any>
struct chain<TypeList<>, Head, Any> {
  static constexpr bool twice = false;
  static constexpr bool bare = false;
};
template <class E, class... Rest, bool Head, bool Any>
struct chain<TypeList<E, Rest...>, Head, Any> {
  using L = look<E>;
  // A wrapper only counts where no head can stand there.
  static constexpr bool wraps = !L::may && L::wraps;
  using next = chain<TypeList<Rest...>, Head || L::heads || L::writes || wraps,
                     Any || L::may || wraps>;
  static constexpr bool twice = (L::may && Any) || L::inside || next::twice;
  static constexpr bool bare = (wraps && !Head) || next::bare;
};
template <class L>
inline constexpr bool late_call = late_in<false, L>;

// An alternative that creates nothing starts on the node it adopts: a write
// after the call goes there.
template <class A, bool Creates = is_self_resolving<A>>
struct hand_at_start {
  using type = TypeList<>;
};
template <class A>
struct hand_at_start<A, false> : adopt_cands<A, TypeList<>> {};

// A failure here is a grammar this stage cannot complete. `Over` marks an
// override, whose type is the base rule's.
template <class RT, class A, bool Over>
struct checked {
  using c = chain<typename alt_parts<A>::type>;
  using hand = typename in_hand<typename hand_at_start<A>::type, A, true>::type;
  static_assert(!Over || all_derive<hand, RT>,
                "this override builds a class outside the type of the rule it "
                "overrides: every caller of the rule expects that type, so the "
                "node the override creates, or the wrapper of its last nest, "
                "must be that class or a class derived from it");
  static_assert(!optional_head<A>,
                "this alternative returns a node that the text may leave out: "
                "a create<>, or a call to a rule that builds a node, is inside "
                "an option(), a repetition that may be empty, or one branch of "
                "a choice, so a text without it gives the rule no node. Put "
                "the node on every path, or create the rule's own node around "
                "it and let the optional part write into it");
  static_assert(
      !c::twice,
      "this alternative can create two nodes: the second replaces the "
      "first, which is then lost. An alternative starts at most one node, "
      "with a create<> or a call to a rule that builds a node, never both, "
      "and may then wrap it with nests. A branch of a choice that starts a "
      "node counts as one start, and each turn of a repetition of such a "
      "branch counts as another");
  static_assert(!late_call<typename alt_parts<A>::type>,
                "this alternative calls a rule that builds a node after the "
                "current node was written, created or nested: a call adopts "
                "its node only while the current node is still empty, and the "
                "two nodes are never merged. Store the called node with "
                "`assign` or `append`, or write the shared part as a fragment "
                "(`static constexpr auto body = ...;`, an expression whose "
                "directives write the current node) and use it in each "
                "alternative that needs it");
  static_assert(!c::bare,
                "this alternative has a nest<> with nothing before it: a nest "
                "wraps the current node, so a create, a call to a rule that "
                "builds a node, or a write must come before it on every path");
  using type = A;
};

// The body, alternative by alternative; a choice keeps its arity.
template <class RT, class RootClass, class A, bool Over>
using checked_alt = checked<RT, headed<RootClass, RT, A>, Over>;
template <class RT, class RootClass, class B, bool Over>
struct materialize {
  using type = typename checked_alt<RT, RootClass, B, Over>::type;
};
template <class RT, class RootClass, bool Over, class... As>
struct materialize<RT, RootClass, Choice<As...>, Over> {
  using type = Choice<typename checked_alt<RT, RootClass, As, Over>::type...>;
};

// Only a node rule needs a head: an infix rule's body is the climb's, and a
// terminal or data rule builds nothing.
template <class X, bool Node = is_node_rule<X>>
struct rule_body {
  using type = typename ov::stage_body<X>::type;
};
template <class X>
struct rule_body<X, true>
    : materialize<
          rule_type_t<X>,
          typename root_create_class<std::remove_cv_t<decltype(X::body)>>::type,
          typename ov::stage_body<X>::type, is_overridden<X>> {};

// The stage hands back a rule, not a body on the side: asking the rule and
// asking the stage must not give two answers.
template <class R, auto B>
struct Materialized : R {
  using declared = R;
  static constexpr auto body = B;
  static constexpr bool produced_body = true;
};

// An override keeps its rule's kind, node or value: one that stops building a
// node would still answer the type of the rule it replaces.
template <class Rule>
consteval bool OverrideChangesKind() {
  static_assert(
      dependent_false<Rule>,
      "this override changes whether the rule builds a node: the rule it "
      "overrides builds a node and the override builds none, or the "
      "reverse. Build a node in the override (with a create<>, a directive "
      "or a call to a rule that builds one) exactly when the base rule does");
  return false;
}

template <class X, bool Over = is_overridden<X>>
inline constexpr bool node_kept = true;
template <class X>
inline constexpr bool node_kept<X, true> = [] {
  if constexpr (!(is_node_rule<X> == is_node_rule<inherited_of_t<X>>))
    return OverrideChangesKind<declared_of_t<X>>();
  return true;
}();

// An unchanged body keeps its rule: a wrapper would double every instantiation
// keyed on the rule.
template <class X, bool Same = std::is_same_v<typename rule_body<X>::type,
                                              typename ov::stage_body<X>::type>>
struct mat_rule {
  static_assert(node_kept<X>);
  using type = Materialized<X, typename rule_body<X>::type{}>;
};
template <class X>
struct mat_rule<X, true> {
  static_assert(node_kept<X>);
  using type = X;
};

template <class L>
struct mat_each;
template <class... Xs>
struct mat_each<TypeList<Xs...>> {
  using type = TypeList<typename mat_rule<Xs>::type...>;
};

}  // namespace mat

// The creation, spelled: every alternative of a node rule heads itself here,
// so the rule's own element is a pure label in every grammar. `S` is the stage
// before, and everything it published is published again.
template <class S>
struct MaterializeCreate {
  template <class R>
  using rule = typename mat::mat_rule<typename S::template rule<R>>::type;

  template <class R>
  using body = typename ov::stage_body<rule<R>>::type;

  // Whether this stage rewrote the rule. A root `create` is removed before
  // this reads it, so `rule<create<T>(x)>` is rewritten to the same body.
  template <class R>
  static constexpr bool rewritten =
      !std::is_same_v<rule<R>, typename S::template rule<R>>;

  // `entry` and the lists hold this stage's rules, so asking through them or
  // through `rule` gives one answer.
  using entry = typename mat::mat_rule<typename S::entry>::type;
  using ignored = typename mat::mat_each<typename S::ignored>::type;
  using hidden = typename mat::mat_each<typename S::hidden>::type;
  static constexpr auto word_boundary = S::word_boundary;
  static constexpr bool case_insensitive = S::case_insensitive;
};

// The pipeline, as one name. A stage added here reaches every reader.
template <class G>
using Normalize = MaterializeCreate<ResolveOverride<G>>;

}  // namespace cursiv::detail
