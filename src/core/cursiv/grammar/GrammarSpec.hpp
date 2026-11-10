#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/CommitLowering.hpp"
#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/grammar/ExprShape.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/utils/ctstring.hpp"
#include "cursiv/utils/typelist.hpp"

// The declarative layer: the rule kinds whose body is a template argument
// (`terminal`, `rule`, `infix`, overrides), the type each rule produces, and
// the `GrammarSpec` base a grammar derives from.

namespace cursiv {

// A rule is referred to (`ref<R>`), never combined; its body is an NTTP, so
// a rule can name itself or a sibling declared later. A kind spelled
// directly, such as `ref<terminal<body>>`, is an anonymous inline rule.

namespace detail {

// A rule value used as a body (`rule<ID{}>`) is the one misuse no combinator
// can refuse, since a rule is not an expression.
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

namespace detail {

template <class G, class X>
struct InGrammar;

// A grammar is named whole: `section<G>` reads G's entry as G alone reads it.
template <class R>
inline constexpr bool is_grammar = std::is_base_of_v<GrammarSpec, R>;

template <class R>
struct named_rule {
  using type = R;
};
template <class R>
  requires is_grammar<R>
struct named_rule<R> {
  using type = InGrammar<R, typename R::entry>;
};

// The rule a reference reads.
template <class R>
using named_rule_t = typename named_rule<R>::type;

template <class E>
inline constexpr bool is_as = false;
template <class C, class E>
inline constexpr bool is_as<As<C, E>> = true;
template <class E, class Children = typename ExprShape<E>::children>
inline constexpr bool holds_as = is_as<E>;
template <class E, class... Ks>
inline constexpr bool holds_as<E, TypeList<Ks...>> =
    is_as<E> || (holds_as<Ks> || ...);

// A body's `as<X>(e)` becomes an inline data rule over `e` with the
// converter. That rule is never called: an engine reads `e` in place.
template <class E, bool Holds = holds_as<E>>
struct as_resolved {
  using type = E;
};
template <class E>
using as_resolved_t = typename as_resolved<E>::type;
template <class E, class Children>
struct as_rebuilt;
template <class E, class... Ks>
struct as_rebuilt<E, TypeList<Ks...>> {
  using type = typename ExprShape<E>::template rebind<as_resolved_t<Ks>...>;
};
template <class E>
struct as_resolved<E, true> : as_rebuilt<E, typename ExprShape<E>::children> {};
template <class C, class E>
struct as_resolved<As<C, E>, true> {
  using type = Ref<Rule<E{}, C>>;
};

// One token, lexed: no trivia is skipped inside. The optional slot names what
// the token's text becomes.
template <auto Body, class... Opts>
struct TerminalRule {
  static_assert(body_not_a_rule<Body>::ok);
  static_assert(!holds_as<std::remove_cv_t<decltype(Body)>>,
                "inside a terminal, as<X>(e) must wrap the whole body or "
                "each of its alternatives: write terminal<as<X>(...)> or "
                "terminal<as<A>(...) | as<B>(...)>");
  static_assert(!holds_commit<std::remove_cv_t<decltype(Body)>>,
                "a commit is allowed in a rule, not in a terminal: inside a "
                "terminal, use option, many, some or repeat without commit");
  using kind = terminal_kind;
  using slot = TypeList<Opts...>;
  static constexpr auto body = Body;
};

// A rule covers both a node and a value, and the body decides which: what it
// commits (a create, a nest, a directive, a reference to a builder) makes it a
// node rule, and a slot makes it a value rule outright.
template <auto Body, class... Opts>
struct Rule {
  static_assert(body_not_a_rule<Body>::ok);
  static_assert(
      !misplaced_commit<as_resolved_t<std::remove_cv_t<decltype(Body)>>>,
      "commit(prefix, body) must be the direct item of option, "
      "many, some or repeat, as in many(commit(a, b))");
  using kind = node_kind;
  using slot = TypeList<Opts...>;
  static constexpr auto body =
      commit_free_t<as_resolved_t<std::remove_cv_t<decltype(Body)>>>{};
};

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

// A condition consumes nothing, bare or under a repetition or a wrapper that
// reads only its child.
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

// The elements of a sequence that consume input: all but its conditions.
template <class... Es>
using readers_t =
    tl_cat_t<cond_t<is_condition<Es>, TypeList<>, TypeList<Es>>...>;

// Whether a sequence builds a node, which only complete rules can say.
template <class E>
struct sequence_builds_node;

// A region, a mark or doc text adds to what it holds and takes nothing from it.
template <class E>
inline constexpr bool is_wrapper = false;
template <class I, class H, class E>
inline constexpr bool is_wrapper<Skip<I, H, E>> = true;
template <class Tag, class E>
inline constexpr bool is_wrapper<Mark<Tag, E>> = true;
template <class E, str T>
inline constexpr bool is_wrapper<Doc<E, T>> = true;

// A sequence of several reading elements is a data rule written in place.
// With `Complete`, a sequence that builds a node stays as it is.
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
template <class E, class Children>
struct operands_rebuilt;
template <class E, class... Ks>
struct operands_rebuilt<E, TypeList<Ks...>> {
  using type =
      typename ExprShape<E>::template rebind<unnamed_data_operands_t<Ks>...>;
};
template <class E>
struct unnamed_data_operands<E, true>
    : operands_rebuilt<E, typename ExprShape<E>::children> {};
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
// postfix)*`, an operand being `prefix* primary`. Every analysis reads this;
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

// A chain of operators as a rule kind: the primary rule for the operands,
// the left, operator and right members, and the level table. A binary level
// builds `Node`; a prefix or postfix level builds the class it names.
template <class Primary, auto LeftPtr, auto OpPtr, auto RightPtr, auto Lvls,
          class Node>
struct InfixRule {
  using kind = infix_kind;
  using slot = TypeList<>;
  using primary = Primary;
  using node_type = Node;
  using levels = inline_operators_t<std::remove_cv_t<decltype(Lvls)>>;
  static constexpr auto left_pointer = LeftPtr;
  static constexpr auto op_pointer = OpPtr;
  static constexpr auto right_pointer = RightPtr;
  static constexpr auto body = typename infix_body<Primary, levels>::type{};
};

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

template <class R>
concept AnyRule = requires {
  typename R::kind;
  R::body;
};

// An override keeps the rule's name: the effective rule is built from the base
// and the override, and `declared` is the rule every diagnostic and dump
// prints.

template <class R>
struct declared_of {
  using type = R;
};
// Recursive: an override of an override still names the rule the author wrote.
template <class R>
  requires requires { typename R::declared; }
struct declared_of<R> : declared_of<typename R::declared> {};
template <class R>
using declared_of_t = typename declared_of<R>::type;

template <class R>
inline constexpr bool is_overridden = requires { typename R::declared; };

// The rule an override replaces: the parents' effective rule, or the declared
// rule when no parent overrides it.
template <class R>
struct inherited_of : declared_of<R> {};
template <class R>
  requires requires { typename R::inherited; }
struct inherited_of<R> {
  using type = typename R::inherited;
};
template <class R>
using inherited_of_t = typename inherited_of<R>::type;

// The label of an inline rule, which has no identifier: every place that
// names a rule reads it instead of a template-id.
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

// The deductions below walk a rule's body into the rules it names, with
// the path of rules they are in, so a cycle stops. A callee gets only the
// path's rules it reaches again, so each rule outside a cycle has one answer.

// The references a deduction can follow: through the shapes that gather or
// wrap their children, never into a directive's payload or a lookahead.
template <class E>
inline constexpr bool wraps_calls = false;
template <class E, str T>
inline constexpr bool wraps_calls<Doc<E, T>> = true;
template <class Tag, class E>
inline constexpr bool wraps_calls<Mark<Tag, E>> = true;
template <auto P, class E>
inline constexpr bool wraps_calls<Flag<P, E>> = true;
template <class E, class Children = typename ExprShape<E>::children>
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
// rule's primary. A rule read in a section adds those the rule that applies
// there names (`InGrammar.hpp`).
template <class R, bool Infix = std::is_same_v<typename R::kind, infix_kind>>
struct rule_calls : dedupe<body_calls_t<R>> {};
template <class R>
struct rule_calls<R, true>
    : dedupe<tl_cat_t<TypeList<typename R::primary>, body_calls_t<R>>> {};
template <class R>
struct deduction_calls : rule_calls<R> {};

// Every rule reached from R in one step or more, in the order a breadth-first
// walk reaches them; `Calls<X>::type` names the steps from X.
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
// in the order R reaches them, so one set gives one path whatever the order it
// was walked in.
template <template <class> class Calls, class Seen, class R>
struct path_into;
template <template <class> class Calls, class R>
struct path_into<Calls, TypeList<>, R> {
  using type = TypeList<>;
};
template <template <class> class Calls, class... Ys, class R>
struct path_into<Calls, TypeList<Ys...>, R>
    : in_reach_order<TypeList<Ys...>, typename reached_by<Calls, R>::type> {};
// The path R's own walk carries: the rules of `Seen` it can reach, then R.
template <class Seen, class R>
using deduction_path_t =
    tl_cat_t<typename path_into<deduction_calls, Seen, R>::type, TypeList<R>>;

// Whether a `rule<>` builds a node: its body commits a create, a nest, a
// directive, or a reference to a node rule. A slot makes it a value rule. A
// rule already on the `Seen` path commits nothing new.
template <class R, class Seen, bool OnPath>
struct rule_builds_node_of;

template <class E, class Seen, class Children = typename ExprShape<E>::children>
inline constexpr bool builds_node = false;
template <class E, class Seen, class... Ks>
  requires grouping<E>
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
                        tl_has<named_rule_t<R>, Seen>::value>::value;

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
          bool Walks =
              std::is_same_v<typename R::kind, node_kind> && slot_empty<R>>
struct body_builds_node
    : std::bool_constant<std::is_same_v<typename R::kind, infix_kind>> {};
template <class R, class Seen>
struct body_builds_node<R, Seen, true>
    : std::bool_constant<builds_node<std::remove_cv_t<decltype(R::body)>,
                                     deduction_path_t<Seen, R>>> {};
template <class R, class Seen>
struct rule_builds_node_of<R, Seen, false> : body_builds_node<R, Seen> {};

template <class R>
inline constexpr bool kind_is_node =
    rule_builds_node_of<R, TypeList<>, false>::value;

// A rule declared `rule<>` whose body commits nothing is a value rule: it
// composes its text from the parts.
template <class R>
inline constexpr bool is_data_rule =
    std::is_same_v<typename R::kind, node_kind> && !kind_is_node<R>;
template <class R>
inline constexpr bool is_node_rule =
    std::is_same_v<typename R::kind, node_kind> && kind_is_node<R>;
template <class R>
inline constexpr bool is_infix_rule =
    std::is_same_v<typename R::kind, infix_kind>;

template <class... Es>
struct sequence_builds_node<Seq<Es...>>
    : std::bool_constant<builds_node<Seq<Es...>, TypeList<>>> {};

// A rule's body as declared. An infix rule's is its `infix_body`.
template <class R>
using declared_body_t = std::remove_cv_t<decltype(R::body)>;

// A create at the root of a body is the rule's own creation, not a region
// inside it: readers see its operand, and the node is created in front of it.
template <class B>
struct strip_root_create {
  using type = B;
};
template <class T, class E>
struct strip_root_create<Create<T, E>> {
  using type = E;
};
template <class R>
using rule_body_t = typename strip_root_create<declared_body_t<R>>::type;

}  // namespace detail

// A value rule answers its slot: the converter's result, the target type,
// else the text. A node rule answers the type it creates, else the most
// derived class its directives write, else the node it adopts.

// Declared only: `Conversion.hpp` defines it, and `unwrap_result` must
// recognise it.
template <class T>
struct ConversionResult;

// The parts a data rule joins, each leaf through its own converter: a parts
// converter never sees the raw bytes.
using Parts = std::span<const std::string_view>;

// A terminal and a value rule name what their text becomes with `as<X>`
// around the whole body: a target type, a callable reading the text or the
// parts, or a constant. Name a lambda before using it in an exported rule.

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
};
template <auto F>
struct parts_conv : made_conv {
  static constexpr decltype(auto) convert(Parts p)
    requires requires { F(p); }
  {
    return F(p);
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

// Whether a class is a converter: it has one call operator of any shape, which
// `as_conv` then judges, or it reads what a converter reads, as an overloaded
// or generic one does.
template <class T>
concept declares_call =
    std::is_class_v<T> &&
    (requires { &T::operator(); } || std::is_invocable_v<const T&> ||
     std::is_invocable_v<const T&, std::string_view> ||
     std::is_invocable_v<const T&, Parts>);
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
  static constexpr bool text = requires(std::string_view t) { X(t); };
  static constexpr bool parts = requires(Parts p) { X(p); };
  static_assert(none || text || parts,
                "as<f>(e): f must take the text (std::string_view), the "
                "parts (std::span<const std::string_view>) or no argument");
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

// `terminal<as<X>(e)>` and `rule<as<X>(e)>`: the whole body's conversion.
// A terminal that gives each alternative a constant is the data rule over the
// same body.
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

// One converter per rule: the slot holds at most one option.
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

// Found by invocability, never by name: with several `convert` overloads (the
// text and parts forms), `R::convert` alone is not a valid expression.
template <class R>
concept ConvertsText = requires(std::string_view t) { R::convert(t); };
template <class R>
concept ConvertsParts = requires(Parts p) { R::convert(p); };
template <class R>
concept ConvertsNothing = requires { R::convert(); };
template <class R>
concept HasConverter =
    ConvertsText<R> || ConvertsParts<R> || ConvertsNothing<R>;

// The type a text converter, or one that reads nothing, returns.
template <class R>
struct convert_result {
  using type = decltype(R::convert(std::string_view{}));
};
template <class R>
  requires(!ConvertsText<R> && ConvertsNothing<R>)
struct convert_result<R> {
  using type = decltype(R::convert());
};

// The type a parts converter returns.
template <class R>
struct convert_parts_result {
  using type = decltype(R::convert(std::declval<Parts>()));
};

// A converter answering `ConversionResult<T>` produces a T: unwrapped here,
// once, so every reader sees the same type.
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

// Defined further down; `cands<Choice<Es...>>` needs both.
template <class A, class Seen = TypeList<>>
struct branch_type;
template <class Branches>
struct common_base_of;

// Whether E is `create<T>(...)` or `nest<T, &P>(...)` at its top level: an
// alternative that builds its own node, not one that writes into the rule's.
template <class E>
inline constexpr bool is_self_creating = false;
template <class T, class E>
inline constexpr bool is_self_creating<Create<T, E>> = true;
template <class T, auto P, class E>
inline constexpr bool is_self_creating<Nest<T, P, E>> = true;

// An alternative resolves its own node when a creation it always reaches
// decides the type: at its root, further along its sequence, or in every
// branch of a choice. A repetition or a directive's payload does not.
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
// directives write. `assign` and `append` payloads build values and are not
// descended into. A bare `ref<Sub>` contributes nothing.
template <class E, class Children = typename ExprShape<E>::children>
struct cands {
  using type = TypeList<>;
};
template <class E, class... Ks>
  requires grouping<E>
struct cands<E, TypeList<Ks...>> : tl_cat<typename cands<Ks>::type...> {};

// A nested choice whose alternatives each resolve their own node
// contributes their common base, as a top-level choice does. A mix of bare
// directives writes into one node, and keeps the flattened list.
template <class... Es>
struct self_created_common {
  using type = typename common_base_of<
      TypeList<typename branch_type<Es>::type...>>::type;
  static_assert(!std::is_void_v<type>,
                "the alternatives of this nested choice build classes with "
                "no common base: derive these classes from one base class");
};
// Two class templates, not a `conditional_t`: only the branch taken may be
// instantiated (see typelist.hpp).
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
// A directive writes only where the engine commits. A lookahead consumes and
// commits nothing, so a directive under one never runs and gives no candidate.
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
// Likewise, `through()` is lexed as one token, so no directive inside it runs.
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
// the first that is a base of every other one.
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

// The classes the current node can be at each point of an alternative: the
// head, then each nest's wrapper. Each must fit the member a nest or
// directive names.
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
// A second turn asks the classes the first one left, and only those: the
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
// The flag writes the node its operand leaves current: the wrapper, if the
// operand nests.
template <class In, auto P, class E, bool Check>
struct in_hand<In, Flag<P, E>, Check>
    : writes_ok<typename in_hand<In, E, Check>::type, P, Check> {};

// The type one alternative builds from what it commits, else from what it
// adopts. `Seen` is the rules whose type is being deduced on this path.
template <class E, class Seen, class Children = typename ExprShape<E>::children>
struct adopt_cands;

// The common base of the classes an alternative can hand back:
// `(create<X>(..) | ..) + many(nest<T>(..))` hands back an X or a T.
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
template <class E, class Children = typename ExprShape<E>::children>
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

// The current node at the start of an alternative: the node it adopts, else
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

// Adoption: a bare reference produces what the rule it names produces, so
// a rule of references needs no annotation. A dispatch on a bool, so the arm
// not taken is never instantiated.
template <class R, class Seen, bool OnPath>
struct adopted_type;
// A rule already on the path answers what it commits and descends no
// further. A cycle that commits nothing answers void, and is refused by
// name.
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
// An infix rule on the path answers its chain's class: the climb reaches its
// own operands through it.
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
// Whether the node type comes from the members written: a pointer to an
// inherited member names its declaring class, so this source alone can be
// wrong. A `create<T>` and an adoption are not descended into.
template <class E, class Children = typename ExprShape<E>::children>
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
  // climb that reaches its own operands ends there: a primary that only
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

// The nodes an alternative adopts: a reference to a node rule, alone among
// trivia. A reference held by `assign` or `append` is not adopted.
template <class E, class Seen, class Children>
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

// Whether an alternative reaches back onto the path through rules that
// commit nothing: it answers nothing, and the other branches answer.
template <class E, class Seen, class Children = typename ExprShape<E>::children>
struct adopt_cycles : std::false_type {};
template <class E, class Seen, class... Ks>
  requires grouping<E>
struct adopt_cycles<E, Seen, TypeList<Ks...>>
    : std::bool_constant<(adopt_cycles<Ks, Seen>::value || ...)> {};
// A rule answers nothing only when every way out of it comes back onto the
// path. Walking its own body, an answer of nothing is a fault.
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
// rule it replaces.
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

// An override with no slot keeps the type of the rule it replaces, and where
// that rule passes a value on, its new body passes the same type.
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

// The value a data rule passes with no conversion: a lone reference passes
// its rule's value, a choice its branch's, anything else its joined text. A
// rule on the path answers `void`.
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

// The value a rule answers: the slot's converter result, else the type of the
// rule it overrides, else, for a data rule, the value its body passes.
template <class R, class Kind, class Seen>
struct rule_type {
  using Cv = conv_t<R>;
  static consteval auto pick() {
    if constexpr (std::is_same_v<typename R::kind, node_kind> &&
                  !slot_empty<R>)
      static_assert(
          !builds_node<std::remove_cv_t<decltype(R::body)>, TypeList<R>>,
          "as<X>(e) converts text, but this e builds an AST node: convert "
          "the text inside the node rule, not the rule itself");
    if constexpr (ConvertsParts<Cv>) {
      // A parts converter is not callable with text; its result is probed
      // against the forward-declared `Parts` instead.
      using Conv = unwrap_result_t<typename convert_parts_result<Cv>::type>;
      return std::type_identity<Conv>{};
    } else if constexpr (ConvertsText<Cv>) {
      // A `ConversionResult<T>` is a T that may also carry a positioned
      // failure, so the rule's type is T.
      using Conv = unwrap_result_t<typename convert_result<Cv>::type>;
      return std::type_identity<Conv>{};
    } else if constexpr (ConvertsNothing<Cv>) {
      using Conv = unwrap_result_t<decltype(Cv::convert())>;
      return std::type_identity<Conv>{};
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

// An infix rule answers what its chain builds.
template <class R, class Seen>
struct rule_type<R, detail::infix_kind, Seen> : infix_answer<R> {};

// `rule<>` covers both: a body that commits a node deduces its type, and a
// body that composes text answers as a value rule, through the primary above.
template <class R, class Seen>
struct rule_type<R, node_kind, Seen> {
  using type = typename cond_t<
      !kind_is_node<R>, rule_type<R, detail::terminal_kind, Seen>,
      cond_t<is_overridden<R>, declared_rule_type<R>,
             deduce_node_type<std::remove_cv_t<decltype(R::body)>, Seen>>>::
      type;
};

template <AnyRule R>
using rule_type_t = typename rule_type<R>::type;

}  // namespace detail

// Grammar inheritance. It hides the parents' `override` primary, so a grammar
// that overrides nothing inherits their effective rules once instead of
// applying their overrides again.
template <class... Parents>
struct extends : Parents... {
  using parents = TypeList<Parents...>;
  template <class>
  struct override;
};

// Everything a grammar spells, as members, so a derived grammar writes
// `terminal<cr<"a-z"> + many(w)>` unqualified. A generated grammar needs an
// `entry`; `ignored` and `hidden` default to nothing.
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

  // Forwarded to `expr.hpp`, so both spellings of a token agree: `kw<"end">`
  // takes this grammar's `word_boundary`.
  template <str S, auto G = detail::no_guard>
  static constexpr auto lit = cursiv::lit<S, G>;
  template <str S>
  static constexpr auto kw = cursiv::kw<S>;
  template <str S>
  static constexpr auto ikw = cursiv::ikw<S>;
  template <str Spec>
  static constexpr auto cr = cursiv::cr<Spec>;
  static constexpr detail::AnyChar any{};
  static constexpr Byte byte{};
  template <class R>
  static constexpr Ref<R> ref{};
  static constexpr detail::Super super{};

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
  static constexpr detail::XRef<P, N> xref{};
  template <auto S>
  static constexpr detail::AssignFn<S> assign{};
  template <auto S>
  static constexpr detail::AppendFn<S> append{};
  template <auto P>
  static constexpr detail::FlagFn<P> flag{};
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
  static constexpr detail::LevelsFn levels{};
  static constexpr detail::SkipFn skip{};
  static constexpr detail::TightFn tight{};
  template <class... Rs>
  static constexpr detail::IgnoreTag<Rs...> ignore{};
  template <class... Rs>
  static constexpr detail::HideTag<Rs...> hide{};
  static constexpr detail::SomeFn some{};
  static constexpr detail::ManyFn many{};
  static constexpr detail::OptionFn option{};
  static constexpr detail::UnorderedFn unordered{};
  template <std::size_t Min, std::size_t Max = Min>
  static constexpr detail::RepeatFn<Min, Max> repeat{};
  template <class Tag>
  static constexpr detail::MarkFn<Tag> mark{};
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

  // The characters a keyword may not run into, so `kw<"if">` does not match
  // inside `iffy`. Override it like `ignored`, such as
  // `static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;`.
  static constexpr auto word_boundary = w;
  // Whether every literal and keyword ignores letter case, as `ikw<>` does:
  // `static constexpr bool case_insensitive = true;`.
  static constexpr bool case_insensitive = false;
  // Between an opener and its closer the indentation says nothing: a
  // grammar with `indent`/`dedent` names its brackets here, literals or
  // terminals -- `static constexpr auto indent_open = lit<"("> | ref<LBRACK>;`.
  static constexpr Epsilon indent_open{};
  static constexpr Epsilon indent_close{};
  // The terminal that ends a statement; between an opener and its closer it
  // is trivia.
  using indent_newline = void;
  static constexpr auto eol = cursiv::eol;
  static constexpr auto eof = cursiv::eof;
  static constexpr std::size_t kUnbounded = cursiv::kUnbounded;
};

}  // namespace cursiv
