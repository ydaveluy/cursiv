#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "cursiv/grammar/Association.hpp"
#include "cursiv/grammar/RuleTraits.hpp"
#include "cursiv/text/unicode/UnicodeProperty.hpp"
#include "cursiv/text/unicode/UnicodeRanges.hpp"
#include "cursiv/syntax-tree/MemberTraits.hpp"
#include "cursiv/text/ClassItem.hpp"
#include "cursiv/text/ClassRanges.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/ctstring.hpp"
#include "cursiv/utils/typelist.hpp"

// Grammar expressions. Every type is an empty structural tag, so a rule's
// body can be a template argument. `operator+` flattens sequences and
// normalizes each into one head creation and a chain of nests (see below).

namespace cursiv::detail {

struct GrammarNode {};

// What the operators and combinators accept; a rule is referred to, not
// combined. Class traits rather than `std::derived_from`: they add no symbol
// per operand.
template <class T>
concept Expr = std::is_base_of_v<GrammarNode, std::remove_cvref_t<T>> &&
               std::is_convertible_v<const volatile std::remove_cvref_t<T>*,
                                     const volatile GrammarNode*>;

inline constexpr std::size_t kUnbounded =
    std::numeric_limits<std::size_t>::max();

// The guard of `kw<>`: the grammar's `word_boundary`, resolved once the
// grammar is known.
struct WordBound {};

// `lit<S>`'s guard when none is given.
struct NoGuard {};
inline constexpr NoGuard no_guard{};

// Exact text, behind every literal spelling. `CI` folds case; `Guard` runs
// right after the match, with no skipper between: `void` is none,
// `WordBound` the grammar's `word_boundary`, else applied as written.
template <class E, str T>
struct Doc;

template <str S, bool CI = false, class Guard = void>
struct Lit : GrammarNode {
  static_assert(S.size() > 0, "lit<> needs a non-empty literal");
  static_assert(std::is_void_v<Guard> || std::is_same_v<Guard, WordBound> ||
                    std::derived_from<Guard, GrammarNode>,
                "lit<S, G>: the guard G must be a grammar expression, "
                "checked right after the text, such as "
                R"(`lit<"toto", !lit<"titi">>`. For a keyword that must not )"
                "run into the next word, write `kw<S>` instead");

