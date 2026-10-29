#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/CommitLowering.hpp"
#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/ExprShape.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

// The declarative layer: rule kinds whose body is a template argument
// (terminal / data / rule / override), type deduction for each, and the
// cursiv::GrammarSpec base a grammar derives from.

namespace cursiv {

// A rule is referred to (`ref<R>`), never combined; its body is an NTTP, so a
// rule can reference itself or a merely-declared sibling from its own
// base-clause. The kind decides whether the token separator runs inside the
// body, which decides what language is accepted.
//
// A kind spelled directly is an anonymous rule: `ref<terminal<body>>` inside
// another body is an inline atom, `ref<rule<body>>` an inline region. Nothing
// new runs -- the base already is a rule, so the reference resolves, the slot
// travels in the type, and two identical spellings are one type and one rule.

namespace detail {

// A rule value where a body belongs (`rule<ID{}>`) is the one bare use no
// combinator can refuse: a rule kind is not an expression, so it reaches
// nothing else. Stated once, asked by both kinds.
template <auto Body>
struct body_not_a_rule {
  static_assert(
      !requires { typename std::remove_cv_t<decltype(Body)>::kind; },
      "a rule is used through `ref<>`, never directly: write "
      "`ref<Name>` for a declared rule, and `ref<terminal<...>>` or "
      "`ref<rule<...>>` for an inline one");
  static constexpr bool ok = true;
};

}  // namespace detail

struct GrammarSpec;
template <class G, class X>
struct InGrammar;

// A grammar is named whole: `section<G>` reads G's entry as G alone reads it.
template <class R>
inline constexpr bool is_grammar = std::is_base_of_v<GrammarSpec, R>;

namespace detail {
template <class R>
struct named_rule {
  using type = R;
};
template <class R>
  requires is_grammar<R>
struct named_rule<R> {
  using type = InGrammar<R, typename R::entry>;
};
}  // namespace detail

// The rule a reference reads.
template <class R>
using named_rule_t = typename detail::named_rule<R>::type;

namespace detail {
template <class E>
inline constexpr bool is_as = false;
template <class C, class E>
inline constexpr bool is_as<As<C, E>> = true;
template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool holds_as = is_as<E>;
template <class E, class... Ks>
inline constexpr bool holds_as<E, TypeList<Ks...>> =
    is_as<E> || (holds_as<Ks> || ...);

// A body's `as<X>(e)` is resolved once the rule is formed: an inline data rule
// over e with the converter, so every reader below meets a rule and e's tokens
// stay e's own. That rule is never called: an engine reads e in place, and the
// conversion rides the record of what e reads.
template <class E, bool Holds = holds_as<E>>
struct as_resolved {
  using type = E;
};
template <class E>
using as_resolved_t = typename as_resolved<E>::type;
template <class E, class Kids>
struct as_rebuilt;
template <class E, class... Ks>
struct as_rebuilt<E, TypeList<Ks...>> {
  using type = typename ExprShape<E>::template rebind<as_resolved_t<Ks>...>;
};
template <class E>
struct as_resolved<E, true> : as_rebuilt<E, typename ExprShape<E>::kids> {};
template <class C, class E>
struct as_resolved<As<C, E>, true> {
  using type = Ref<Rule<E{}, C>>;
};
}  // namespace detail

// One token, lexed: the separator never runs inside. The optional slot names
// what the token's text becomes.
template <auto Body, class... Opts>
struct TerminalRule {
  static_assert(detail::body_not_a_rule<Body>::ok);
  static_assert(!detail::holds_as<std::remove_cv_t<decltype(Body)>>,
                "inside a terminal, as<X>(e) must wrap the whole body or "
                "each of its alternatives: write terminal<as<X>(...)> or "
                "terminal<as<A>(...) | as<B>(...)>");
  static_assert(!detail::holds_commit<std::remove_cv_t<decltype(Body)>>,
                "a commit is allowed in a rule, not in a terminal: inside a "
                "terminal, use option, many, some or repeat without commit");
  using kind = detail::terminal_kind;
  using slot = TypeList<Opts...>;
  static constexpr auto body = Body;
};

// A rule covers both a node and a value, and the body decides which: what it
// commits (a create, a nest, a directive, a reference to a builder) makes it a
// node rule, and a slot makes it a value rule outright.
template <auto Body, class... Opts>
struct Rule {
  static_assert(detail::body_not_a_rule<Body>::ok);
  static_assert(!detail::stray_commit<
                    detail::as_resolved_t<std::remove_cv_t<decltype(Body)>>>,
                "commit(prefix, body) must be the direct item of option, "
                "many, some or repeat, as in many(commit(a, b))");
  using kind = detail::node_kind;
  using slot = TypeList<Opts...>;
  static constexpr auto body = detail::commit_free_t<
      detail::as_resolved_t<std::remove_cv_t<decltype(Body)>>>{};
};

namespace detail {

// The operators of a table's levels of one kind, as alternatives: `prefix`,
// `postfix`, or `left` for every binary level.
template <class L, Association A>
inline constexpr bool level_is =
    A == Association::left ? !is_unary_level<L> : L::kGroup == A;
template <class Lvls, Association A>
struct level_ops;
template <class... Ls, Association A>
struct level_ops<Levels<Ls...>, A>
    : tl_cat<cond_t<level_is<Ls, A>,
                    typename alts_list<typename Ls::op_expr>::type,
                    TypeList<>>...> {};
template <class Lvls, Association A>
using level_ops_t = typename level_ops<Lvls, A>::type;

// A condition takes no position, bare or under a repetition or a node whose
// reading is its child's.
template <class E>
inline constexpr bool is_condition = false;
template <std::size_t Mn, std::size_t Mx, class E>
inline constexpr bool is_condition<Repeat<Mn, Mx, E>> = is_condition<E>;
template <class E>
  requires transparent<E>
inline constexpr bool is_condition<E> = is_condition<through_t<E>>;
template <class E>
inline constexpr bool is_condition<And<E>> = true;
template <class E>
inline constexpr bool is_condition<Not<E>> = true;
template <class E>
inline constexpr bool is_condition<GapHas<E>> = true;
template <class E>
inline constexpr bool is_condition<GapIs<E>> = true;
template <>
inline constexpr bool is_condition<Eof> = true;

// What a sequence reads: its elements, but for the conditions around them.
template <class... Es>
using readers_t =
    tl_cat_t<cond_t<is_condition<Es>, TypeList<>, TypeList<Es>>...>;

// Whether a sequence builds a node, which only complete rules can say.
template <class E>
struct sequence_builds_node;

// A region, a mark or prose adds to what it holds and takes nothing from it.
template <class E>
inline constexpr bool is_wrapper = false;
template <class I, class H, class E>
inline constexpr bool is_wrapper<Skip<I, H, E>> = true;
template <class Tag, class E>
inline constexpr bool is_wrapper<Mark<Tag, E>> = true;
template <class E, str T>
inline constexpr bool is_wrapper<Doc<E, T>> = true;

// A sequence of several elements that read, wrapped or not, is a data rule
// written in place: the wrapper stays in the rule. `Complete` is a reader that
// can ask what the rules build, and leaves a sequence that builds a node as it
// is.
template <bool Complete, class E>
inline constexpr bool unnamed_sequence = false;
template <bool Complete, class... Es>
  requires(tl_size<readers_t<Es...>>::value >= 2 &&
           (!Complete || !sequence_builds_node<Seq<Es...>>::value))
inline constexpr bool unnamed_sequence<Complete, Seq<Es...>> = true;
template <bool Complete, class E>
  requires is_wrapper<E>
inline constexpr bool unnamed_sequence<Complete, E> =
    unnamed_sequence<Complete, through_t<E>>;

template <bool Complete, class E>
struct unnamed_data {
  using type = E;
};
template <bool Complete, class E>
using unnamed_data_t = typename unnamed_data<Complete, E>::type;
template <bool Complete, class... Es>
struct unnamed_data<Complete, Choice<Es...>> {
  using type = Choice<unnamed_data_t<Complete, Es>...>;
};
template <bool Complete, class E>
  requires unnamed_sequence<Complete, E>
struct unnamed_data<Complete, E> {
  using type = Ref<Rule<E{}>>;
};

// The operand of every `assign` and `append` in a body, once the rules are
// complete.
template <class E, bool Holds = holds_directive<E>>
struct unnamed_data_operands {
  using type = E;
};
template <class E>
using unnamed_data_operands_t = typename unnamed_data_operands<E>::type;
template <class E, class Kids>
struct operands_rebuilt;
template <class E, class... Ks>
struct operands_rebuilt<E, TypeList<Ks...>> {
  using type =
      typename ExprShape<E>::template rebind<unnamed_data_operands_t<Ks>...>;
};
template <class E>
struct unnamed_data_operands<E, true>
    : operands_rebuilt<E, typename ExprShape<E>::kids> {};
template <auto Sp, class E>
struct unnamed_data_operands<Assign<Sp, E>, true> {
  using type = Assign<Sp, unnamed_data_t<true, unnamed_data_operands_t<E>>>;
};
template <auto Sp, class E>
struct unnamed_data_operands<Append<Sp, E>, true> {
  using type = Append<Sp, unnamed_data_t<true, unnamed_data_operands_t<E>>>;
};

template <class Lvls>
struct inline_operators;
template <class Lvls>
using inline_operators_t = typename inline_operators<Lvls>::type;
template <class... Ls>
struct inline_operators<Levels<Ls...>> {
  using type = Levels<typename inline_operators<Ls>::type...>;
};
template <class Op>
struct inline_operators<LeftAssoc<Op>> {
  using type = LeftAssoc<unnamed_data_t<false, as_resolved_t<Op>>>;
};
template <class Op>
struct inline_operators<RightAssoc<Op>> {
  using type = RightAssoc<unnamed_data_t<false, as_resolved_t<Op>>>;
};
template <class Op>
struct inline_operators<NonAssoc<Op>> {
  using type = NonAssoc<unnamed_data_t<false, as_resolved_t<Op>>>;
};
template <Association A, class N, auto O, auto X, class Op>
struct inline_operators<UnaryLevel<A, N, O, X, Op>> {
  using type =
      UnaryLevel<A, N, O, X, unnamed_data_t<false, as_resolved_t<Op>>>;
};

// Any number of the alternatives, as a list of one item; none where there
// are none.
template <class Alts>
struct any_of_turns {
  using type = TypeList<Repeat<0, kUnbounded, typename or_of<Alts>::type>>;
};
template <>
struct any_of_turns<TypeList<>> {
  using type = TypeList<>;
};
template <class Ops, class Operand>
struct joins_of {
  using type = TypeList<cat_seq_t<typename or_of<Ops>::type, Operand>>;
};
template <class Operand>
struct joins_of<TypeList<>, Operand> {
  using type = TypeList<>;
};
template <class L>
struct cat_list;
template <class... Es>
struct cat_list<TypeList<Es...>> : cat_seq<Es...> {};

// What the chain matches, as an expression: `operand ((ops operand) |
// postfix)*`, an operand being `prefix* primary`. Every analysis --
// nullability, tails (`many` ends past the separator, which is the climb's own
// discipline), reachability, the element table, validation -- reads this;
// only an engine parses the climb instead.
template <class Primary, class Lvls>
struct infix_body {
  using operand = typename cat_list<tl_cat_t<
      typename any_of_turns<level_ops_t<Lvls, Association::prefix>>::type,
      TypeList<Ref<Primary>>>>::type;
  using turns = typename any_of_turns<tl_cat_t<
      typename joins_of<level_ops_t<Lvls, Association::left>, operand>::type,
      level_ops_t<Lvls, Association::postfix>>>::type;
  using type = typename cat_list<tl_cat_t<TypeList<operand>, turns>>::type;
};

}  // namespace detail

// A chain of binary operators is a grammar spelled badly either way:
// left-recursive spelled directly, one near-identical rule per level spelled
// as a cascade. So it is a rule kind: the primary rule for the operands, the
// three members (left operand, operator text, right operand), the level table.
// A binary level builds `Node`; a prefix or postfix level builds the class it
// names.
template <class Primary, auto LeftPtr, auto OpPtr, auto RightPtr, auto Lvls,
          class Node>
struct InfixRule {
  using kind = detail::infix_kind;
  using slot = TypeList<>;
  using primary = Primary;
  using node_type = Node;
  using levels = detail::inline_operators_t<std::remove_cv_t<decltype(Lvls)>>;
  static constexpr auto left_pointer = LeftPtr;
  static constexpr auto op_pointer = OpPtr;
  static constexpr auto right_pointer = RightPtr;
  static constexpr auto body =
      typename detail::infix_body<Primary, levels>::type{};
};

namespace detail {

// The rule `infix<>` spells: three member pointers after the levels, or none
// for `T::left`, `T::op` and `T::right`.
template <class Primary, class T, auto Lvls, auto... Members>
struct infix_of {
  static_assert(sizeof...(Members) == 3,
                "cursiv: an infix rule names zero or three members: the left "
                "operand, the operator and the right operand");
};
template <class Primary, class T, auto Lvls>
struct infix_of<Primary, T, Lvls> {
  using type = InfixRule<Primary, &T::left, &T::op, &T::right, Lvls, T>;
};
template <class Primary, class T, auto Lvls, auto Left, auto Op, auto Right>
struct infix_of<Primary, T, Lvls, Left, Op, Right> {
  using type = InfixRule<Primary, Left, Op, Right, Lvls, T>;
};

}  // namespace detail

template <class R>
concept AnyRule = requires {
  typename R::kind;
  R::body;
};

// An override does not replace a rule's name: the effective rule is minted
// from the base and the override, and `declared` is the identity every
// diagnostic and every dump prints.

template <class R>
struct declared_of {
  using type = R;
};
// Recursive: a stage that mints over a mint must still name the rule an
// author wrote.
template <class R>
  requires requires { typename R::declared; }
struct declared_of<R> : declared_of<typename R::declared> {};
template <class R>
using declared_of_t = typename declared_of<R>::type;

template <class R>
inline constexpr bool is_overridden = requires { typename R::declared; };

// The rule an override stood over: what its grammar's parents made effective,
// the declared rule where nothing else restated it.
template <class R>
struct inherited_of : declared_of<R> {};
template <class R>
  requires requires { typename R::inherited; }
struct inherited_of<R> {
  using type = typename R::inherited;
};
template <class R>
using inherited_of_t = typename inherited_of<R>::type;

// An inline rule has no identifier. The kind spelled directly is the rule; a
// declared rule derives from one, so the two are told apart by type alone.
// Every place that names a rule reads the label rather than reflect a
// template-id nobody can read.
template <class R>
inline constexpr std::string_view anonymous_rule_label{};
template <auto Body, class... Opts>
inline constexpr std::string_view
    anonymous_rule_label<TerminalRule<Body, Opts...>> = "inline terminal";
template <auto Body, class... Opts>
inline constexpr std::string_view anonymous_rule_label<Rule<Body, Opts...>> =
    "inline rule";

template <class R>
inline constexpr bool is_anonymous_rule = !anonymous_rule_label<R>.empty();

// In a condition, an inline terminal is text the condition reads, never a
// token of the grammar.
template <class R>
inline constexpr bool is_inline_terminal = false;
template <auto Body, class... Opts>
inline constexpr bool is_inline_terminal<TerminalRule<Body, Opts...>> = true;

namespace detail {

// The deductions below walk a rule's body and descend into the rules it
// names, carrying the path of rules they are inside so that a cycle stops.
// Only the rules of the path that the callee reaches again can change its
// answer: they reach it along the path, so they lie on a cycle with it. The
// callee is handed those alone, in an order it fixes, so every path into a
// rule outside a cycle shares one answer, and so does every order of one set
// of rules inside one.

// The references a deduction can follow: through the shapes that gather or
// wrap their kids, never into a directive's payload or a lookahead.
template <class E>
inline constexpr bool wraps_calls = false;
template <class E, str T>
inline constexpr bool wraps_calls<Doc<E, T>> = true;
template <class Tag, class E>
inline constexpr bool wraps_calls<Mark<Tag, E>> = true;
template <auto P, class E>
inline constexpr bool wraps_calls<Flag<P, E>> = true;
template <class E, class Kids = typename ExprShape<E>::kids>
struct walked_calls {
  using type = TypeList<>;
};
template <class E, class... Ks>
  requires(grouping<E> || wraps_calls<E>)
struct walked_calls<E, TypeList<Ks...>>
    : tl_cat<typename walked_calls<Ks>::type...> {};
template <class R>
struct walked_calls<Ref<R>, TypeList<>> {
  using type = TypeList<named_rule_t<R>>;
};
template <class R>
using body_calls_t =
    typename walked_calls<std::remove_cv_t<decltype(R::body)>>::type;

// The rules a deduction enters from R: those its body names, and an infix
// rule's primary. A rule read in a section adds those the rule in force there
// names (`InGrammar.hpp`).
template <class R, bool Infix = std::is_same_v<typename R::kind, infix_kind>>
struct rule_calls : dedupe<body_calls_t<R>> {};
template <class R>
struct rule_calls<R, true>
    : dedupe<tl_cat_t<TypeList<typename R::primary>, body_calls_t<R>>> {};
template <class R>
struct deduction_calls : rule_calls<R> {};

// Every rule reached from R in one step or more, in the order a breadth-first
// walk meets them; `Calls<X>::type` names the steps from X.
template <template <class> class Calls, class Seen, class Front>
struct calls_closure;
template <template <class> class Calls, class Seen>
struct calls_closure<Calls, Seen, TypeList<>> {
  using type = Seen;
};
template <template <class> class Calls, class Seen, class... Fs>
struct calls_closure<Calls, Seen, TypeList<Fs...>> {
  using fresh = typename tl_minus<
      dedupe_t<tl_cat_t<typename Calls<Fs>::type...>>, Seen>::type;
  using type =
      typename calls_closure<Calls, tl_cat_t<Seen, fresh>, fresh>::type;
};
template <template <class> class Calls, class R>
struct reached_by
    : calls_closure<Calls, typename Calls<R>::type, typename Calls<R>::type> {
};

// The positions in `Reach` of the rules of `Kept` it holds, sorted, each
// once, and the rules at those positions.
template <std::size_t N>
struct reach_positions {
  std::size_t at[N];
  std::size_t size = 0;
};
template <std::size_t N>
consteval reach_positions<N> sorted_positions(const std::size_t (&in)[N],
                                              std::size_t held) {
  reach_positions<N> out{};
  for (const std::size_t p : in) {
    std::size_t i = 0;
    while (i < out.size && out.at[i] < p) ++i;
    if (p >= held || (i < out.size && out.at[i] == p)) continue;
    for (std::size_t j = out.size; j > i; --j) out.at[j] = out.at[j - 1];
    out.at[i] = p;
    ++out.size;
  }
  return out;
}
template <class Reach, auto Pos, class Is = std::make_index_sequence<Pos.size>>
struct reach_pick;
template <class Reach, auto Pos, std::size_t... Is>
struct reach_pick<Reach, Pos, std::index_sequence<Is...>> {
  using type = TypeList<tl_at_t<Pos.at[Is], Reach>...>;
};
template <class Kept, class Reach>
struct in_reach_order;
template <class... Ys, class Reach>
struct in_reach_order<TypeList<Ys...>, Reach>
    : reach_pick<Reach, sorted_positions<sizeof...(Ys)>(
                            {index_of<Ys, Reach>::value...},
                            tl_size<Reach>::value)> {};

// The path a deduction entering R carries on: the rules of `Seen` R reaches,
// in the order R reaches them -- one set is one path, whichever order it was
// walked in.
template <template <class> class Calls, class Seen, class R>
struct path_into;
template <template <class> class Calls, class R>
struct path_into<Calls, TypeList<>, R> {
  using type = TypeList<>;
};
template <template <class> class Calls, class... Ys, class R>
struct path_into<Calls, TypeList<Ys...>, R>
    : in_reach_order<TypeList<Ys...>, typename reached_by<Calls, R>::type> {};
// The path R's own walk carries: the rules of `Seen` it can meet, then R.
template <class Seen, class R>
using deduction_path_t =
    tl_cat_t<typename path_into<deduction_calls, Seen, R>::type, TypeList<R>>;

}  // namespace detail

// The genre of a `rule<>`, deduced. `terminal<>` names its own; `rule<>`
// covers value and node, and what tells them apart is whether the body
// commits a node: a create, a nest, a directive, or a reference to a rule
// that builds one. A slot settles it outright -- a rule that names what its
// text becomes is a value rule whatever its body holds.
//
// `Seen` is the reference path: `A : rule<ref<B>>` over `B : rule<ref<A>>`
// would ask the question of itself for ever, so a rule already on the path
// commits nothing new and the answer comes from the rest.
template <class R, class Seen, bool OnPath>
struct rule_builds_node_of;

template <class E, class Seen, class Kids = typename detail::ExprShape<E>::kids>
inline constexpr bool builds_node = false;
template <class E, class Seen, class... Ks>
  requires detail::grouping<E>
inline constexpr bool builds_node<E, Seen, TypeList<Ks...>> =
    (builds_node<Ks, Seen> || ...);
template <class T, class E, class Seen>
inline constexpr bool builds_node<Create<T, E>, Seen> = true;
template <class T, auto P, class E, class Seen>
inline constexpr bool builds_node<Nest<T, P, E>, Seen> = true;
template <auto Sp, class E, class Seen>
inline constexpr bool builds_node<Assign<Sp, E>, Seen> = true;
template <auto Sp, class E, class Seen>
inline constexpr bool builds_node<Append<Sp, E>, Seen> = true;
template <auto P, class E, class Seen>
inline constexpr bool builds_node<Flag<P, E>, Seen> = true;
// A lookahead commits nothing and `through()` is a token shape, so neither is
// descended into; a reference answers what the rule it names is.
template <class R, class Seen>
inline constexpr bool builds_node<Ref<R>, Seen> =
    rule_builds_node_of<named_rule_t<R>, Seen,
                        detail::tl_has<named_rule_t<R>, Seen>::value>::value;

template <class R>
inline constexpr bool slot_empty = std::is_same_v<typename R::slot, TypeList<>>;

// Dispatched on the path, never guarded by an `&&`: naming `::value` of the
// other operand instantiates it, so a cycle would recurse to the depth limit
// instead of stopping.
template <class R, class Seen>
struct rule_builds_node_of<R, Seen, true> : std::false_type {};
// Only a `rule<>` with no slot is answered by its body: the walk, and the
// path it carries, are formed for that rule alone.
template <class R, class Seen,
          bool Walks = std::is_same_v<typename R::kind, detail::node_kind> &&
                       slot_empty<R>>
struct body_builds_node
    : std::bool_constant<std::is_same_v<typename R::kind, detail::infix_kind>> {
};
template <class R, class Seen>
struct body_builds_node<R, Seen, true>
    : std::bool_constant<builds_node<std::remove_cv_t<decltype(R::body)>,
                                     detail::deduction_path_t<Seen, R>>> {};
template <class R, class Seen>
struct rule_builds_node_of<R, Seen, false> : body_builds_node<R, Seen> {};

template <class R>
inline constexpr bool kind_is_node =
    rule_builds_node_of<R, TypeList<>, false>::value;

// A rule declared `rule<>` whose body commits nothing is a value rule: it
// composes its text from the parts.
template <class R>
inline constexpr bool is_data_rule =
    std::is_same_v<typename R::kind, detail::node_kind> && !kind_is_node<R>;
template <class R>
inline constexpr bool is_node_rule =
    std::is_same_v<typename R::kind, detail::node_kind> && kind_is_node<R>;
template <class R>
inline constexpr bool is_infix_rule =
    std::is_same_v<typename R::kind, detail::infix_kind>;

namespace detail {
template <class... Es>
struct sequence_builds_node<Seq<Es...>>
    : std::bool_constant<builds_node<Seq<Es...>, TypeList<>>> {};
}  // namespace detail

// A rule's body, and the only read of one. An infix rule's is the synthesized
// `primary (ops primary)*`.
template <class R>
using declared_body_t = std::remove_cv_t<decltype(R::body)>;

// A create at the root of a body is the rule's own creation, not a region
// inside it: what everything downstream reads is `rest`, and the stage makes
// the head again in front of it -- same elements, same shape, same tree.
template <class B>
struct shed_root_create {
  using type = B;
};
template <class T, class E>
struct shed_root_create<Create<T, E>> {
  using type = E;
};
template <class R>
using rule_body_t = typename shed_root_create<declared_body_t<R>>::type;

// A value rule answers its slot: the converter's result, the target the slot
// names, else the text. A node rule answers its body: the type a root create
// names, else the most derived class the directives write, else the node it
// adopts; for a choice, the common base of the alternatives.

// The positioned conversion result, declared here and defined below: the
// grammar layer sits below conversion and must not include it, yet the
// deduction has to recognise the wrapper (see unwrap_result below).
template <class T>
struct ConversionResult;

// ...and the leaf list, declared for the same reason: the parts forms of a
// converter are recognised in the one chain below, beside the text forms --
// never by exclusion, which leaves a parts converter no optional-resource
// pair. Only a reference to it is ever formed here; the definition is complete
// everywhere these concepts are checked.
// The parts a data rule joins, as values: each leaf through its own
// converter. A parts converter sees what the join concatenates, never the raw
// bytes -- the two would answer differently the moment a leaf converts.
using Parts = std::span<const std::string_view>;

// A terminal and a value rule name what their text becomes with `as<X>`
// around the whole body. X is a target type reached by `default_converter<T>`,
// a callable that reads the joined text or the parts, or any other constant,
// which is the value itself.
//
// A lambda is named before it is cited. A closure defined outside an
// initializer is TU-local ([basic.link]/16) and an exported rule may not
// expose one, so `as<[](...){...}>` spelled in place is refused in every
// module interface: write `inline constexpr auto f = [](...){...};` and cite
// `f`. (The closure in an NTTP is a g++ extension, CWG2542; this project is
// g++-only and says so here.)

// The resource parameter is spelled exactly: a pointer converts implicitly to
// `bool` and `void*`, so a second parameter that also accepts an `int` or a
// `void*` is not a resource.
template <auto F, class A>
concept resource_arg = requires(std::string_view t, A a) { F(t, a); } &&
                       !requires(std::string_view t, int i) { F(t, i); } &&
                       !requires(std::string_view t, void* v) { F(t, v); };
template <auto F, class A>
concept parts_resource_arg = requires(Parts p, A a) { F(p, a); } &&
                             !requires(Parts p, int i) { F(p, i); } &&
                             !requires(Parts p, void* v) { F(p, v); };

namespace detail {

// The converters `as<X>` turns a callable or a constant into: one reads the
// joined text, one the parts, one nothing. Each is a `made_conv`, which a slot
// holds as it is.
struct made_conv {};
template <auto F>
struct text_conv : made_conv {
  // A converter that reads nothing: the match denotes one value whatever it
  // spelled, and no text is joined for it.
  static constexpr decltype(auto) convert()
    requires requires { F(); }
  {
    return F();
  }
  static constexpr decltype(auto) convert(std::string_view t)
    requires requires { F(t); }
  {
    return F(t);
  }
  static constexpr decltype(auto) convert(std::string_view t,
                                          std::pmr::memory_resource* m)
    requires resource_arg<F, std::pmr::memory_resource*>
  {
    return F(t, m);
  }
};
template <auto F>
struct parts_conv : made_conv {
  static constexpr decltype(auto) convert(Parts p)
    requires requires { F(p); }
  {
    return F(p);
  }
  static constexpr decltype(auto) convert(Parts p, std::pmr::memory_resource* m)
    requires parts_resource_arg<F, std::pmr::memory_resource*>
  {
    return F(p, m);
  }
};
// The constant a match denotes: `'is' 'not' | '!='` is one operator, and the
// member takes it without the text being read.
template <auto V>
struct value_conv : made_conv {
  static constexpr auto convert() { return V; }
};

// The converter of a data rule that states none and passes a value other
// than text: the builder reads the value of the record the rule took.
template <class T>
struct pass_conv {
  using type = T;
};
template <class C>
inline constexpr bool is_pass_conv = false;
template <class T>
inline constexpr bool is_pass_conv<pass_conv<T>> = true;

// Whether a class declares a call operator of any shape: the probe's own is
// then ambiguous in a class deriving from both.
struct call_probe {
  void operator()() const;
};
template <class T>
struct call_probe_mix : T, call_probe {};
template <class T>
concept declares_call = std::is_class_v<T> && !std::is_final_v<T> &&
                        !requires { &call_probe_mix<T>::operator(); };
template <class T>
inline constexpr bool is_callable_constant =
    (std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>) ||
    declares_call<T>;

// What `as<X>` makes of a constant X. A callable is a converter, and what it
// reads is what its signature accepts; any other constant is the value.
template <auto X, bool Callable = is_callable_constant<
                      std::remove_cv_t<decltype(X)>>>
struct as_conv {
  using type = value_conv<X>;
};
template <auto X>
struct as_conv<X, true> {
  static constexpr bool none = requires { X(); };
  static constexpr bool text = requires(std::string_view t) { X(t); } ||
                               resource_arg<X, std::pmr::memory_resource*>;
  static constexpr bool parts = requires(Parts p) { X(p); } ||
                                parts_resource_arg<X, std::pmr::memory_resource*>;
  static_assert(none || text || parts,
                "as<f>(e): f must take the text (std::string_view), the "
                "parts (std::span<const std::string_view>) or no argument; "
                "a second parameter may only be a std::pmr::memory_resource*");
  static_assert(int(none) + int(text) + int(parts) <= 1,
                "as<f>(e): f accepts more than one kind of input (the text, "
                "the parts or no argument), as a generic (auto) lambda does: "
                "give its parameter a type");
  using type = cond_t<parts, parts_conv<X>, text_conv<X>>;
};

// `as<X>(e)`: what `e` matches converts with C, resolved where the rule that
// holds it is formed.
template <class C, class E>
constexpr auto as_with() {
  static_assert(!is_as<E>,
                "as<X>(as<Y>(e)) is not allowed: combine the two conversions "
                "in one function");
  static_assert(!holds_directive<E>,
                "as<X>(e) converts text, but this e holds a directive "
                "(create, assign, append, flag or nest): move the directives "
                "out of the `as`");
  return As<C, E>{};
}

// A word an alternative of a terminal reads: a literal with no guard, a
// keyword, or a choice of those.
template <class E>
inline constexpr bool word_of = false;
template <str S, bool CI, class G>
  requires(std::is_void_v<G> || std::is_same_v<G, WordBound>)
inline constexpr bool word_of<Lit<S, CI, G>> = true;
template <class... Es>
inline constexpr bool word_of<Choice<Es...>> = (word_of<Es> && ...);

template <class C>
inline constexpr bool is_value_conv = false;
template <auto V>
inline constexpr bool is_value_conv<value_conv<V>> = true;
template <class... Cs>
inline constexpr bool one_value_type = true;
template <auto V, auto... Vs>
inline constexpr bool one_value_type<value_conv<V>, value_conv<Vs>...> =
    (std::is_same_v<std::remove_cv_t<decltype(V)>,
                    std::remove_cv_t<decltype(Vs)>> &&
     ...);

// `terminal<as<X>(e)>` and `rule<as<X>(e)>`: the conversion of the whole body
// is the rule's own. A terminal may instead give each alternative a constant:
// it is then the data rule over the same body, read where it is named, so its
// words are tokens of their own as in place.
template <auto Body, class E = std::remove_cv_t<decltype(Body)>>
struct terminal_of {
  using type = TerminalRule<Body>;
};
template <auto Body, class C, class E>
struct terminal_of<Body, As<C, E>> {
  using type = TerminalRule<E{}, C>;
};
template <auto Body, class... Cs, class... Es>
struct terminal_of<Body, Choice<As<Cs, Es>...>> {
  static_assert((is_value_conv<Cs> && ...),
                "inside a terminal, as<V>(e) on one alternative must give a "
                "constant value, such as as<true>(...): to convert with a "
                "type or a function, wrap the whole body, "
                "terminal<as<X>(...)>");
  static_assert((word_of<Es> && ...),
                "inside a terminal, an alternative with its own as<V>(e) "
                R"(may only match words: lit<"...">, kw<"...">, )"
                R"(ikw<"..."> or a choice of those. For other text, write a )"
                "data rule");
  static_assert(one_value_type<Cs...>,
                "inside a terminal, the constants of the alternatives' "
                "as<V>(e) have different types: give them one type");
  using type = Rule<Body>;
};
template <auto Body, class E = std::remove_cv_t<decltype(Body)>>
struct rule_of {
  using type = Rule<Body>;
};
template <auto Body, class C, class E>
struct rule_of<Body, As<C, E>> {
  using type = Rule<E{}, C>;
};

}  // namespace detail

// `as<T>(e)` converts with `default_converter<T>`; `as<X>(e)`, for a constant
// X, with the callable X or to the value X.
template <class T, Expr E>
constexpr auto as(E) {
  return detail::as_with<T, std::remove_cvref_t<E>>();
}
template <auto X, Expr E>
constexpr auto as(E) {
  return detail::as_with<typename detail::as_conv<X>::type,
                         std::remove_cvref_t<E>>();
}

namespace detail {

// One converter per rule: the slot holds at most one option, and two would be
// two answers to one question.
template <class L>
struct slot_of {
  using type = void;
};
template <class O>
struct slot_of<TypeList<O>> {
  using type = O;
};
template <class O0, class O1, class... Os>
struct slot_of<TypeList<O0, O1, Os...>> {
  static_assert(dependent_false<O0>,
                "a rule takes at most one converter, and this one is given "
                "two");
  using type = O0;
};
template <class R>
using slot_t = typename slot_of<typename R::slot>::type;

// The converter a slot entry is: one `as<X>` made is itself, a bare target
// type is the default converter for it, an empty slot is none.
template <class O>
struct slot_conv {
  using type = default_converter<O>;
};
template <>
struct slot_conv<void> {
  using type = void;
};
template <class O>
  requires std::is_base_of_v<made_conv, O>
struct slot_conv<O> {
  using type = O;
};
template <class R>
using conv_t = typename slot_conv<slot_t<R>>::type;

// A converter is found by invocability, never by name: a rule may declare
// several `convert` overloads -- the text form and the text-plus-resource
// form are one such pair -- and `R::convert` is not an id-expression the
// moment it does.
//
// The resource is optional and it wins. A converter that has to materialise
// its answer takes the tree's memory resource, allocates there and hands
// back a view of it; one that only slices its input takes text alone. When
// both forms are viable the resource form is the one every reader deduces
// from and the builder calls.
template <class R>
concept ConvertsText = requires(std::string_view t) { R::convert(t); };
//
// And the second parameter is the resource, not something a resource pointer
// merely converts to: a pointer converts implicitly to `bool` and `void*`, so
// `convert(std::string_view, bool keepQuotes)` would pass for this form, win
// over the text form and run with `keepQuotes = true`. A parameter that also
// accepts an `int` or a `void*` is such an impostor, never a resource.
template <class R>
concept ConvertsTextMem =
    requires(std::string_view t, std::pmr::memory_resource* m) {
      R::convert(t, m);
    } && !requires(std::string_view t, int i) { R::convert(t, i); } &&
    !requires(std::string_view t, void* v) { R::convert(t, v); };
// A data rule's converter reads the parts instead -- same optional resource,
// same precedence within the pair. Asked by invocability like the others:
// recognising the parts forms by exclusion would be an id-expression, which
// breaks the moment a rule declares both parts overloads.
template <class R>
concept ConvertsParts = requires(Parts p) { R::convert(p); };
template <class R>
concept ConvertsPartsMem =
    requires(Parts p, std::pmr::memory_resource* m) { R::convert(p, m); } &&
    !requires(Parts p, int i) { R::convert(p, i); } &&
    !requires(Parts p, void* v) { R::convert(p, v); };
// ...and the converter that reads nothing at all.
template <class R>
concept ConvertsNothing = requires { R::convert(); };
template <class R>
concept HasConverter = ConvertsText<R> || ConvertsTextMem<R> ||
                       ConvertsParts<R> || ConvertsPartsMem<R> ||
                       ConvertsNothing<R>;

// The value the retained form answers, resource form first.
template <class R>
struct convert_result {
  using type = decltype(R::convert(std::string_view{}));
};
template <class R>
  requires ConvertsTextMem<R>
struct convert_result<R> {
  using type = decltype(R::convert(std::string_view{},
                                   std::declval<std::pmr::memory_resource*>()));
};
template <class R>
  requires (!ConvertsTextMem<R> && !ConvertsText<R> && ConvertsNothing<R>)
struct convert_result<R> {
  using type = decltype(R::convert());
};

// ...and the same question of the parts form, in the priority order the
// invocation itself uses: resource form first. Without this trait the Parts
// branch would trust the annotation alone, and a `data<NUM, ..., int>` whose
// `convert` returns `double` would truncate at the write, silently.
template <class R>
struct convert_parts_result {
  using type = decltype(R::convert(std::declval<Parts>()));
};
template <class R>
  requires ConvertsPartsMem<R>
struct convert_parts_result<R> {
  using type = decltype(R::convert(std::declval<Parts>(),
                                   std::declval<std::pmr::memory_resource*>()));
};

// A converter answering `ConversionResult<T>` produces a T -- the wrapper is
// the error channel, not the value type. Unwrapping here, in the deduction
// itself, is what makes every reader agree: the dump prints `returns double`,
// a variant's exact-match round compares against `double`, and the payload
// <-> member check reads the type that actually reaches the member.
// Anything unwrapping a second time downstream would be reading a rule type
// the grammar never had.
template <class T>
struct unwrap_result {
  using type = T;
};
template <class T>
struct unwrap_result<ConversionResult<T>> {
  using type = T;
};
template <class T>
using unwrap_result_t = typename unwrap_result<std::remove_cvref_t<T>>::type;

// Forward declarations: `cands<Choice<Es...>>` below needs both, but
// each is only defined further down (branch_type after the ancestor-walk
// machinery it depends on for its own primary template; common_base_of right
// after that machinery too) -- the mutual reference is resolved at
// instantiation time.
template <class A, class Seen = TypeList<>>
struct branch_type;
template <class Branches>
struct common_base_of;

// True when E is literally `create<T>(...)` or `nest<T, &P>(...)` at its own
// top level: this alternative builds its own node, as opposed to a bare
// assign/append/flag that writes into the one implicit node the
// enclosing rule builds regardless of which alternative matched.
template <class E>
inline constexpr bool is_self_creating = false;
template <class T, class E>
inline constexpr bool is_self_creating<Create<T, E>> = true;
template <class T, auto P, class E>
inline constexpr bool is_self_creating<Nest<T, P, E>> = true;

// An alternative resolves its own node when a creation it is bound to reach
// decides the type: a create<>/nest<> at its root, one further along its
// sequence -- the directives written before it land on the node that creation
// makes -- or a choice whose every branch resolves. A repetition may run zero
// times and a directive's payload builds a value, so neither resolves.
template <class E>
inline constexpr bool is_self_resolving = is_self_creating<E>;
template <class... Es>
inline constexpr bool is_self_resolving<Seq<Es...>> =
    (is_self_resolving<Es> || ...);
template <class... Es>
inline constexpr bool is_self_resolving<Choice<Es...>> =
    (is_self_resolving<Es> && ...);
template <class I, class H, class E>
inline constexpr bool is_self_resolving<Skip<I, H, E>> = is_self_resolving<E>;
template <class G, class E>
inline constexpr bool is_self_resolving<Embed<G, E>> = is_self_resolving<E>;
template <auto P, class E>
inline constexpr bool is_self_resolving<Flag<P, E>> = is_self_resolving<E>;

// The candidates a body contributes: created types, and the classes the
// directives write. The payloads of `assign` and `append` build values, not this
// rule's node, so they are not descended into; a flag's operand is read in the
// node in hand, and is. A bare `ref<Sub>` contributes nothing (annotate the
// rule if an alternative is only a reference).
template <class E, class Kids = typename ExprShape<E>::kids>
struct cands {
  using type = TypeList<>;
};
template <class E, class... Ks>
  requires grouping<E>
struct cands<E, TypeList<Ks...>> : tl_cat<typename cands<Ks>::type...> {};

// A choice whose alternatives each resolve their own node -- a creation at
// the root or further along the branch -- contributes the one common base
// that satisfies every branch -- the same question a top-level Or answers
// (deduce_node_type's Or specialization, below), reached one level down
// (nested in a Seq, or under a skip() region) instead of at the rule's own
// top level. The plain flatten-and-concat fallback stays for a mix of bare
// directives (or a mix of directives and creates): those all write into the
// same implicit node, so most_derived over the flattened list is the right
// question there -- blending a nested choice of separate nodes the same way
// would silently pick the most derived alternative's type instead of their
// common base, storing e.g. a Base node behind a Derived* whenever the
// match actually takes the Base branch.
template <class... Es>
struct self_created_common {
  using type = typename common_base_of<
      TypeList<typename branch_type<Es>::type...>>::type;
  static_assert(!std::is_void_v<type>,
                "the alternatives of this nested choice build classes with "
                "no common base: derive these classes from one base class");
};
// The laziness rule (typelist.hpp): a branch is lazy only when it is a class
// template-id whose `::type` is asked afterwards. Spelled as two
// `conditional_t` arguments instead, both would be instantiated and one
// thrown away -- and `self_created_common`'s static_assert would fire on the
// branch that was not chosen.
template <class... Es>
struct cands_or_common {
  using type = TypeList<typename self_created_common<Es...>::type>;
};
template <class... Es>
struct cands_or_cat : tl_cat<typename cands<Es>::type...> {};
template <class... Es>
struct cands<Choice<Es...>, TypeList<Es...>>
    : cond_t<(is_self_resolving<Es> && ...), cands_or_common<Es...>,
             cands_or_cat<Es...>> {};
// Law: a directive only writes where the engine commits. A lookahead matches
// without consuming and commits nothing, so a directive under one never runs
// and contributes no type candidate.
template <class E>
struct lookahead_writes_nothing {
  static_assert(
      std::is_same_v<typename cands<E>::type, TypeList<>>,
      "a create<>, assign<>, append<> or flag<> is inside a condition "
      "(`&` or `!`): a condition only tests the text and builds nothing, so "
      "this directive never runs. Move it out of the condition");
  using type = TypeList<>;
};
template <class E>
struct cands<Not<E>> : lookahead_writes_nothing<E> {};
template <class E>
struct cands<And<E>> : lookahead_writes_nothing<E> {};
template <class D>
struct cands<SuperT<D>> : super_escaped<SuperT<D>> {
  using type = TypeList<>;
};
// Same law, other side: `through()` is a token shape, lexed whole, so no
// directive placed inside it ever runs either.
template <class O, class Cl>
struct cands<Through<O, Cl>> {
  static_assert(
      std::is_same_v<typename cands<O>::type, TypeList<>> &&
          std::is_same_v<typename cands<Cl>::type, TypeList<>>,
      "a create<>, assign<>, append<> or flag<> is inside a through(): "
      "through() matches one token, so this directive never runs. Move it "
      "out of the `through`");
  using type = TypeList<>;
};
template <class Tag, class E>
struct cands<Mark<Tag, E>> {
  static_assert(std::is_same_v<typename cands<E>::type, TypeList<>>,
                "a create<>, assign<>, append<> or flag<> is inside a "
                "mark<>: a mark matches part of one token, so this directive "
                "never runs. Move it out of the `mark<>`");
  using type = TypeList<>;
};
template <class T, class E>
struct cands<Create<T, E>> {
  using type = TypeList<T>;
};
template <class T, auto P, class E>
struct cands<Nest<T, P, E>> {
  using type = TypeList<T>;
};
template <auto P, class E>
struct cands<Assign<P, E>> {
  using type = TypeList<typename Assign<P, E>::node_type>;
};
template <auto P, class E>
struct cands<Append<P, E>> {
  using type = TypeList<typename Append<P, E>::node_type>;
};
template <auto P, class E>
struct cands<Flag<P, E>>
    : tl_cat<TypeList<typename Flag<P, E>::node_type>, typename cands<E>::type> {
};

// First R in the list such that every element is a base of R.
template <class L, class All = L>
struct most_derived {
  using type = void;
};
template <class R, class... Rest, class... All>
struct most_derived<TypeList<R, Rest...>, TypeList<All...>>
    : cond_t<(std::is_base_of_v<All, R> && ...), std::type_identity<R>,
             most_derived<TypeList<Rest...>, TypeList<All...>>> {};

// The elements of `Candidates` that are bases of every branch type.
template <class Candidates, class Branches>
struct keep_bases {
  using type = TypeList<>;
};
template <class R, class... Rest, class... Bs>
struct keep_bases<TypeList<R, Rest...>, TypeList<Bs...>> {
  using tail = typename keep_bases<TypeList<Rest...>, TypeList<Bs...>>::type;
  using type = typename cond_t<(std::is_base_of_v<R, Bs> && ...),
                               tl_cat<TypeList<R>, tail>,
                               std::type_identity<tail>>::type;
};

// All ancestors of T, T first, then the `Extends<>` chain it declares: the
// first entry that is a base of every branch is the most derived common base.
// A node that declares no base ends the walk -- it is its own only ancestor.
template <class T, class = void>
struct ancestor_list {
  using type = TypeList<T>;
};
template <class L>
struct ancestors_of_all;
template <class... Bs>
struct ancestors_of_all<TypeList<Bs...>> {
  using type = tl_cat_t<typename ancestor_list<Bs>::type...>;
};
template <class T>
struct ancestor_list<T, std::void_t<typename T::ast_bases>> {
  using type = tl_cat_t<TypeList<T>,
                        typename ancestors_of_all<typename T::ast_bases>::type>;
};
template <class T>
using ancestor_list_t = typename ancestor_list<T>::type;

// The common base of a list of branch types: the most derived ancestor of
// the first branch that is also a base of every other one. Shared by a
// top-level Or (deduce_node_type's specialization, below) and a nested Or
// whose alternatives each create<>/nest<> their own node
// (cands<Choice<...>>, above) -- both ask the same question about the
// same shape, wherever it sits in the body, and must answer it the same way.
template <class Branches>
struct common_base_of {
  using type = void;
};
template <class B0, class... Bs>
struct common_base_of<TypeList<B0, Bs...>> {
  using type = typename most_derived<typename keep_bases<
      ancestor_list_t<B0>, TypeList<B0, Bs...>>::type>::type;
};

// A branch that named nothing drops out before the common base is asked: only
// a cycle produces one, and a cycle is an answer the other branches carry.
template <class L>
struct drop_void;
template <class... Ts>
struct drop_void<TypeList<Ts...>>
    : tl_cat<cond_t<std::is_void_v<Ts>, TypeList<>, TypeList<Ts>>...> {};

// What a table hands back once an operator matched: the class of the level
// that matched last, so the common base of the binary class, where a binary
// level is listed, and of each unary level's own.
template <class L, bool Unary = is_unary_level<L>>
struct level_class {
  using type = TypeList<>;
};
template <class L>
struct level_class<L, true> {
  using type = TypeList<typename L::node_type>;
};
template <class R, class Lvls = typename R::levels>
struct infix_answer;
template <class R, class... Ls>
struct infix_answer<R, Levels<Ls...>> {
  using type = typename common_base_of<
      tl_cat_t<cond_t<(!is_unary_level<Ls> || ...),
                      TypeList<typename R::node_type>, TypeList<>>,
               typename level_class<Ls>::type...>>::type;
  static_assert(!std::is_void_v<type>,
                "the levels of this infix rule build classes with no common "
                "base: derive these classes from one base class");
};
template <class R>
using infix_answer_t = typename infix_answer<R>::type;

// The classes the node in hand can be at each point of an alternative: the
// head, then each nest's wrapper. A nest buries that node in its member and a
// directive writes into it, so each class must fit the member named -- the
// builder learns the class only at run time.
template <class Ts, class C>
inline constexpr bool all_derive = false;
template <class... Ts, class C>
inline constexpr bool all_derive<TypeList<Ts...>, C> =
    (std::is_base_of_v<C, Ts> && ...);
template <class In, class P>
inline constexpr bool buries_ok = true;
template <class In, class G, class C>
inline constexpr bool buries_ok<In, G* C::*> = all_derive<In, G>;

template <class In, class E, bool Check = true>
struct in_hand {
  using type = In;
};
template <class In, class L, bool Check>
struct in_hand_seq {
  using type = In;
};
template <class In, class E, class... Es, bool Check>
struct in_hand_seq<In, TypeList<E, Es...>, Check>
    : in_hand_seq<typename in_hand<In, E, Check>::type, TypeList<Es...>,
                  Check> {};
template <class In, class... Es, bool Check>
struct in_hand<In, Seq<Es...>, Check>
    : in_hand_seq<In, TypeList<Es...>, Check> {};
template <class In, class... As, bool Check>
struct in_hand<In, Choice<As...>, Check>
    : dedupe<tl_cat_t<typename in_hand<In, As, Check>::type...>> {};

template <std::size_t I, class L, class Js>
struct unordered_rest;
template <std::size_t I, class... Es, std::size_t... Js>
struct unordered_rest<I, TypeList<Es...>, std::index_sequence<Js...>> {
  using type = TypeList<pack_at_t<(Js < I ? Js : Js + 1), Es...>...>;
};
template <std::size_t I, class L>
using unordered_rest_t = typename unordered_rest<
    I, L, std::make_index_sequence<tl_size<L>::value - 1>>::type;

template <class In, class L, bool Check>
struct unordered_hand;
template <class In, class L, bool Check, class Js>
struct unordered_hand_paths;
template <class In, class... Es, bool Check, std::size_t... Js>
struct unordered_hand_paths<In, TypeList<Es...>, Check,
                            std::index_sequence<Js...>>
    : dedupe<tl_cat_t<typename unordered_hand<
          typename in_hand<In, pack_at_t<Js, Es...>, Check>::type,
          unordered_rest_t<Js, TypeList<Es...>>, Check>::type...>> {};
template <class In, class... Es, bool Check>
struct unordered_hand<In, TypeList<Es...>, Check>
    : cond_t<(std::is_same_v<In, typename in_hand<In, Es, Check>::type> && ...),
             std::type_identity<In>,
             unordered_hand_paths<In, TypeList<Es...>, Check,
                                  std::index_sequence_for<Es...>>> {};
template <class In, class... Es, bool Check>
struct in_hand<In, Unordered<Es...>, Check>
    : unordered_hand<In, TypeList<Es...>, Check> {};
// A second turn meets the classes the first one left, and only those: the
// rest were asked already.
template <class Ts, class Seen>
struct hand_new;
template <class... Ts, class Seen>
struct hand_new<TypeList<Ts...>, Seen>
    : tl_cat<cond_t<tl_has<Ts, Seen>::value, TypeList<>, TypeList<Ts>>...> {};
template <class In, std::size_t Mn, std::size_t Mx, class E, bool Check>
struct in_hand<In, Repeat<Mn, Mx, E>, Check> {
  using first = typename in_hand<In, E, Check>::type;
  using again =
      cond_t<(Mx > 1), typename hand_new<first, In>::type, TypeList<>>;
  using type =
      typename dedupe<tl_cat_t<cond_t<Mn == 0, In, TypeList<>>, first,
                               typename in_hand<again, E, Check>::type>>::type;
};
template <class In, class I, class H, class E, bool Check>
struct in_hand<In, Skip<I, H, E>, Check> : in_hand<In, E, Check> {};
template <class In, class G, class E, bool Check>
struct in_hand<In, Embed<G, E>, Check> : in_hand<In, E, Check> {};
template <class In, class E, str T, bool Check>
struct in_hand<In, Doc<E, T>, Check> : in_hand<In, E, Check> {};
template <class In, class T, class E, bool Check>
struct in_hand<In, Create<T, E>, Check> : in_hand<TypeList<T>, E, Check> {};
template <class In, class T, auto P, class E, bool Check>
struct in_hand<In, Nest<T, P, E>, Check> : in_hand<TypeList<T>, E, Check> {
  static_assert(!Check || buries_ok<In, std::remove_cv_t<decltype(P)>>,
                "this nest stores the current node in a member that cannot "
                "hold it: the node created or called before the nest, or the "
                "wrapper of an earlier nest, can be of a class that does not "
                "derive from the member's type. Give the member a type that "
                "every node before the nest derives from");
};
template <class In, auto Sp, bool Check>
struct writes_ok {
  static_assert(!Check || std::is_same_v<In, TypeList<>> ||
                    all_derive<In, spec_class_t<Sp>>,
                "this directive writes a member that the current node may not "
                "have: the alternative adopts a node of a base class, or a "
                "nest wrapped it in another class, and that class does not "
                "declare the member. Adopt the class that declares the "
                "member, or write a member that every class of the current "
                "node declares");
  using type = In;
};
// An operand that creates its node is a region on a head of its own.
template <class In, auto Sp, class E, bool Check>
struct writes_over : writes_ok<In, Sp, Check> {
  using operand = typename in_hand<In, E, Check>::type;
};
template <class In, auto Sp, class E, bool Check>
struct in_hand<In, Assign<Sp, E>, Check> : writes_over<In, Sp, E, Check> {};
template <class In, auto Sp, class E, bool Check>
struct in_hand<In, Append<Sp, E>, Check> : writes_over<In, Sp, E, Check> {};
// The flag writes the node its operand leaves in hand: the wrapper, if the
// operand nests.
template <class In, auto P, class E, bool Check>
struct in_hand<In, Flag<P, E>, Check>
    : writes_ok<typename in_hand<In, E, Check>::type, P, Check> {};

// The type one alternative builds, from what it commits -- and, when it
// commits nothing of its own, from what it adopts (the empty-list
// specialization below, spelled apart so only one of the two instantiates).
// `Seen` is the set of rules whose type is already being deduced on this path.
template <class E, class Seen, class Kids = typename ExprShape<E>::kids>
struct adopt_cands;

// The classes an alternative can hand back, walked as the stage checks them
// but without the checks: one that creates nothing starts on the node it
// adopts. Its type is their common base -- `(create<X>(..) | ..) +
// many(nest<T>(..))` hands back an X or a T. A walk that names no class leaves
// the directives' answer, and the stage makes such a head of that type.
template <class H, class L, class Ad>
struct hand_or_writes {
  using type = typename common_base_of<H>::type;
};
template <class L, class Ad>
struct hand_or_writes<TypeList<>, L, Ad> {
  using type = typename cond_t<std::is_same_v<Ad, TypeList<>>, most_derived<L>,
                               common_base_of<tl_cat_t<L, Ad>>>::type;
};

template <class E>
inline constexpr bool must_adopt = false;
template <class R>
inline constexpr bool must_adopt<Ref<R>> =
    is_node_rule<named_rule_t<R>> || is_infix_rule<named_rule_t<R>>;
template <class... Es>
inline constexpr bool must_adopt<Seq<Es...>> = (must_adopt<Es> || ...);
template <class... Es>
inline constexpr bool must_adopt<Choice<Es...>> = (must_adopt<Es> && ...);
template <class... Es>
inline constexpr bool must_adopt<Unordered<Es...>> = (must_adopt<Es> || ...);
template <std::size_t Mn, std::size_t Mx, class E>
inline constexpr bool must_adopt<Repeat<Mn, Mx, E>> =
    Mn > 0 && must_adopt<E>;
template <class I, class H, class E>
inline constexpr bool must_adopt<Skip<I, H, E>> = must_adopt<E>;
template <auto P, class E>
inline constexpr bool must_adopt<Flag<P, E>> = must_adopt<E>;
template <class G, class E>
inline constexpr bool must_adopt<Embed<G, E>> = must_adopt<E>;
template <class E, str T>
inline constexpr bool must_adopt<Doc<E, T>> = must_adopt<E>;

// The classes declaring the members written ahead of any wrapper: the head a
// path that adopts and creates nothing implies.
template <class E, class Kids = typename ExprShape<E>::kids>
struct head_writes {
  using type = TypeList<>;
};
template <class E, class... Ks>
  requires grouping<E>
struct head_writes<E, TypeList<Ks...>>
    : tl_cat<typename head_writes<Ks>::type...> {};
template <auto Sp, class E>
struct head_writes<Assign<Sp, E>> {
  using type = TypeList<typename Assign<Sp, E>::node_type>;
};
template <auto Sp, class E>
struct head_writes<Append<Sp, E>> {
  using type = TypeList<typename Append<Sp, E>::node_type>;
};
template <auto P, class E>
struct head_writes<Flag<P, E>>
    : tl_cat<TypeList<typename Flag<P, E>::node_type>,
             typename head_writes<E>::type> {};

// The node in hand at the start of an alternative: the node it adopts, else
// the one its writes imply.
template <class Ad, bool Implied, class Hw>
struct start_hand {
  using type = Ad;
};
template <class Hw>
struct start_hand<TypeList<>, true, Hw> {
  using W = typename most_derived<Hw>::type;
  using type = cond_t<std::is_void_v<W>, TypeList<>, TypeList<W>>;
};

template <class A, class Seen, class L>
struct branch_type_from {
  using Ad = typename adopt_cands<A, Seen>::type;
  using Start = typename start_hand<
      Ad, std::is_same_v<Ad, TypeList<>> && !is_self_resolving<A>,
      typename head_writes<A>::type>::type;
  using inferred =
      typename hand_or_writes<typename in_hand<Start, A, false>::type, L,
                              Ad>::type;
  // A cycle's inherited member writes do not determine its adopted head.
  using type =
      cond_t<must_adopt<A> && std::is_same_v<Ad, TypeList<>>, void, inferred>;
  static_assert(must_adopt<A> || !std::is_void_v<type>,
                "the directives of this alternative write members of "
                "unrelated classes: add create<T> to say which class to build");
};
template <class A, class Seen>
struct branch_type : branch_type_from<A, Seen, typename cands<A>::type> {};
template <class T, auto P, class E, class Seen>
struct branch_type<Nest<T, P, E>, Seen> {
  using type = T;
};

// Adoption. A bare reference names no member of its own, so what it produces
// is what the rule it names produces -- that is how a rule whose every
// alternative is a reference gets a type without an annotation.
//
// A dispatch on a bool, never a `&&` or a `conditional_t` over two computed
// answers: the arm not taken would instantiate, and on `A: ref<B>|'a';
// B: ref<A>|'b'` that is a descent nothing stops.
template <class R, class Seen, bool OnPath>
struct adopted_type;
// A rule already on the path answers what it commits directly and descends no
// further: that is the fixed point. `A: name=ID (B | A); B: 'b' A?` settles on
// A's own directive, while a pair that commits nothing anywhere answers void
// and the alternative is refused by name.
template <class R, bool Node>
struct path_type {
  using type = void;
};
// One alternative's own commits write one node, so the most derived; a
// choice of alternatives creating siblings answers their common base, as
// the top-level Or does -- one alternative committing nothing drops out.
template <class E>
struct path_alt {
  using L = typename cands<E>::type;
  using H = typename in_hand<TypeList<>, E, false>::type;
  using type =
      typename cond_t<std::is_same_v<H, TypeList<>>,
                      cond_t<std::is_same_v<L, TypeList<>> || must_adopt<E>,
                             std::type_identity<void>, most_derived<L>>,
                      common_base_of<H>>::type;
};
template <class B>
struct path_body : path_alt<B> {};
template <class... As>
struct path_body<Choice<As...>> {
  using L = typename drop_void<TypeList<typename path_alt<As>::type...>>::type;
  using type = typename cond_t<std::is_same_v<L, TypeList<>>,
                               std::type_identity<void>, common_base_of<L>>::type;
};
template <class I, class H, class E>
struct path_body<Skip<I, H, E>> : path_body<E> {};
template <class G, class E>
struct path_body<Embed<G, E>> : path_body<E> {};
template <class R>
struct path_type<R, true> : path_body<std::remove_cv_t<decltype(R::body)>> {};
// ...and an infix rule on the path settles on its chain's class: the climb
// reaches its own operands through it.
template <class R, bool Infix>
struct on_path_answer
    : path_type<R, std::is_same_v<typename R::kind, detail::node_kind>> {};
template <class R>
struct on_path_answer<R, true> : infix_answer<R> {};
template <class R, class Seen>
struct adopted_type<R, Seen, true> : on_path_answer<R, is_infix_rule<R>> {};

template <class Body, class Seen = TypeList<>>
struct deduce_node_type : branch_type<Body, Seen> {};
template <class A0, class... As, class Seen>
struct deduce_node_type<Choice<A0, As...>, Seen> {
  using Branches = TypeList<typename branch_type<A0, Seen>::type,
                            typename branch_type<As, Seen>::type...>;
  // The same computation cands<Choice<...>> (above) uses for a nested
  // self-creating choice: a top-level Or is just that shape at the rule's own
  // top level.
  using Named = typename drop_void<Branches>::type;
  using type = typename common_base_of<Named>::type;
  // Under a path, a choice whose every branch comes back onto the path
  // answers nothing, as a lone alternative does; walked as the rule's own
  // body, it is a fault.
  static_assert(!std::is_void_v<type> ||
                    (std::is_same_v<Named, TypeList<>> &&
                     !std::is_same_v<Seen, TypeList<>>),
                "the alternatives of this rule build classes with no common "
                "base: derive these classes from one base class, or add "
                "create<T> to say which class each alternative builds");
};
// Where a node type came from, and only one source can lie: a pointer to an
// inherited member carries the class that declares it, so the derived class
// is lost before any deduction runs. A `create<T>` states its type and an
// adoption reads one already deduced; neither can be wrong this way, and
// neither is descended into -- a directive under a create writes the
// created node.
template <class E, class Kids = typename ExprShape<E>::kids>
inline constexpr bool type_from_members = false;
template <auto Sp, class E>
inline constexpr bool type_from_members<Assign<Sp, E>> = true;
template <auto Sp, class E>
inline constexpr bool type_from_members<Append<Sp, E>> = true;
template <auto P, class E>
inline constexpr bool type_from_members<Flag<P, E>> = true;
template <class E, class... Ks>
  requires grouping<E>
inline constexpr bool type_from_members<E, TypeList<Ks...>> =
    (type_from_members<Ks> || ...);

template <class R, class Kind = typename R::kind, class Seen = TypeList<>>
struct rule_type;

// Adopting from a climb. An infix rule hands back the chain's node when an
// operator matched and the primary's when none did, so what a caller adopts
// is what both are -- the rule's own answer is the chain alone.
template <class R, class Seen, bool Climb = is_infix_rule<R>>
struct adopt_answer {
  using type =
      typename rule_type<R, typename R::kind, deduction_path_t<Seen, R>>::type;
};
template <class R, class Seen>
struct adopt_answer<R, Seen, true> {
  using P = typename R::primary;
  // The primary answers as any adopted rule does, this rule on the path so a
  // climb that reaches its own operands settles there: a primary that only
  // chooses between rules answers what they build, not nothing.
  using Path = deduction_path_t<Seen, R>;
  using Both = typename common_base_of<typename drop_void<TypeList<
      infix_answer_t<R>,
      typename adopted_type<P, Path, tl_has<P, Path>::value>::type>>::type>::
      type;
  // An operand of a class unrelated to the chain's answers the chain alone:
  // the caller then names the class it takes.
  using type = cond_t<std::is_void_v<Both>, infix_answer_t<R>, Both>;
};
template <class R, class Seen>
struct adopted_type<R, Seen, false> : adopt_answer<R, Seen> {};
template <class R, class Seen>
struct branch_type<Ref<R>, Seen>
    : adopted_type<named_rule_t<R>, Seen,
                   tl_has<named_rule_t<R>, Seen>::value> {};

// The nodes an alternative adopts: a reference to a node rule produces that
// rule's node, and trivia around one leaves it the only node there is. A
// reference held by `assign` or `append` is not adopted, so those are not
// descended into; a flag around one changes nothing.
template <class E, class Seen, class Kids>
struct adopt_cands {
  using type = TypeList<>;
};
template <class E, class Seen, class... Ks>
  requires grouping<E>
struct adopt_cands<E, Seen, TypeList<Ks...>>
    : tl_cat<typename adopt_cands<Ks, Seen>::type...> {};
template <class R0, class Seen>
struct adopt_cands<Ref<R0>, Seen> {
  using R = named_rule_t<R0>;
  using T = typename cond_t<is_node_rule<R> || is_infix_rule<R>,
                            adopted_type<R, Seen, tl_has<R, Seen>::value>,
                            std::type_identity<void>>::type;
  using type = cond_t<std::is_void_v<T>, TypeList<>, TypeList<T>>;
};
template <auto P, class E, class Seen>
struct adopt_cands<Flag<P, E>, Seen, TypeList<E>> : adopt_cands<E, Seen> {};

// Whether an alternative reaches back onto the path through rules that commit
// nothing of their own -- `'(' Type ')'` where `Type` is `rule<ref<B>>` and `B`
// is being deduced, or a chain of such rules down to `B`: the alternative
// answers nothing here, and the other branches carry the answer.
template <class E, class Seen, class Kids = typename ExprShape<E>::kids>
struct adopt_cycles : std::false_type {};
template <class E, class Seen, class... Ks>
  requires grouping<E>
struct adopt_cycles<E, Seen, TypeList<Ks...>>
    : std::bool_constant<(adopt_cycles<Ks, Seen>::value || ...)> {};
// A rule on the path answers what it commits, and one off it answers through
// its own walk, which carries the path on: either answers nothing only when
// every way out of it comes back onto the path. A rule's own body is walked
// with no path to come back to, and there an answer of nothing is a fault.
template <class R, class Seen, bool OnPath = tl_has<R, Seen>::value,
          bool Walked = OnPath || (is_node_rule<R> &&
                                   !std::is_same_v<Seen, TypeList<>>)>
struct adopt_cycles_of : std::false_type {};
template <class R, class Seen, bool OnPath>
struct adopt_cycles_of<R, Seen, OnPath, true>
    : std::bool_constant<
          std::is_void_v<typename adopted_type<R, Seen, OnPath>::type>> {};
template <class R, class Seen>
struct adopt_cycles<Ref<R>, Seen> : adopt_cycles_of<named_rule_t<R>, Seen> {};
template <auto P, class E, class Seen>
struct adopt_cycles<Flag<P, E>, Seen, TypeList<E>> : adopt_cycles<E, Seen> {};

// An alternative that commits nothing adopts. `'(' Expression ')'` writes no
// member and creates nothing, so what it produces is the node it wraps.
template <class A, class Seen>
struct branch_type_from<A, Seen, TypeList<>> {
  using L = typename adopt_cands<A, Seen>::type;
  static constexpr bool cycles = adopt_cycles<A, Seen>::value;
  static_assert(!std::is_same_v<L, TypeList<>> || cycles,
                "cannot find the class this alternative builds: it has no "
                "create<>, assign<>, append<> or flag<>. Add create<T>");
  using type = typename cond_t<std::is_same_v<L, TypeList<>>,
                               std::type_identity<void>,
                               common_base_of<L>>::type;
  static_assert(std::is_same_v<L, TypeList<>> || !std::is_void_v<type>,
                "the nodes this alternative adopts have no common base: add "
                "create<T> to say which class to build");
};

// An override restating neither slot nor converter keeps the type of the
// rule it stood over.
template <class R>
struct declared_rule_type {
  using type = typename rule_type<inherited_of_t<R>>::type;
};

template <class R, class Seen>
struct body_value;

// Whether a rule passes on a value other than text: a data rule with no slot,
// or an override with no slot over one that does.
template <class R, bool Over = is_overridden<R>,
          bool Data = std::is_same_v<typename R::kind, node_kind> &&
                      slot_empty<R> && !kind_is_node<R>>
struct passes_value : std::false_type {};
template <class R>
struct passes_value<R, false, true>
    : std::bool_constant<!std::is_same_v<typename rule_type<R>::type,
                                         std::string_view>> {};
template <class R, bool Data>
struct passes_value<R, true, Data>
    : cond_t<slot_empty<R>, passes_value<inherited_of_t<R>>,
             std::false_type> {};

// An override with no slot keeps the type of the rule it stood over, and
// where that rule passes a value on, its new body passes the same type.
template <class R, class Seen,
          bool Passes = passes_value<inherited_of_t<R>>::value>
struct overridden_value : declared_rule_type<R> {};
template <class R, class Seen>
struct overridden_value<R, Seen, true> : declared_rule_type<R> {
  static_assert(std::is_same_v<typename body_value<R, Seen>::type,
                               typename declared_rule_type<R>::type>,
                "this override gives a value of another type than the data "
                "rule it overrides: keep the same type, or set it with "
                "as<T>(...)");
};

// The value a data rule passes when it states no conversion: a lone reference
// passes the value of the rule it calls, a choice the value of the branch it
// took, and anything else its joined text. A rule already on the path answers
// `void`: the other branches answer for it.
template <class E, class Seen>
struct value_flow {
  using type = std::string_view;
};
template <class E, class Seen>
using value_flow_t = typename value_flow<E, Seen>::type;
// Conditions around the one element that reads leave it the value.
template <class... Es, class Seen>
  requires(tl_size<readers_t<Es...>>::value == 1)
struct value_flow<Seq<Es...>, Seen>
    : value_flow<tl_at_t<0, readers_t<Es...>>, Seen> {};
template <class E, str T, class Seen>
struct value_flow<Doc<E, T>, Seen> : value_flow<E, Seen> {};
template <class I, class H, class E, class Seen>
struct value_flow<Skip<I, H, E>, Seen> : value_flow<E, Seen> {};
// Dispatched on the path, as `adopted_type` is: the arm not taken must not
// instantiate.
template <class R, class Seen, bool OnPath>
struct called_value {
  using type = void;
};
template <class R, class Seen>
struct called_value<R, Seen, false>
    : rule_type<R, typename R::kind, deduction_path_t<Seen, R>> {};
template <class R0, class Seen>
struct value_flow<Ref<R0>, Seen>
    : called_value<named_rule_t<R0>, Seen,
                   tl_has<named_rule_t<R0>, Seen>::value> {};

// The one type the branches of a choice pass.
template <class L>
struct one_value {
  using type = void;
};
template <class T, class... Ts>
struct one_value<TypeList<T, Ts...>> {
  static_assert((std::is_same_v<T, Ts> && ...),
                "the alternatives of this data rule give values of different "
                "types: wrap the whole body in as<T>(...), or make every "
                "alternative give the same type");
  using type = T;
};
template <class... Bs, class Seen>
struct value_flow<Choice<Bs...>, Seen>
    : one_value<typename drop_void<
          TypeList<value_flow_t<Bs, Seen>...>>::type> {};

template <class R, class Seen>
struct body_value {
  using V = value_flow_t<std::remove_cv_t<decltype(R::body)>,
                         tl_cat_t<Seen, TypeList<R>>>;
  using type = cond_t<std::is_void_v<V>, std::string_view, V>;
};

// The value a rule answers: the slot's converter result, else the rule this
// one was minted over, else, for a data rule, the value its body passes.
template <class R, class Kind, class Seen>
struct rule_type {
  // The converter the slot names, and the rule stays the rule: the two are
  // asked different questions below, and one name for both would have the
  // converter answer "is this rule an override".
  using Cv = conv_t<R>;
  static consteval auto pick() {
    if constexpr (std::is_same_v<typename R::kind, node_kind> &&
                  !slot_empty<R>)
      static_assert(
          !builds_node<std::remove_cv_t<decltype(R::body)>, TypeList<R>>,
          "as<X>(e) converts text, but this e builds an AST node: convert "
          "the text inside the node rule, not the rule itself");
    if constexpr (ConvertsParts<Cv> || ConvertsPartsMem<Cv>) {
      // A parts converter is not callable with text; its result is probed
      // against the forward-declared `Parts` instead.
      using Conv = unwrap_result_t<typename convert_parts_result<Cv>::type>;
      return std::type_identity<Conv>{};
    } else if constexpr (ConvertsText<Cv> || ConvertsTextMem<Cv>) {
      // A `ConversionResult<T>` is a T that may also carry a positioned
      // failure, so the rule's type is T.
      using Conv = unwrap_result_t<typename convert_result<Cv>::type>;
      return std::type_identity<Conv>{};
    } else if constexpr (ConvertsNothing<Cv>) {
      using Conv = unwrap_result_t<decltype(Cv::convert())>;
      return std::type_identity<Conv>{};
    } else if constexpr (requires { typename Cv::result; }) {
      // A bare target: the slot names the type and the tree's own conversion
      // produces it.
      return std::type_identity<typename Cv::result>{};
    } else {
      // A slot nothing reaches: a target type no conversion answers.
      static_assert(slot_empty<R>,
                    "no default_converter<T> for this type -- provide one by "
                    "specialising cursiv::default_converter<T>, or convert "
                    "with a function: as<f>(e)");
      if constexpr (is_overridden<R>)
        return std::type_identity<typename overridden_value<R, Seen>::type>{};
      else if constexpr (std::is_same_v<typename R::kind, node_kind>)
        return std::type_identity<typename body_value<R, Seen>::type>{};
      else
        return std::type_identity<std::string_view>{};
    }
  }
  using type = typename decltype(pick())::type;
};

// An infix rule answers what its climb builds; the shape states it, so there
// is nothing to name.
template <class R, class Seen>
struct rule_type<R, detail::infix_kind, Seen> : infix_answer<R> {};

// `rule<>` covers both genres, so this one specialization answers both: a body
// that commits a node deduces its type, a body that composes text answers the
// slot -- and the value path is the primary above, reached by delegation.
template <class R, class Seen>
struct rule_type<R, node_kind, Seen> {
  using type = typename cond_t<
      !kind_is_node<R>, rule_type<R, detail::terminal_kind, Seen>,
      cond_t<is_overridden<R>, declared_rule_type<R>,
             deduce_node_type<std::remove_cv_t<decltype(R::body)>, Seen>>>::
      type;
};

}  // namespace detail

template <AnyRule R>
using rule_type_t = typename detail::rule_type<R>::type;

// Everything a grammar spells, re-exposed as members so a derived grammar
// writes `terminal<cr<"a-z"> + many(w)>` unqualified. `entry` is required;
// `ignored` and `hidden` default to nothing.

// Grammar inheritance. The parent's `override` primary is hidden here, so a
// grammar that restates nothing inherits its parent's effective rules once
// instead of applying the parent's overrides a second time.
template <class... Parents>
struct extends : Parents... {
  using parents = TypeList<Parents...>;
  template <class>
  struct override;
};

struct GrammarSpec {
  template <class... Ts>
  using list = TypeList<Ts...>;