  // The text a hover shows over this word.
  template <str D>
  consteval auto doc() const {
    return Doc<Lit<S, CI, Guard>, D>{};
  }
};

// Hover text as a node wrapping what it documents, so two equal literals stay
// one type. A later doc replaces: `.doc<A>().doc<B>()` is B.
template <class E, str T>
struct Doc : GrammarNode {
  template <str D>
  consteval auto doc() const {
    return Doc<E, D>{};
  }
};

// A class of characters: the Unicode properties it names, one bit each, the
// scalars spelled beside them in canonical form, and whether it is
// complemented. Two spellings of one set are one type.
template <std::uint64_t Props, bool Not, str Spec>
struct CpSet : GrammarNode {};

// One character: a scalar, or the byte where none starts.
using AnyChar = CpSet<0, true, "">;

// One raw byte, whatever it is.
struct Byte : GrammarNode {};

// The end of the text: zero width.
struct Eof : GrammarNode {};

// `open + many(!close + any) + close`: the first closer ends the run.
template <class Open, class Close>
struct Through : GrammarNode {};

// The bytes a run of the token chose, named, and their echo further on in
// the same token: a long bracket's `=`s, a raw string's delimiter, a
// heredoc's word.
template <class Tag, class E>
struct Mark : GrammarNode {};
template <class Tag>
struct Same : GrammarNode {};

// A line deeper than the one that opened the block, and one back at an
// enclosing level: each is a terminal's whole body, read at a line start
// against the indentation of the enclosing lines, never against a width.
struct Indent : GrammarNode {};
struct Dedent : GrammarNode {};
// A condition on the bytes between the previous token and the current one,
// comments included: `GapHas` holds where some run of them is read by E,
// `GapIs` where all of them are. Zero width; `!` and `|` compose them.
template <class E>
struct GapHas : GrammarNode {};
template <class E>
struct GapIs : GrammarNode {};
// The zero-width token standing for `Tok` where `Cond` holds and the current
// token is refused: `implied(tok, cond)` is `tok | Implied<Tok, Cond>`, so a
// walk reaches `tok` as any alternative.
template <class Tok, class Cond>
struct Implied : GrammarNode {};

// `Seq<A>` is A and `Seq<>` is nothing, so both are unspellable; same for the
// choices.
template <class... Es>
struct Seq : GrammarNode {
  static_assert(sizeof...(Es) >= 2,
                "Seq needs at least two elements: build sequences with "
                "operator+");
};

// The choice, spelled `a | b`, is ordered: the arm written first wins a tie.
template <class... Es>
struct Choice : GrammarNode {
  static_assert(sizeof...(Es) >= 2,
                "Choice needs at least two alternatives: build choices "
                "with operator|");
};

template <std::size_t Min, std::size_t Max, class E>
struct Repeat : GrammarNode {
  static_assert(Min <= Max, "repeat<Min, Max>: Min must not exceed Max");
  // A repetition that may match at most nothing neither consumes nor decides.
  static_assert(Max != 0, "repeat<Min, Max>: Max must be at least one");
};

// The item of a quantifier that may stop only where `Prefix` cannot be read:
// `Prefix + Body` is the item. It appears nowhere else.
template <class Prefix, class Body>
struct Commit : GrammarNode {};

// What a commit's exit asks, under `Not`: its prefix, which must consume.
template <class E>
struct Trigger : GrammarNode {};

// Zero-width lookaheads.
template <class E>
struct Not : GrammarNode {};
template <class E>
struct And : GrammarNode {};

// Each consuming operand once, in any order. A repetition stays one block, and
// so does a group nested as an operand.
template <class... Es>
struct Unordered : GrammarNode {
  static_assert(sizeof...(Es) >= 2,
                "an unordered group needs at least two items: "
                "`unordered(a, b)`");
};

// Names R as a template argument, so R may still be incomplete: a grammar can
// be recursive, and a rule can reference itself from its own base-clause.
template <class R>
struct Ref : GrammarNode {};

// `as<X>(e)`: what E matches converts with C. The annotation reads nothing of
// its own: a rule's body turns it into a data rule over E, so E's tokens stay
// what they are.
template <class C, class E>
struct As : GrammarNode {};

template <auto Body, class... Opts>
struct Rule;
template <template <auto, class> class Directive, auto S, class E>
struct directive_operand;

// `E` is `Seq<open, Ref<G>, close>`: `G`'s entry between two tokens of the
// caller. A directive over the section is written on the `Ref<G>`.
template <class G, class E>
struct Embed : GrammarNode {};

// The overridden body, inside an override's body; `plug` splices the
// parent's body over it. A template, so each walk's refusal fires on use.
template <class = void>
struct SuperT : GrammarNode {};
using Super = SuperT<>;

// The empty payload of a bare `create<T>`: matches nothing, consumes nothing,
// never fails. It is what makes the empty alternative spellable.
struct Epsilon : GrammarNode {};

// What a match builds. How a member receives its value (slice, converted
// value, child pointer, cross-reference) follows from the member's type.

// A region: what E matched is a T.
template <class T, class E>
struct Create : GrammarNode {};

// `create<T>` is the region with the empty payload; calling it gives the
// payload.
template <class T>
struct Create<T, Epsilon> : GrammarNode {
  template <Expr E>
  constexpr Create<T, std::remove_cvref_t<E>> operator()(E) const {
    return {};
  }
};

template <class E>
inline constexpr bool is_create = false;
template <class T, class E>
inline constexpr bool is_create<Create<T, E>> = true;

// Beside `is_create`, as the combinators below refuse both; `Nest` is declared
// later.
template <class E>
inline constexpr bool is_nest = false;

// What a directive writes: a bare member pointer, or an `xref<P, N>` that
// names the member and the classes `N` this site accepts. `N` filters the
// link; it never changes the member's type.
template <auto P, class N>
struct XRef {};

template <class T, auto S>
struct spec_of {
  static constexpr auto ptr = S;
  using narrow = void;
};
template <auto P, class N, auto S>
struct spec_of<XRef<P, N>, S> {
  static constexpr auto ptr = P;
  using narrow = N;
};
template <auto S>
using spec_t = spec_of<std::remove_cv_t<decltype(S)>, S>;

template <auto S>
inline constexpr auto spec_ptr = spec_t<S>::ptr;
template <auto S>
using spec_narrow_t = typename spec_t<S>::narrow;
template <auto S>
using spec_class_t =
    typename member_traits<std::remove_cv_t<decltype(spec_ptr<S>)>>::class_type;

// The member receives the match.
template <auto S, class E>
struct Assign : GrammarNode {
  using node_type = spec_class_t<S>;
};

// The container member receives one element per match.
template <auto S, class E>
struct Append : GrammarNode {
  using node_type = spec_class_t<S>;
};

// `?=`: the bool member is set to true when E matched.
template <auto Ptr, class E>
struct Flag : GrammarNode {
  using node_type = typename member_traits<decltype(Ptr)>::class_type;
  using member_type = typename member_traits<decltype(Ptr)>::type;
  static_assert(std::is_same_v<member_type, bool>,
                "flag<&T::m>: the member must be bool");
};

// `{T.f=current}`: creates a T, moves the node built so far into T::f, and T
// is current for the rest of the region. A region over E.
template <class T, auto Ptr, class E>
struct Nest : GrammarNode {
  static_assert(
      std::is_base_of_v<typename member_traits<decltype(Ptr)>::class_type, T>,
      "nest<T, &C::f>: the member must belong to T (or a base of T)");
};

template <class T, auto Ptr, class E>
inline constexpr bool is_nest<Nest<T, Ptr, E>> = true;

template <class... Rs>
struct IgnoreTag {};
template <class... Rs>
struct HideTag {};

// A skip-configuration region: between E's elements, and in the rules E
// calls, the skipper runs these rules until an inner skip() sets its own.
template <class I, class H, class E>
struct Skip : GrammarNode {};

// Levels are listed tightest-binding first in `levels(...)`. `left` and
// `right` say how equal operators group, `nonassoc` that a chain is an error.
// An operator has a length known at compile time: the table is sorted on it.
template <class Op>
struct LeftAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::Left;
};
template <class Op>
struct RightAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::Right;
};
template <class Op>
struct NonAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::Non;
};
// A unary level: its operator stands before the operand (`prefix`) or after
// it (`postfix`) and builds a `Node` of its own. Its operand is read at its
// own level.
template <Association A, class Node, auto OpPtr, auto OperandPtr, class Op>
struct UnaryLevel {
  static_assert(
      std::is_base_of_v<typename member_traits<decltype(OpPtr)>::class_type,
                        Node> &&
          std::is_base_of_v<
              typename member_traits<decltype(OperandPtr)>::class_type, Node>,
      "prefix/postfix: the operator and operand members must belong to the "
      "level's class (or a base of it)");
  using op_expr = Op;
  using node_type = Node;
  static constexpr Association kGroup = A;
  static constexpr auto op_pointer = OpPtr;
  static constexpr auto operand_pointer = OperandPtr;
};
template <class L>
inline constexpr bool is_unary_level =
    L::kGroup == Association::Prefix || L::kGroup == Association::Postfix;
template <class... Ls>
struct Levels {
  static constexpr std::size_t size = sizeof...(Ls);
  template <std::size_t I>
  using at = pack_at_t<I, Ls...>;
};

// The level a table of one holds, as a list; none for anything else.
template <class T>
struct sole_level {
  using type = TypeList<>;
};
template <class L>
struct sole_level<Levels<L>> {
  using type = TypeList<L>;
};
template <class L>
struct levels_of;
template <class... Ls>
struct levels_of<TypeList<Ls...>> {
  using type = Levels<Ls...>;
};

// A function object: `GrammarSpec` holds it as a member.
struct LevelsFn {
  template <class... Ts>
  constexpr auto operator()(Ts...) const {
    using Ls = tl_cat_t<typename sole_level<Ts>::type...>;
    static_assert(sizeof...(Ts) > 0 && tl_size<Ls>::value == sizeof...(Ts),
                  "levels(...) takes only precedence levels, tightest-binding "
                  "first: each one is `left(op)`, `right(op)`, `nonassoc(op)`, "
                  "`prefix<...>(op)` or `postfix<...>(op)`");
    // After the refusal, keep only the real levels, so the error stays one
    // line.
    return typename levels_of<Ls>::type{};
  }
};

// One declaration for the whole file; the specialisations are at the bottom,
// after the algebra they fold through.
template <class... Es>
struct cat_seq;

// A list under construction; not a grammar shape, so Seq and the choices can
// assert their arity at class scope.
template <class... Es>
struct Pack {};

// The one place a sequence is built: `F<a,b> + c` is `F<a,b,c>`, never
// `F<F<a,b>,c>`, so an expression has one shape however it is grouped.
template <template <class...> class F, class A, class B>
struct FlatCat {
  using type = F<A, B>;
};
template <template <class...> class F, class... As, class B>
struct FlatCat<F, F<As...>, B> {
  using type = F<As..., B>;
};
template <template <class...> class F, class A, class... Bs>
struct FlatCat<F, A, F<Bs...>> {
  using type = F<A, Bs...>;
};
template <template <class...> class F, class... As, class... Bs>
struct FlatCat<F, F<As...>, F<Bs...>> {
  using type = F<As..., Bs...>;
};

// A bare creation absorbs what follows it, so `create<N> + e` and
// `create<N>(e)` are one type.
template <class A, class B>
struct SeqCat : FlatCat<Seq, A, B> {};
template <class N, class B>
struct SeqCat<Create<N, Epsilon>, B> {
  using type = Create<N, B>;
};
template <class N, class... Bs>
struct SeqCat<Create<N, Epsilon>, Seq<Bs...>> {
  using type = Create<N, Seq<Bs...>>;
};
template <class A, class B>
using ChoiceCat = FlatCat<Choice, A, B>;