  template <auto Body>
  using terminal = typename detail::terminal_of<Body>::type;
  template <auto Body>
  using rule = typename detail::rule_of<Body>::type;
  // The infix that builds a `T`, climbing over `T::left`, `T::op` and
  // `T::right`, even where a base of `T` declares them; three member pointers
  // after the levels name other members.
  template <class Primary, class T, auto Lvls, auto... Members>
  using infix = typename detail::infix_of<Primary, T, Lvls, Members...>::type;
  template <class... Parents>
  using extends = cursiv::extends<Parents...>;

  using ignored = TypeList<>;
  using hidden = TypeList<>;

  // Factories.
  // Through `expr`'s factories, so the two spellings of one token cannot
  // drift: `kw<"end">` takes this grammar's `word_boundary` discipline,
  // `lit<"end", !lit<"if">>` the guard it spells for itself.
  template <str S, auto G = detail::no_guard>
  static constexpr auto lit = cursiv::lit<S, G>;
  template <str S>
  static constexpr auto kw = cursiv::kw<S>;
  template <str S>
  static constexpr auto ikw = cursiv::ikw<S>;
  template <str Spec>
  static constexpr auto cr = cursiv::cr<Spec>;
  static constexpr AnyChar any{};
  static constexpr Byte byte{};
  template <class R>
  static constexpr Ref<R> ref{};
  static constexpr Super super{};