// The creation algebra: a sequence normalizes to
//     [bare...] [Create<T, ...>] Nest<W1, ...> Nest<W2, ...> ...
// A create absorbs both sides up to a nest; a nest absorbs what follows it.
// So `a + create<T>(b) + c + nest<W, &W::f>(d)` is
// `create<T>(a + b + c) + nest<W, &W::f>(d)`, however it is grouped.

// No region open yet -- the run is still collecting its bare prefix.
struct NoRegion {};

// One element joins the region currently open. The region's body is re-folded,
// so a nest inside it keeps absorbing what comes after it.
template <class R, class E>
struct region_add;
template <class T, class X, class E>
struct region_add<Create<T, X>, E> {
  using type = Create<T, typename cat_seq<X, E>::type>;
};
template <class T, auto P, class X, class E>
struct region_add<Nest<T, P, X>, E> {
  using type = Nest<T, P, typename cat_seq<X, E>::type>;
};

// The canonical run back to a shape: one part is the shape (Seq asserts
// arity two at class scope). The parts are regions and bare elements, never
// a Seq -- SeqCat flattened before the fold ran.
template <class... Xs>
struct run_shape;
template <class X>
struct run_shape<X> {
  using type = X;
};
template <class X, class Y, class... Zs>
struct run_shape<X, Y, Zs...> {
  using type = Seq<X, Y, Zs...>;
};

template <class Pre, class Done, class Cur>
struct close_region_run;
template <class... Pre, class... Done>
struct close_region_run<Pack<Pre...>, Pack<Done...>, NoRegion>
    : run_shape<Pre..., Done...> {};
template <class... Pre, class... Done, class Cur>
struct close_region_run<Pack<Pre...>, Pack<Done...>, Cur>
    : run_shape<Pre..., Done..., Cur> {};

// Which transition an element takes. A non-type argument, so the step
// specializations never have to out-specialize each other on the state.
template <class E>
inline constexpr int step_kind = 0;
template <class T, class X>
inline constexpr int step_kind<Create<T, X>> = 1;
template <class T, auto P, class X>
inline constexpr int step_kind<Nest<T, P, X>> = 2;

template <class Pre, class Done, class Cur, class Es>
struct fold_run;

template <class Pre, class Done, class Cur, class E, class Rest>
struct plain_bare;
template <class... Pre, class Done, class Cur, class E, class... Rest>
struct plain_bare<Pack<Pre...>, Done, Cur, E, Pack<Rest...>>
    : fold_run<Pack<Pre..., E>, Done, Cur, Pack<Rest...>> {};

template <class Pre, class Done, class Cur, class E, class Rest>
struct plain_absorb;
template <class Pre, class Done, class Cur, class E, class... Rest>
struct plain_absorb<Pre, Done, Cur, E, Pack<Rest...>>
    : fold_run<Pre, Done, typename region_add<Cur, E>::type, Pack<Rest...>> {};

// The head creation opens: it takes the bare prefix with it.
template <class Pre, class Done, class T, class X, class Rest>
struct create_open;
template <class... Pre, class Done, class T, class X, class... Rest>
struct create_open<Pack<Pre...>, Done, T, X, Pack<Rest...>>
    : fold_run<Pack<>, Done, Create<T, typename cat_seq<Pre..., X>::type>,
               Pack<Rest...>> {};

// A create with a region already open. Two named mistakes, and exactly one of
// the two asserts below fires: the open region is a Create (two heads in one
// sequence) or a Nest (a head declared after the current node moved).
template <class Pre, class Done, class Cur, class T, class X, class Rest>
struct create_second;
template <class Pre, class Done, class Cur, class T, class X, class... Rest>
struct create_second<Pre, Done, Cur, T, X, Pack<Rest...>> {
  static_assert(!is_nest<Cur>,
                "a create<> after a nest<> starts a second node: the nest "
                "already made its wrapper the current node. Put the create "
                "before the nest, or in its own alternative");
  static_assert(is_nest<Cur>,
                "a sequence builds one node, so it cannot hold two create<>: "
                "put each create in its own alternative");
  // The errors above have already been reported; carrying on keeps the
  // diagnostic to the one line that states the mistake.
  using type =
      typename fold_run<Pre, Done, typename region_add<Cur, Create<T, X>>::type,
                        Pack<Rest...>>::type;
};

// The first nest: the bare prefix stays bare (adoption).
template <class Pre, class Done, class T, auto P, class X, class Rest>
struct nest_first;
template <class Pre, class Done, class T, auto P, class X, class... Rest>
struct nest_first<Pre, Done, T, P, X, Pack<Rest...>>
    : fold_run<Pre, Done, Nest<T, P, X>, Pack<Rest...>> {};

// Any later nest closes the region before it: the chain grows outwards.
template <class Pre, class Done, class Cur, class T, auto P, class X,
          class Rest>
struct nest_next;
template <class Pre, class... Done, class Cur, class T, auto P, class X,
          class... Rest>
struct nest_next<Pre, Pack<Done...>, Cur, T, P, X, Pack<Rest...>>
    : fold_run<Pre, Pack<Done..., Cur>, Nest<T, P, X>, Pack<Rest...>> {};

template <int K, class Pre, class Done, class Cur, class E, class Rest>
struct fold_step;
// conditional_t picks the class; only the winner is asked for ::type, so the
// absorbing branch never forms `region_add<NoRegion, E>`.
template <class Pre, class Done, class Cur, class E, class Rest>
struct fold_step<0, Pre, Done, Cur, E, Rest>
    : cond_t<std::is_same_v<Cur, NoRegion>, plain_bare<Pre, Done, Cur, E, Rest>,
             plain_absorb<Pre, Done, Cur, E, Rest>> {};
template <class Pre, class Done, class Cur, class T, class X, class Rest>
struct fold_step<1, Pre, Done, Cur, Create<T, X>, Rest>
    : cond_t<std::is_same_v<Cur, NoRegion>, create_open<Pre, Done, T, X, Rest>,
             create_second<Pre, Done, Cur, T, X, Rest>> {};
template <class Pre, class Done, class Cur, class T, auto P, class X,
          class Rest>
struct fold_step<2, Pre, Done, Cur, Nest<T, P, X>, Rest>
    : cond_t<std::is_same_v<Cur, NoRegion>,
             nest_first<Pre, Done, T, P, X, Rest>,
             nest_next<Pre, Done, Cur, T, P, X, Rest>> {};

template <class Pre, class Done, class Cur>
struct fold_run<Pre, Done, Cur, Pack<>> : close_region_run<Pre, Done, Cur> {};
template <class Pre, class Done, class Cur, class E, class... Rest>
struct fold_run<Pre, Done, Cur, Pack<E, Rest...>>
    : fold_step<step_kind<E>, Pre, Done, Cur, E, Pack<Rest...>> {};

template <class S>
struct normalize_seq {
  using type = S;
};
template <class... Es>
struct normalize_seq<Seq<Es...>>
    : fold_run<Pack<>, Pack<>, NoRegion, Pack<Es...>> {};
// A run with no creation and no nest folds to itself, answered at once: `+`
// asks this at every element it appends, and a fold step per element would
// walk the whole run again each time.
template <class... Es>
  requires((step_kind<Es> == 0) && ...)
struct normalize_seq<Seq<Es...>> {
  using type = Seq<Es...>;
};

// Folds a list of expressions into one shape; a single element is the
// shape. The primary is declared before the creation algebra, which folds
// through it.
template <class A>
struct cat_seq<A> {
  using type = A;
};
// The empty payload is the identity of concatenation, which is what makes
// `a + create<T>` and `create<T>(a)` one type.
template <class A>
struct cat_seq<A, Epsilon> {
  using type = A;
};
template <class A, class B, class... Rest>
struct cat_seq<A, B, Rest...>
    : normalize_seq<
          typename SeqCat<A, typename cat_seq<B, Rest...>::type>::type> {};
template <class... Es>
using cat_seq_t = typename cat_seq<Es...>::type;

template <Expr A, Expr B>
constexpr auto operator+(A, B) {
  return typename normalize_seq<typename SeqCat<
      std::remove_cvref_t<A>, std::remove_cvref_t<B>>::type>::type{};
}

// The ordered choice: the arm written first wins a tie.
template <Expr A, Expr B>
constexpr auto operator|(A, B) {
  return typename ChoiceCat<std::remove_cvref_t<A>,
                            std::remove_cvref_t<B>>::type{};
}

// An expression joined to what is not one, such as a character or a string.
template <class A, class B>
  requires(Expr<A> != Expr<B>)
constexpr auto operator+(A, B) {
  static_assert(dependent_false<A>,
                "a grammar expression is joined to something that is not one: "
                "write a word or a character as `lit<\"x\">`, a rule as "
                "`ref<Rule>`");
}
template <class A, class B>
  requires(Expr<A> != Expr<B>)
constexpr auto operator|(A, B) {
  static_assert(dependent_false<A>,
                "a grammar expression is joined to something that is not one: "
                "write a word or a character as `lit<\"x\">`, a rule as "
                "`ref<Rule>`");
}

template <Expr E>
constexpr auto operator!(E) {
  return Not<std::remove_cvref_t<E>>{};
}
template <Expr E>
constexpr auto operator&(E) {
  return And<std::remove_cvref_t<E>>{};
}

// The end of a line, which consumes its terminator: `\r\n` before `\r`.
using Eol = decltype(Lit<"\n">{} | Lit<"\r\n">{} | Lit<"\r">{});

// `lit<S>` has no guard; `lit<S, G>` carries the one it was given. A terminal
// named there stands for the body the current grammar spells: a class, a word
// or a choice of those.
template <str S, bool CI, class G>
consteval auto make_lit() {
  if constexpr (std::is_same_v<G, NoGuard>)
    return Lit<S, CI>{};
  else
    return Lit<S, CI, G>{};
}