  // Directives.
  template <class T>
  static constexpr Create<T, Epsilon> create{};
  template <class T, Expr E>
  static constexpr auto as(E e) {
    return cursiv::as<T>(e);
  }
  template <auto X, Expr E>
  static constexpr auto as(E e) {
    return cursiv::as<X>(e);
  }
  template <auto P, class N>
  static constexpr XRef<P, N> xref{};
  template <auto S>
  static constexpr AssignFn<S> assign{};
  template <auto S>
  static constexpr AppendFn<S> append{};
  template <auto P>
  static constexpr FlagFn<P> flag{};
  template <class T, auto P, Expr E>
  static constexpr auto nest(E e) {
    return cursiv::nest<T, P>(e);
  }
  template <auto P, Expr E>
  static constexpr auto nest(E e) {
    return cursiv::nest<P>(e);
  }
  template <Expr A, Expr B>
  static constexpr auto through(A open, B close) {
    return cursiv::through(open, close);
  }
  template <Expr P, Expr B>
  static constexpr auto commit(P prefix, B body) {
    return cursiv::commit(prefix, body);
  }
  template <class G, Expr A, Expr B>
  static constexpr auto section(A open, B close) {
    return cursiv::section<G>(open, close);
  }

  // Combinators.
  static constexpr auto left = [](Expr auto op) { return cursiv::left(op); };
  static constexpr auto right = [](Expr auto op) { return cursiv::right(op); };
  static constexpr auto nonassoc = [](Expr auto op) {
    return cursiv::nonassoc(op);
  };
  template <auto OpPtr, auto OperandPtr, Expr E>
  static constexpr auto prefix(E op) {
    return cursiv::prefix<OpPtr, OperandPtr>(op);
  }
  template <class T, Expr E>
  static constexpr auto prefix(E op) {
    return cursiv::prefix<T>(op);
  }
  template <auto OperandPtr, auto OpPtr, Expr E>
  static constexpr auto postfix(E op) {
    return cursiv::postfix<OperandPtr, OpPtr>(op);
  }
  template <class T, Expr E>
  static constexpr auto postfix(E op) {
    return cursiv::postfix<T>(op);
  }
  static constexpr LevelsFn levels{};
  static constexpr SkipFn skip{};
  static constexpr TightFn tight{};
  template <class... Rs>
  static constexpr IgnoreTag<Rs...> ignore{};
  template <class... Rs>
  static constexpr HideTag<Rs...> hide{};
  static constexpr SomeFn some{};
  static constexpr ManyFn many{};
  static constexpr OptionFn option{};
  static constexpr UnorderedFn unordered{};
  template <std::size_t Min, std::size_t Max = Min>
  static constexpr RepeatFn<Min, Max> repeat{};
  template <class Tag>
  static constexpr MarkFn<Tag> mark{};
  template <class Tag>
  static constexpr Same<Tag> same{};
  static constexpr Indent indent{};
  static constexpr Dedent dedent{};