// Whether a spelling holds a letter with another case: folding is the
// identity on every other character.
template <str S>
consteval bool holds_a_letter() {
  return has_cased_letter(S.view());
}

// A case-insensitive keyword. A spelling with no letter that has another
// case, such as `{`, folds to itself: it is the keyword `kw<>` builds, so a
// grammar may write every keyword `ikw<>`.
template <str S>
consteval auto make_ikw() {
  return Lit<S, holds_a_letter<S>(), WordBound>{};
}

template <ClassFault Fault>
consteval void class_refused() {
  static_assert(Fault != ClassFault::lone_backslash,
                "cursiv: this character class ends with a lone backslash. A "
                "backslash starts an escape, as in std::regex: write a "
                R"msg(backslash itself as `\\`, or R"(\\)" in C++)msg");
  static_assert(Fault != ClassFault::unknown_escape,
                "cursiv: this character class holds an escape that "
                "std::regex does not accept in a class. A backslash followed "
                "by a letter or a digit must form one of `xHH`, `uHHHH`, "
                "`u{H...}`, `n`, `r`, `t`, `f`, `v`, `b`, `0` (not followed "
                "by a digit), `cX` with X a letter, or one of the classes "
                "`d` `D` `s` `S` `w` `W`; a backslash before any other "
                R"(character stands for that character: `\-`, `\^`, `\\`)");
  static_assert(Fault != ClassFault::class_endpoint,
                "cursiv: this character class uses a class escape (`\\d`, "
                "`\\s`, `\\w` or their complements) as an end of a range, but "
                "a range goes from one character to another. To match a `-` "
                "next to a class, write `\\-`");
  static_assert(Fault != ClassFault::reversed_range,
                "cursiv: this character class holds a reversed range: its "
                "first character comes after its last");
  static_assert(Fault != ClassFault::not_scalar,
                "cursiv: this character class names a value that is not a "
                "Unicode character: a surrogate (U+D800 to U+DFFF) or a value "
                "above U+10FFFF");
  static_assert(Fault != ClassFault::malformed,
                "cursiv: this character class holds bytes that are not "
                "valid UTF-8");
}
template <str Spec>
consteval ClassFault class_fault() {
  constexpr bool negated = Spec.size() > 1 && Spec[0] == '^';
  constexpr ClassFault fault = for_class_ranges(
      Spec.view().substr(negated ? 1 : 0),
      [](char32_t, char32_t) { /* only the fault is wanted */ });
  class_refused<fault>();
  return fault;
}
template <str Spec>
consteval auto cp_tail() {
  char buf[Spec.size()]{};
  for (std::size_t i = 1; i < Spec.size(); ++i) buf[i - 1] = Spec[i];
  return str<Spec.size()>(buf);
}
enum class ClassOp : unsigned char { unite, intersect, outside };
template <str A, str B, ClassOp Op>
consteval ClassRanges class_ranges() {
  const ClassRanges a = ClassRanges::of(A.view());
  const ClassRanges b = ClassRanges::of(B.view());
  return Op == ClassOp::unite       ? a.united(b)
         : Op == ClassOp::intersect ? a.intersected(b)
                                    : a.complemented();
}
template <str A, str B, ClassOp Op>
consteval auto class_spelled() {
  constexpr std::size_t n = class_ranges<A, B, Op>().spelled_size();
  char buf[n + 1]{};
  std::size_t at = 0;
  class_ranges<A, B, Op>().spell([&buf, &at](char c) { buf[at++] = c; });
  return str<n + 1>(buf);
}
template <str Text>
consteval bool class_holds_invalid() {
  return ClassRanges::of(Text.view()).holdsInvalid;
}
// A spelling's set among all characters: held in its own scalars, or, where it
// holds a complement escape, as the complement of the scalars it leaves out.
template <str Text, bool Complement>
consteval auto class_of() {
  constexpr bool holdsInvalid = class_holds_invalid<Text>();
  return CpSet<
      0, holdsInvalid != Complement,
      class_spelled<Text, "",
                    holdsInvalid ? ClassOp::outside : ClassOp::unite>()>{};
}
template <str Spec>
consteval auto cr_of() {
  constexpr bool negated = Spec.size() > 1 && Spec[0] == '^';
  if constexpr (class_fault<Spec>() != ClassFault::none)
    return CpSet<0, false, "">{};
  else if constexpr (negated)
    return class_of<cp_tail<Spec>(), true>();
  else
    return class_of<Spec, false>();
}
consteval std::uint64_t cp_bit(unicode::Property p) {
  return std::uint64_t{1} << static_cast<unsigned>(p);
}
// The scalars the properties `M` names.
template <std::uint64_t M>
consteval ClassRanges props_ranges() {
  ClassRanges out;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::Count); ++i)
    if ((M >> i) & 1)
      out.r.insert(out.r.end(), unicode::kTables[i].begin(),
                   unicode::kTables[i].end());
  out.merge();
  return out;
}
// What a complement of `Sn` and the properties `Mn` leaves out once the
// positive class `Sp`, `Mp` is joined to it: a property both name drops out
// whole, the others are read as their scalars.
template <str Sn, std::uint64_t Mn, str Sp, std::uint64_t Mp>
consteval ClassRanges join_left_out() {
  return ClassRanges::of(Sn.view())
      .united(props_ranges<Mn & ~Mp>())
      .minus(ClassRanges::of(Sp.view()).united(props_ranges<Mp>()));
}
template <str Sn, std::uint64_t Mn, str Sp, std::uint64_t Mp>
consteval auto join_left_out_spelled() {
  constexpr std::size_t n = join_left_out<Sn, Mn, Sp, Mp>().spelled_size();
  char buf[n + 1]{};
  std::size_t at = 0;
  join_left_out<Sn, Mn, Sp, Mp>().spell([&buf, &at](char c) { buf[at++] = c; });
  return str<n + 1>(buf);
}
// Two classes joined: the scalars together, and the invalid bytes of either. A
// complement joined to anything is the complement of what it leaves out.
template <std::uint64_t MA, bool NA, str SA, std::uint64_t MB, bool NB, str SB>
consteval auto cp_join() {
  if constexpr (!NA && !NB) {
    return CpSet<MA | MB, false, class_spelled<SA, SB, ClassOp::unite>()>{};
  } else if constexpr (NA && NB) {
    static_assert(MA == MB,
                  "cursiv: two complemented classes can be joined with `|` "
                  "only when they use the same Unicode properties: complement "
                  "the whole union instead, ~(a | b)");
    return CpSet<MA, true, class_spelled<SA, SB, ClassOp::intersect>()>{};
  } else if constexpr (NA) {
    return CpSet<0, true, join_left_out_spelled<SA, MA, SB, MB>()>{};
  } else {
    return CpSet<0, true, join_left_out_spelled<SB, MB, SA, MA>()>{};
  }
}
// Classes join into one class, and `~` complements among all characters.
template <std::uint64_t A, bool NA, str SA, std::uint64_t B, bool NB, str SB>
constexpr auto operator|(CpSet<A, NA, SA>, CpSet<B, NB, SB>) {
  return cp_join<A, NA, SA, B, NB, SB>();
}
template <std::uint64_t M, bool N, str S>
constexpr auto operator~(CpSet<M, N, S>) {
  static_assert(!N || M != 0 || S.size() != 0,
                "cursiv: this complement matches no character: it is the "
                "complement of `any`");
  return CpSet<M, !N, S>{};
}
// `byte` is one raw byte, not a class: it has no complement and joins none.
template <class B>
  requires std::same_as<std::remove_cvref_t<B>, Byte>
constexpr auto operator~(B) {
  static_assert(dependent_false<B>,
                "cursiv: `byte` is one raw byte, not a class, so it has no "
                "complement: write the class, `~cr<...>`");
  return Byte{};
}
template <std::uint64_t M, bool N, str S>
constexpr auto operator|(CpSet<M, N, S>, Byte) {
  static_assert(dependent_false<CpSet<M, N, S>>,
                "cursiv: `byte` is one raw byte, not a class, so it cannot be "
                "joined to a class: give each its own alternative");
  return Byte{};
}
template <std::uint64_t M, bool N, str S>
constexpr auto operator|(Byte, CpSet<M, N, S>) {
  static_assert(dependent_false<CpSet<M, N, S>>,
                "cursiv: `byte` is one raw byte, not a class, so it cannot be "
                "joined to a class: give each its own alternative");
  return Byte{};
}

// A separated list's item may not be a create or a nest: the creation algebra
// would hoist a create over the whole list, folding the separator into each
// item's region, and a nest would absorb the repetition after it.
template <class... Es>
inline constexpr bool list_item_stands =
    !(is_create<Es> || ...) && !(is_nest<Es> || ...);

// A committed list: each item after the first opens with `sep + prefix`.
template <class P, class B, class Sep>
using committed_tail =
    Repeat<0, kUnbounded,
           Commit<std::remove_cvref_t<decltype(Sep{} + P{})>, B>>;
// The whole list, when it may be absent, commits on the first prefix.
template <class P, class B, class Sep>
using committed_list = Commit<
    P, std::remove_cvref_t<decltype(B{} + committed_tail<P, B, Sep>{})>>;

// Function objects, which `GrammarSpec` holds as members. The separated forms
// spell `a (sep a)*`; no dedicated node comes of them.