  // The six classes of regex: space, word, digit, and their complements.
  static constexpr auto s = cursiv::s;
  static constexpr auto S = cursiv::S;
  static constexpr auto w = cursiv::w;
  static constexpr auto W = cursiv::W;
  static constexpr auto d = cursiv::d;
  static constexpr auto D = cursiv::D;
  using uc = cursiv::uc;
  using gap = cursiv::gap;

  // Which characters a keyword may not run into -- so `kw<"if">` does not
  // match inside `iffy`. A declaration like `ignored` and `hidden`, and
  // overridden the same way: a CSS-flavoured grammar writes
  // `static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;`, one with
  // identifiers past ASCII `cr<"a-zA-Z0-9_$"> | uc::XID_Continue`, and every
  // `kw<>` in it is bounded by that instead; a token may still name its own
  // class.
  static constexpr auto word_boundary = w;
  // Between an opener and its closer the indentation says nothing: a
  // grammar with `indent`/`dedent` names its brackets here, literals or
  // terminals -- `static constexpr auto indent_open = lit<"("> | ref<LBRACK>;`.
  static constexpr Epsilon indent_open{};
  static constexpr Epsilon indent_close{};
  // ...and the terminal that ends a statement, a blank inside them.
  using indent_newline = void;
  static constexpr auto eol = cursiv::eol;
  static constexpr auto eof = cursiv::eof;
  static constexpr std::size_t kUnbounded = cursiv::kUnbounded;
};

}  // namespace cursiv