struct SomeFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<1, kUnbounded, std::remove_cvref_t<E>>{};
  }
  template <Expr E, Expr Sep>
  constexpr auto operator()(E e, Sep sep) const {
    static_assert(
        list_item_stands<std::remove_cvref_t<E>, std::remove_cvref_t<Sep>>,
        "a create<> or nest<> cannot be the item of a separated "
        "list, since it would take in the whole list: declare the "
        "item as a rule and use ref<> to it");
    return e + Repeat<0, kUnbounded, std::remove_cvref_t<decltype(sep + e)>>{};
  }
  template <class P, class B, Expr Sep>
  constexpr auto operator()(Commit<P, B>, Sep) const {
    static_assert(list_item_stands<P, B, std::remove_cvref_t<Sep>>,
                  "a create<> or nest<> cannot be the item of a separated "
                  "list, since it would take in the whole list: declare the "
                  "item as a rule and use ref<> to it");
    return P{} + B{} + committed_tail<P, B, std::remove_cvref_t<Sep>>{};
  }
};

struct ManyFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<0, kUnbounded, std::remove_cvref_t<E>>{};
  }
  template <Expr E, Expr Sep>
  constexpr auto operator()(E e, Sep sep) const {
    return Repeat<0, 1, std::remove_cvref_t<decltype(SomeFn{}(e, sep))>>{};
  }
  template <class P, class B, Expr Sep>
  constexpr auto operator()(Commit<P, B>, Sep) const {
    static_assert(list_item_stands<P, B, std::remove_cvref_t<Sep>>,
                  "a create<> or nest<> cannot be the item of a separated "
                  "list, since it would take in the whole list: declare the "
                  "item as a rule and use ref<> to it");
    return Repeat<0, 1, committed_list<P, B, std::remove_cvref_t<Sep>>>{};
  }
};

struct OptionFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<0, 1, std::remove_cvref_t<E>>{};
  }
};

// The unordered group: each item once, in any order. A nested group stays
// one item. Every item must read input.
struct UnorderedFn {
  template <Expr... Es>
  constexpr auto operator()(Es...) const {
    return Unordered<std::remove_cvref_t<Es>...>{};
  }
};

template <std::size_t Min, std::size_t Max = Min>
struct RepeatFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<Min, Max, std::remove_cvref_t<E>>{};
  }
};

template <class Tag>
struct MarkFn {
  template <Expr E>
  constexpr Mark<Tag, std::remove_cvref_t<E>> operator()(E) const {
    return {};
  }
};

template <auto S>
struct AssignFn {
  template <Expr E>
  constexpr typename directive_operand<Assign, S, std::remove_cvref_t<E>>::type
  operator()(E) const {
    return {};
  }
};

template <auto S>
struct AppendFn {
  template <Expr E>
  constexpr typename directive_operand<Append, S, std::remove_cvref_t<E>>::type
  operator()(E) const {
    return {};
  }
};

template <auto Ptr>
struct FlagFn {
  template <Expr E>
  constexpr Flag<Ptr, std::remove_cvref_t<E>> operator()(E) const {
    return {};
  }
};

// The tags exist for the call site only; the node stores plain lists.
struct SkipFn {
  template <class... Is, class... Hs, Expr E0, Expr... Es>
  constexpr auto operator()(IgnoreTag<Is...>, HideTag<Hs...>, E0,
                            Es...) const {
    return Skip<TypeList<Is...>, TypeList<Hs...>, cat_seq_t<E0, Es...>>{};
  }
};

struct TightFn {
  template <Expr E>
  constexpr auto operator()(E e) const {
    return SkipFn{}(IgnoreTag<>{}, HideTag<>{}, e);
  }
};

// Rebuilding helpers shared by the authoring, compiling and optimizing
// layers: a list of parts back to a shape, a choice's parts as a list.

// A set flattens as its operator does: `a | b | z` is one choice of three, so
// a choice spliced into a choice joins it rather than nesting. A create a
// rewrite introduces stays a bounded region.
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

// One part is the shape: a choice of one is no choice (Seq and the choices
// assert arity two at class scope).
template <class L>
struct seq_of;
template <class E>
struct seq_of<TypeList<E>> {
  using type = E;
};
template <class... Es>
struct seq_of<TypeList<Es...>> : flat_of<Seq, Es...> {};
template <class L>
struct or_of;
template <class E>
struct or_of<TypeList<E>> {
  using type = E;
};
template <class... Es>
struct or_of<TypeList<Es...>> {
  using type = Choice<Es...>;
};
// A non-choice is its own one-element run.
template <class E>
struct alts_list_impl {
  using type = TypeList<E>;
};
template <class E>
using alts_list = alts_list_impl<E>;
template <class... Es>
struct alts_list_impl<Choice<Es...>> {
  using type = TypeList<Es...>;
};

}  // namespace cursiv::detail
