#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/RuleTraits.hpp"
#include "cursiv/core/grammar/unicode/UnicodeProperty.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/core/text/ClassItem.hpp"
#include "cursiv/core/text/ClassRanges.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/core/utils/typelist.hpp"

// Grammar expressions. Every type is an empty structural tag so that a rule's
// body can be a template argument (NTTP). `operator+` flattens sequences
// (one type per sequence, whatever the parenthesisation) and normalizes each
// into one head creation followed by a chain of nests -- the creation
// algebra below states the rule precisely.

namespace cursiv {

struct GrammarNode {};

// What the operators and combinators accept. A rule is referred to, not
// combined, so it is not one of these. This is `std::derived_from` spelled
// with the class traits: its `is_convertible_v` is a variable, and g++ gives
// every variable it instantiates a symbol name -- one per operand an operator
// meets, each spelling the whole expression built so far.
template <class T>
concept Expr =
    std::is_base_of<GrammarNode, std::remove_cvref_t<T>>::value &&
    std::is_convertible<const volatile std::remove_cvref_t<T>*,
                        const volatile GrammarNode*>::value;

inline constexpr std::size_t kUnbounded =
    std::numeric_limits<std::size_t>::max();

namespace detail {

// The guard `kw<>` asks for: "whatever this grammar declares". A sentinel
// and not a class, because a keyword is spelled before any grammar exists:
// it is resolved where the declared class is in scope.
struct WordBound {};

// "No guard given" -- `lit<S>`'s second argument when it has none. A tag of
// its own, so no expression has to double as a null.
struct NoGuard {};
inline constexpr NoGuard no_guard{};


}  // namespace detail

// Exact text -- the one terminal behind every literal spelling. `CI` folds
// case; `Guard` is an expression the lexer runs immediately after the match,
// on a copy of the cursor, and the match only stands if it succeeds.
//
// Adjacency is the whole point, and it is why the guard lives in the node
// and not in the rule. `lit<"toto"> + !lit<"titi">` is not this: between two
// atoms of a rule the skipper runs, so that spelling also refuses a `toto`
// that a comment separates from `titi`, where `lit<"toto", !lit<"titi">>`
// looks only at the byte that follows the text. Inside a terminal no
// skipper runs and the two agree -- which is exactly the distinction worth
// a node parameter.
//
// Three states, one slot. `void` is no guard. `detail::WordBound` is the
// boundary discipline the grammar declares (`word_boundary`): a `kw<>` is
// spelled before any grammar exists, so it cannot resolve its own guard --
// it is resolved where the declared class is in scope, keeping the guard
// only where the text can actually run into that class and dropping it
// where it cannot. Anything else is an expression the author spelled
// through `lit<S, G>`, applied as written: no judgement is made about
// whether it can fire, because only its author knows.
template <class E, str T>
struct Doc;

template <str S, bool CI = false, class Guard = void>
struct Lit : GrammarNode {
  static_assert(S.size() > 0, "lit<> needs a non-empty literal");
  static_assert(std::is_void_v<Guard> ||
                    std::is_same_v<Guard, detail::WordBound> ||
                    std::derived_from<Guard, GrammarNode>,
                "lit<S, G>: the guard G must be a grammar expression, "
                "checked right after the text, such as "
                "`lit<\"toto\", !lit<\"titi\">>`. For a keyword that must not "
                "run into the next word, write `kw<S>` instead");

  // The prose a hover shows over this word. Spelled on the element, the only
  // place that knows what the word means.
  template <str D>
  consteval auto doc() const {
    return Doc<Lit<S, CI, Guard>, D>{};
  }
};

// Prose as a node, not as a parameter of what it documents: a literal
// carrying its own doc would make two words that read the same two types,
// where the core holds one token -- and every reader of the tree would have
// to put them back together.
//
// A doc replaces, it does not append: the wrapper rewrites its own prose, so
// `Doc<Doc<E, A>, B>` never forms and `.doc<A>().doc<B>()` is B.
template <class E, str T>
struct Doc : GrammarNode {
  template <str D>
  consteval auto doc() const {
    return Doc<E, D>{};
  }
};

// The doc a grammar element carries, empty for everything that carries none.
// One reader for the whole pipeline: the element table's doc column and the
// dump's `/** ... */` prefix both ask here.
template <class E>
struct doc_of {
  static constexpr std::string_view value{};
};
template <class E, str T>
struct doc_of<Doc<E, T>> {
  static constexpr std::string_view value = T.view();
};

// A class of characters: the Unicode properties it names, one bit each over
// `unicode::Property`, the scalars spelled beside them in canonical form
// (`"\\xc0-\\xff"`, `"$_"`), and whether the whole is complemented. A
// complement holds every scalar outside the rest, and a byte that starts no
// scalar. Two spellings of one set are one type.
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
// against the runs of the lines the parse stands in, never against a width.
struct Indent : GrammarNode {};
struct Dedent : GrammarNode {};
// A condition on the bytes between the token before and the token in hand,
// comments included: `GapHas` holds where some run of them is read by E,
// `GapIs` where all of them are. Zero width; `!` and `|` compose them.
template <class E>
struct GapHas : GrammarNode {};
template <class E>
struct GapIs : GrammarNode {};
// The zero-width token standing for `Tok` where `Cond` holds and the token in
// hand is refused: `implied(tok, cond)` is `tok | Implied<Tok, Cond>`, so a
// walk reaches `tok` as any alternative.
template <class Tok, class Cond>
struct Implied : GrammarNode {};

// `Seq<A>` is A and `Seq<>` is nothing, so both are unspellable; same for the
// choices.
template <class... Es>
struct Seq : GrammarNode {
  static_assert(sizeof...(Es) >= 2,
                "Seq needs at least two elements: build sequences with "
                "operator+ or cat_seq");
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
// `Prefix + Body` is the item. It stands nowhere else.
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
namespace detail {
template <class E>
struct operand_of;
}

// `E` is `Seq<open, Ref<G>, close>`: `G`'s entry between two tokens of the
// caller. A directive over the section is written on the `Ref<G>`.
template <class G, class E>
struct Embed : GrammarNode {};

// The overridden body, inside an override's body. `plug` splices the
// parent's effective body over it, so no walk below ever meets one. Spelled
// as a template so each walk's refusal stays dependent and fires on use,
// not on the specialization's own definition.
template <class = void>
struct SuperT : GrammarNode {};
using Super = SuperT<>;
inline constexpr Super super{};

// The empty payload of a bare `create<T>`: matches nothing, consumes nothing,
// never fails. It is what makes the empty alternative spellable.
struct Epsilon : GrammarNode {};

// What a match builds. How a member receives its value (slice, converted
// value, child pointer, cross-reference) follows from the member's type.

// A region: what E matched is a T.
template <class T, class E>
struct Create : GrammarNode {};

// ...and the empty region is also the factory. `create<T>` is the node with
// the empty payload, and calling it names a payload; there is no functor
// type, so no combinator has to fold one away and none can reach the IR.
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

// Forward: `is_nest` is spelled beside `is_create` because the combinators
// below refuse both for the same reason, while `Nest` itself is declared
// further down.
template <class E>
inline constexpr bool is_nest = false;

// What a directive writes: a bare member pointer or an `xref<P, N>` that
// carries its member and stands in its place.
//
// `N` is what this site accepts. A cross-reference member names the classes
// it may ever bind; one writing site often means less than all of them
// (`assign<xref<&Class::base, Interface>>(ref<QName>)`), and saying so is
// what makes a homonym of the wrong class a failure instead of a wrong
// link. It is a filter, never a cast: the member's type is unchanged and so
// is every reader.
template <auto P, class N>
struct XRef {};
template <auto P, class N>
inline constexpr XRef<P, N> xref{};

namespace detail {
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
}  // namespace detail

template <auto S>
inline constexpr auto spec_ptr = detail::spec_t<S>::ptr;
template <auto S>
using spec_narrow_t = typename detail::spec_t<S>::narrow;
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

// A skip-configuration region: inside E the skipper runs these rules --
// between E's elements only, the entry boundary belongs to the caller. The
// configuration holds through the rules E calls (dynamic scope), until an
// inner skip() declares its own.
template <class... Rs>
struct IgnoreTag {};
template <class... Rs>
struct HideTag {};
template <class I, class H, class E>
struct Skip : GrammarNode {};

// Levels are listed tightest-binding first in `levels(...)`. `left`/
// `right` say which way a chain of equal precedence groups, and `nonassoc`
// that it does not group: the chain is an error; the operator is
// any lexable expression of statically known length, so a level may hold a
// choice of several -- and, when they share one length, that choice is
// fixed-length too. The length is what the whole table is sorted on:
// maximal munch is a compile-time order, not a runtime race between
// alternatives, so an operator whose width only the input knows has no rank
// to take and is refused where it is written.
template <class Op>
struct LeftAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::left;
};
template <class Op>
struct RightAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::right;
};
template <class Op>
struct NonAssoc {
  using op_expr = Op;
  static constexpr Association kGroup = Association::non;
};
// A unary level: its operator stands before the operand (`prefix`) or after
// it (`postfix`) and builds a `Node` of its own, writing the operator and the
// operand into the two members named. Its operand is read at its own level, so
// a prefix applies to everything that binds tighter than it.
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
template <class T>
concept Assoc = requires {
  typename T::op_expr;
  T::kGroup;
};
template <class L>
inline constexpr bool is_unary_level =
    L::kGroup == Association::prefix || L::kGroup == Association::postfix;
template <class... Ls>
struct Levels {
  static constexpr std::size_t size = sizeof...(Ls);
  template <std::size_t I>
  using at = detail::pack_at_t<I, Ls...>;
};

// `left`/`right`/`nonassoc` yield a table of one, not a bare level, and
// `levels(...)` lists several. One level is then a legal table -- which is
// what a cascade spelled one `infix<>` rule per precedence level needs, every
// storey of it carrying exactly one -- and nothing downstream has to
// normalise a level into a table. `levels(left(a), right(b))` is
// `Levels<LeftAssoc<a>, RightAssoc<b>>`. A level's operators are matched by
// maximal munch, longest first.
template <Expr Op>
constexpr auto left(Op) {
  return Levels<LeftAssoc<std::remove_cvref_t<Op>>>{};
}
template <Expr Op>
constexpr auto right(Op) {
  return Levels<RightAssoc<std::remove_cvref_t<Op>>>{};
}
template <Expr Op>
constexpr auto nonassoc(Op) {
  return Levels<NonAssoc<std::remove_cvref_t<Op>>>{};
}
// `prefix<&U::op, &U::operand>(op)` builds the class declaring `op`;
// `prefix<U>(op)` builds `U` through its members `op` and `operand`, even
// where a base of `U` declares them. `postfix` names the operand first, as the
// text reads.
template <auto OpPtr, auto OperandPtr, Expr Op>
constexpr auto prefix(Op) {
  return Levels<UnaryLevel<Association::prefix,
                           typename member_traits<decltype(OpPtr)>::class_type,
                           OpPtr, OperandPtr, std::remove_cvref_t<Op>>>{};
}
template <class T, Expr Op>
constexpr auto prefix(Op) {
  return Levels<UnaryLevel<Association::prefix, T, &T::op, &T::operand,
                           std::remove_cvref_t<Op>>>{};
}
template <auto OperandPtr, auto OpPtr, Expr Op>
constexpr auto postfix(Op) {
  return Levels<UnaryLevel<Association::postfix,
                           typename member_traits<decltype(OpPtr)>::class_type,
                           OpPtr, OperandPtr, std::remove_cvref_t<Op>>>{};
}
template <class T, Expr Op>
constexpr auto postfix(Op) {
  return Levels<UnaryLevel<Association::postfix, T, &T::op, &T::operand,
                           std::remove_cvref_t<Op>>>{};
}

namespace detail {
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
}  // namespace detail

// A function object, so the cursiv::GrammarSpec base can re-expose it.
struct LevelsFn {
  template <class... Ts>
  constexpr auto operator()(Ts...) const {
    using Ls = detail::tl_cat_t<typename detail::sole_level<Ts>::type...>;
    static_assert(
        sizeof...(Ts) > 0 && detail::tl_size<Ls>::value == sizeof...(Ts),
        "levels(...) takes only precedence levels, tightest-binding "
        "first: each one is `left(op)`, `right(op)`, `nonassoc(op)`, "
        "`prefix<...>(op)` or `postfix<...>(op)`");
    // Past the refusal, the table of the levels that are levels: the line
    // that states the mistake stays the only one.
    return typename detail::levels_of<Ls>::type{};
  }
};
inline constexpr LevelsFn levels{};

// One declaration for the whole file; the specialisations are at the bottom,
// after the algebra they fold through.
template <class... Es>
struct cat_seq;

namespace detail {

// A list under construction; not a grammar shape, so Seq and the choices can
// assert their arity at class scope.
template <class... Es>
struct Pack {};

// The one place a sequence is built: flattening happens here.
// Flattening concatenation for a variadic form: `F<a,b> + c` is `F<a,b,c>`
// and never `F<F<a,b>,c>`, so a spelled expression has one shape whatever
// the order its operators associate in. One mechanism, four cases; each
// form is an alias of it, since the rule does not depend on which form it
// is.
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

// A sequence is the one form with an exception: a bare creation absorbs what
// follows it, so `create<N> + e` and `create<N>(e)` are one type. Absorbing
// a sequence is the same fact, spelled out because the flattening case is
// neither more nor less specialised than the one above.
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

// The creation algebra. A sequence normalizes to one head creation followed
// by a chain of nests:
//
//     [bare...] [Create<T, ...>] Nest<W1, ...> Nest<W2, ...> ...
//
// A `Create` absorbs what surrounds it -- both sides -- up to the boundary a
// `Nest` sets; a `Nest` absorbs what follows it, up to the next nest, because
// it made its wrapper the current node and everything after belongs to that
// wrapper. So `a + create<T>(b) + c + nest<W, &W::f>(d) + e` is
// `create<T>(a + b + c) + nest<W, &W::f>(d + e)`, and the current node stays
// W1 until W2 wraps it.
//
// The bare prefix survives only when no create claims it: that is the
// adoption case (`ref<Sub> + nest<W, &W::f>(x)`), where the head node comes
// from the reference and the nest wraps what the reference built.
//
// Confluence is the criterion, exactly as for flattening: the fold reads the
// flat element run, so `(a + create<T>(b)) + c` and `a + (create<T>(b) + c)`
// build the same type. Absorbing into a region re-normalizes that region's
// body (`cat_seq`), which is what makes appending past a nest agree with
// writing the same run inside the create in one go.

// No region open yet -- the run is still collecting its bare prefix.
struct NoRegion {};

// One element joins the region currently open. The region's body is re-folded,
// so a nest inside it keeps absorbing what lands after it.
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

}  // namespace detail

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
    : detail::normalize_seq<typename detail::SeqCat<
          A, typename cat_seq<B, Rest...>::type>::type> {};
template <class... Es>
using cat_seq_t = typename cat_seq<Es...>::type;

template <Expr A, Expr B>
constexpr auto operator+(A, B) {
  return typename detail::normalize_seq<typename detail::SeqCat<
      std::remove_cvref_t<A>, std::remove_cvref_t<B>>::type>::type{};
}

// `|` is the choice, and the only one, ordered: the arm written first wins a
// tie.
template <Expr A, Expr B>
constexpr auto operator|(A, B) {
  return typename detail::ChoiceCat<std::remove_cvref_t<A>,
                                    std::remove_cvref_t<B>>::type{};
}

// The lookaheads, unary `!` and `&`.
template <Expr E>
constexpr auto operator!(E) {
  return Not<std::remove_cvref_t<E>>{};
}
template <Expr E>
constexpr auto operator&(E) {
  return And<std::remove_cvref_t<E>>{};
}

namespace detail {

// `lit<S>` has no guard; `lit<S, G>` carries the one it was given. A terminal
// named there stands for the body the grammar in force spells: a class, a word
// or a choice of those.
template <str S, bool CI, class G>
consteval auto make_lit() {
  if constexpr (std::is_same_v<G, NoGuard>)
    return Lit<S, CI>{};
  else
    return Lit<S, CI, G>{};
}

// A case-insensitive keyword needs a case. `ikw<>` differs from `kw<>` in
// exactly one thing -- the byte compare folds -- and folding is the identity
// on every byte that is not a letter. A spelling with no `a-zA-Z` in it is
// therefore `kw<>` written the long way: same language, one more template
// argument, one more element row.
template <str S>
consteval bool holds_a_letter() {
  for (std::size_t i = 0; i < S.size(); ++i) {
    const char c = S[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
  }
  return false;
}

template <str S>
consteval auto make_ikw() {
  static_assert(holds_a_letter<S>(),
                "`ikw<S>` is a case-insensitive keyword, and this text has no "
                "letter, so case changes nothing: write `kw<S>` instead");
  return Lit<S, true, WordBound>{};
}

}  // namespace detail

// Three spellings of one node, and two ways to guard it.
//
// `kw<S>` asks for the boundary discipline and takes no argument for it: the
// class is the grammar's `word_boundary` (`w` unless it says otherwise), and
// the lowering is conditional -- `kw<S>` is `lit<S, !boundary>` where S's
// last character is in that class, and plain `lit<S>` where it is not. So
// `kw<"[">` carries no guard and accepts `[abc`, while `kw<"if">` refuses
// `iffy`, under one rule and no special case, applied where the grammar
// that decides is in scope -- not here.
//
// `lit<S, G>` is the custom path: any expression the lexer can run, applied
// unconditionally as spelled -- no word-final judgement, because a general
// predicate has no membership question to ask. The author who spells it
// owns that. A keyword-ish token that needs a non-class guard is spelled
// this way; a literal that wants boundary discipline is spelled `kw`.
template <str S, auto G = detail::no_guard>
inline constexpr auto lit =
    detail::make_lit<S, false, std::remove_cvref_t<decltype(G)>>();
template <str S>
inline constexpr Lit<S, false, detail::WordBound> kw{};
template <str S>
inline constexpr auto ikw = detail::make_ikw<S>();
namespace detail {
template <ClassFault Fault>
consteval void class_refused() {
  static_assert(Fault != ClassFault::lone_backslash,
                "cursiv: this character class ends with a lone backslash. A "
                "backslash starts an escape, as in std::regex: write a "
                "backslash itself as `\\\\`, or R\"(\\\\)\" in C++");
  static_assert(Fault != ClassFault::unknown_escape,
                "cursiv: this character class holds an escape that "
                "std::regex does not accept in a class. A backslash followed "
                "by a letter or a digit must form one of `xHH`, `uHHHH`, "
                "`u{H...}`, `n`, `r`, `t`, `f`, `v`, `b`, `0` (not followed "
                "by a digit), `cX` with X a letter, or one of the classes "
                "`d` `D` `s` `S` `w` `W`; a backslash before any other "
                "character stands for that character: `\\-`, `\\^`, `\\\\`");
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
      Spec.view().substr(negated ? 1 : 0), [](char32_t, char32_t) {});
  class_refused<fault>();
  return fault;
}
template <str Spec>
consteval auto cp_tail() {
  char buf[Spec.size()]{};
  for (std::size_t i = 1; i < Spec.size(); ++i) buf[i - 1] = Spec[i];
  return str<Spec.size()>(buf);
}
enum class ClassOp : unsigned char { unite, meet, minus, outside };
template <str A, str B, ClassOp Op>
consteval ClassRanges class_ranges() {
  const ClassRanges a = ClassRanges::of(A.view());
  const ClassRanges b = ClassRanges::of(B.view());
  return Op == ClassOp::unite   ? a.united(b)
         : Op == ClassOp::meet  ? a.intersected(b)
         : Op == ClassOp::minus ? a.minus(b)
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
// Whether a spelling names ASCII scalars only.
constexpr bool cp_ascii_of(std::string_view spec) {
  const ClassRanges r = ClassRanges::of(spec);
  return r.r.empty() || r.r.back().hi < 0x80;
}
template <str Spec>
consteval bool cp_ascii() {
  return cp_ascii_of(Spec.view());
}
template <str Text>
consteval bool class_strays() {
  return ClassRanges::of(Text.view()).strays;
}
// A spelling's set among all characters: held in its own scalars, or, where it
// holds a complement escape, as the complement of the scalars it leaves out.
template <str Text, bool Complement>
consteval auto class_of() {
  constexpr bool strays = class_strays<Text>();
  return CpSet<
      0, strays != Complement,
      class_spelled<Text, "", strays ? ClassOp::outside : ClassOp::unite>()>{};
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
// What a class is called, for a dump and a terminal's name: `\p{L|Nd}`,
// `\p{^L}`, the spelled scalars appended. Measured and written by one body, so
// the reserved length cannot drift from the text.
template <class Sink>
constexpr void cp_name_emit(std::uint64_t props, bool complement,
                            std::string_view scalars, Sink sink) {
  const auto put = [&sink](std::string_view v) {
    for (const char ch : v) sink(ch);
  };
  put("\\p{");
  if (complement) sink('^');
  bool first = true;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::count);
       ++i)
    if ((props >> i) & 1) {
      if (!first) sink('|');
      first = false;
      put(unicode::kNames[i]);
    }
  put(scalars);
  sink('}');
}
template <std::uint64_t M, bool N, str S>
consteval auto cp_name() {
  constexpr std::size_t n = [] {
    std::size_t k = 1;
    cp_name_emit(M, N, S.view(), [&k](char) { ++k; });
    return k;
  }();
  char buf[n]{};
  std::size_t at = 0;
  cp_name_emit(M, N, S.view(), [&buf, &at](char c) { buf[at++] = c; });
  return str<n>(buf);
}
// The scalars the properties `M` names.
template <std::uint64_t M>
consteval ClassRanges props_ranges() {
  ClassRanges out;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::count); ++i)
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
// Two classes joined: the scalars together, and the strays of either. A
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
    return CpSet<MA, true, class_spelled<SA, SB, ClassOp::meet>()>{};
  } else if constexpr (NA) {
    return CpSet<0, true, join_left_out_spelled<SA, MA, SB, MB>()>{};
  } else {
    return CpSet<0, true, join_left_out_spelled<SB, MB, SA, MA>()>{};
  }
}
}  // namespace detail
// A class of characters: a bracket expression of std::regex (ECMAScript), its
// brackets left out, spelling scalars -- `\xHH`, `\uHHHH` and `\u{H...}` are
// scalars too. A leading `^` complements among all characters.
template <str Spec>
inline constexpr auto cr = detail::cr_of<Spec>();
// Classes join into one class, and `~` complements among all characters.
template <std::uint64_t A, bool NA, str SA, std::uint64_t B, bool NB, str SB>
constexpr auto operator|(CpSet<A, NA, SA>, CpSet<B, NB, SB>) {
  return detail::cp_join<A, NA, SA, B, NB, SB>();
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
  static_assert(detail::dependent_false<B>,
                "cursiv: `byte` is one raw byte, not a class, so it has no "
                "complement: write the class, `~cr<...>`");
  return Byte{};
}
template <std::uint64_t M, bool N, str S>
constexpr auto operator|(CpSet<M, N, S>, Byte) {
  static_assert(detail::dependent_false<CpSet<M, N, S>>,
                "cursiv: `byte` is one raw byte, not a class, so it cannot be "
                "joined to a class: give each its own alternative");
  return Byte{};
}
template <std::uint64_t M, bool N, str S>
constexpr auto operator|(Byte, CpSet<M, N, S>) {
  static_assert(detail::dependent_false<CpSet<M, N, S>>,
                "cursiv: `byte` is one raw byte, not a class, so it cannot be "
                "joined to a class: give each its own alternative");
  return Byte{};
}
// The Unicode properties, which hold scalars only: a general category and its
// majors, and the identifier and space properties.
struct uc {
  template <unicode::Property P>
  using of = CpSet<detail::cp_bit(P), false, "">;
  static constexpr of<unicode::Property::L> L{};
  static constexpr of<unicode::Property::M> M{};
  static constexpr of<unicode::Property::N> N{};
  static constexpr of<unicode::Property::P> P{};
  static constexpr of<unicode::Property::S> S{};
  static constexpr of<unicode::Property::Z> Z{};
  static constexpr of<unicode::Property::C> C{};
  static constexpr of<unicode::Property::Lu> Lu{};
  static constexpr of<unicode::Property::Ll> Ll{};
  static constexpr of<unicode::Property::Lt> Lt{};
  static constexpr of<unicode::Property::Lm> Lm{};
  static constexpr of<unicode::Property::Lo> Lo{};
  static constexpr of<unicode::Property::Mn> Mn{};
  static constexpr of<unicode::Property::Mc> Mc{};
  static constexpr of<unicode::Property::Me> Me{};
  static constexpr of<unicode::Property::Nd> Nd{};
  static constexpr of<unicode::Property::Nl> Nl{};
  static constexpr of<unicode::Property::No> No{};
  static constexpr of<unicode::Property::Pc> Pc{};
  static constexpr of<unicode::Property::Pd> Pd{};
  static constexpr of<unicode::Property::Ps> Ps{};
  static constexpr of<unicode::Property::Pe> Pe{};
  static constexpr of<unicode::Property::Pi> Pi{};
  static constexpr of<unicode::Property::Pf> Pf{};
  static constexpr of<unicode::Property::Po> Po{};
  static constexpr of<unicode::Property::Sm> Sm{};
  static constexpr of<unicode::Property::Sc> Sc{};
  static constexpr of<unicode::Property::Sk> Sk{};
  static constexpr of<unicode::Property::So> So{};
  static constexpr of<unicode::Property::Zs> Zs{};
  static constexpr of<unicode::Property::Zl> Zl{};
  static constexpr of<unicode::Property::Zp> Zp{};
  static constexpr of<unicode::Property::Cc> Cc{};
  static constexpr of<unicode::Property::Cf> Cf{};
  static constexpr of<unicode::Property::Co> Co{};
  static constexpr of<unicode::Property::XID_Start> XID_Start{};
  static constexpr of<unicode::Property::XID_Continue> XID_Continue{};
  static constexpr of<unicode::Property::White_Space> White_Space{};
};
inline constexpr AnyChar any{};
inline constexpr Byte byte{};
template <class R>
inline constexpr Ref<R> ref{};

// The classes every grammar ends up spelling, and their complements -- the six
// of regex.
inline constexpr auto s = cr<R"( \t\r\n\f\v)">;
inline constexpr auto S = ~s;
inline constexpr auto w = cr<"a-zA-Z0-9_">;
inline constexpr auto W = ~w;
inline constexpr auto d = cr<"0-9">;
inline constexpr auto D = ~d;

// End of line, which consumes its terminator (`\r\n` before `\r`, or a CRLF
// file ends its lines twice), and end of input, which is zero-width -- `eol`
// is not: matching it twice in a row never re-matches the same terminator.
// `\r` is a prefix of `\r\n`: the sort puts the pair longest-first, and the
// spelled order already satisfies it -- the resolved choice is unchanged.
inline constexpr auto eol = lit<"\n"> | lit<"\r\n"> | lit<"\r">;
// Inside a terminal, `eof` tests the raw buffer end; in a rule it tests
// the end after the active skipper has consumed trivia.
inline constexpr Eof eof{};

// The conditions on the gap before the token in hand, by name.
struct gap {
  template <Expr E>
  static constexpr GapHas<std::remove_cvref_t<E>> has(E) { return {}; }
  template <Expr E>
  static constexpr GapIs<std::remove_cvref_t<E>> is(E) { return {}; }
  static constexpr GapHas<std::remove_cvref_t<decltype(eol)>> line{};
  static constexpr GapIs<Epsilon> none{};
};
template <Expr T, Expr C>
constexpr auto implied(T, C) {
  using Tok = std::remove_cvref_t<T>;
  return Choice<Tok, Implied<Tok, std::remove_cvref_t<C>>>{};
}

namespace detail {
// The region law would hoist a create over the whole list and fold the
// separator into every tail item's region; a nest as the item would absorb
// the repetition that follows it, for the same reason.
template <class... Es>
inline constexpr bool list_item_stands =
    !(is_create<Es> || ...) && !(is_nest<Es> || ...);

// A committed list: each item after the first opens with `sep + prefix`.
template <class P, class B, class Sep>
using committed_tail =
    Repeat<0, kUnbounded,
           Commit<std::remove_cvref_t<decltype(Sep{} + P{})>, B>>;
// ...and the whole list, when it may be absent, commits on the first prefix.
template <class P, class B, class Sep>
using committed_list = Commit<
    P, std::remove_cvref_t<decltype(B{} + committed_tail<P, B, Sep>{})>>;
}  // namespace detail

// Function objects, so the cursiv::GrammarSpec base can re-expose them as
// members. The separated forms spell `a (sep a)*`; no dedicated node comes of
// them.

struct SomeFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<1, kUnbounded, std::remove_cvref_t<E>>{};
  }
  template <Expr E, Expr Sep>
  constexpr auto operator()(E e, Sep sep) const {
    static_assert(detail::list_item_stands<std::remove_cvref_t<E>,
                                           std::remove_cvref_t<Sep>>,
                  "a create<> or nest<> cannot be the item of a separated "
                  "list, since it would take in the whole list: declare the "
                  "item as a rule and use ref<> to it");
    return e + Repeat<0, kUnbounded, std::remove_cvref_t<decltype(sep + e)>>{};
  }
  template <class P, class B, Expr Sep>
  constexpr auto operator()(Commit<P, B>, Sep) const {
    static_assert(detail::list_item_stands<P, B, std::remove_cvref_t<Sep>>,
                  "a create<> or nest<> cannot be the item of a separated "
                  "list, since it would take in the whole list: declare the "
                  "item as a rule and use ref<> to it");
    return P{} + B{} + detail::committed_tail<P, B, std::remove_cvref_t<Sep>>{};
  }
};
inline constexpr SomeFn some{};

struct ManyFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<0, kUnbounded, std::remove_cvref_t<E>>{};
  }
  template <Expr E, Expr Sep>
  constexpr auto operator()(E e, Sep sep) const {
    return Repeat<0, 1, std::remove_cvref_t<decltype(some(e, sep))>>{};
  }
  template <class P, class B, Expr Sep>
  constexpr auto operator()(Commit<P, B>, Sep) const {
    static_assert(detail::list_item_stands<P, B, std::remove_cvref_t<Sep>>,
                  "a create<> or nest<> cannot be the item of a separated "
                  "list, since it would take in the whole list: declare the "
                  "item as a rule and use ref<> to it");
    return Repeat<0, 1,
                  detail::committed_list<P, B, std::remove_cvref_t<Sep>>>{};
  }
};
inline constexpr ManyFn many{};

template <Expr P, Expr B>
constexpr auto commit(P, B) {
  return Commit<std::remove_cvref_t<P>, std::remove_cvref_t<B>>{};
}

struct OptionFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<0, 1, std::remove_cvref_t<E>>{};
  }
};
inline constexpr OptionFn option{};

// The unordered group: each item once, in any order. An item is whole, so a
// group given as an item stays one: `unordered(unordered(a, b), c)` reads `c`
// before or after both of `a` and `b`, never between them. Every item reads
// input; the generators refuse one that may match nothing.
struct UnorderedFn {
  template <Expr... Es>
  constexpr auto operator()(Es...) const {
    return Unordered<std::remove_cvref_t<Es>...>{};
  }
};
inline constexpr UnorderedFn unordered{};

template <std::size_t Min, std::size_t Max = Min>
struct RepeatFn {
  template <Expr E>
  constexpr auto operator()(E) const {
    return Repeat<Min, Max, std::remove_cvref_t<E>>{};
  }
};
template <std::size_t Min, std::size_t Max = Min>
inline constexpr RepeatFn<Min, Max> repeat{};

template <class Tag>
struct MarkFn {
  template <Expr E>
  constexpr Mark<Tag, std::remove_cvref_t<E>> operator()(E) const {
    return {};
  }
};
template <class Tag>
inline constexpr MarkFn<Tag> mark{};
template <class Tag>
inline constexpr Same<Tag> same{};
inline constexpr Indent indent{};
inline constexpr Dedent dedent{};

template <Expr A, Expr B>
constexpr auto through(A, B) {
  return Through<std::remove_cvref_t<A>, std::remove_cvref_t<B>>{};
}

template <class G, Expr O, Expr C>
constexpr auto section(O, C) {
  return Embed<G,
               Seq<std::remove_cvref_t<O>, Ref<G>, std::remove_cvref_t<C>>>{};
}

template <class T>
inline constexpr Create<T, Epsilon> create{};

template <auto S>
struct AssignFn {
  template <Expr E>
  constexpr Assign<S, typename detail::operand_of<std::remove_cvref_t<E>>::type>
  operator()(E) const {
    return {};
  }
  template <class G, class O, class M, class C>
  constexpr Embed<G, Seq<O, Assign<S, M>, C>> operator()(
      Embed<G, Seq<O, M, C>>) const {
    return {};
  }
};
template <auto S>
inline constexpr AssignFn<S> assign{};

template <auto S>
struct AppendFn {
  template <Expr E>
  constexpr Append<S, typename detail::operand_of<std::remove_cvref_t<E>>::type>
  operator()(E) const {
    return {};
  }
  template <class G, class O, class M, class C>
  constexpr Embed<G, Seq<O, Append<S, M>, C>> operator()(
      Embed<G, Seq<O, M, C>>) const {
    return {};
  }
};
template <auto S>
inline constexpr AppendFn<S> append{};

template <auto Ptr>
struct FlagFn {
  template <Expr E>
  constexpr Flag<Ptr, std::remove_cvref_t<E>> operator()(E) const {
    return {};
  }
};
template <auto Ptr>
inline constexpr FlagFn<Ptr> flag{};

// The tags of `skip()` are verbs, what the region does to each terminal; the
// grammar's own settings, `ignored` and `hidden`, are participles.
template <class... Rs>
inline constexpr IgnoreTag<Rs...> ignore{};
template <class... Rs>
inline constexpr HideTag<Rs...> hide{};
// The tags exist for the call site only; the node stores plain lists.
struct SkipFn {
  template <class... Is, class... Hs, Expr E0, Expr... Es>
  constexpr auto operator()(IgnoreTag<Is...>, HideTag<Hs...>, E0,
                            Es...) const {
    return Skip<TypeList<Is...>, TypeList<Hs...>, cat_seq_t<E0, Es...>>{};
  }
};
inline constexpr SkipFn skip{};
struct TightFn {
  template <Expr E>
  constexpr auto operator()(E e) const {
    return skip(ignore<>, hide<>, e);
  }
};
inline constexpr TightFn tight{};

// Two spellings, one node. `nest<T, &T::f>` names the wrapper, `nest<&T::f>`
// lets the member name it -- a pointer to member already says which class it
// belongs to, and stating it twice is a place for the two to disagree. Two
// function templates and not one variable template: a type argument cannot
// bind to `auto` nor a value to `class`, so the two spellings sort
// themselves.
template <class T, auto Ptr, Expr E>
constexpr Nest<T, Ptr, std::remove_cvref_t<E>> nest(E) {
  return {};
}
template <auto Ptr, Expr E>
constexpr Nest<typename member_traits<decltype(Ptr)>::class_type, Ptr,
               std::remove_cvref_t<E>>
nest(E) {
  return {};
}

}  // namespace cursiv

// Rebuilding helpers shared by the authoring, compiling and optimizing
// layers: a list of parts back to a shape, a choice's parts as a list.
namespace cursiv::detail {

// Flattening without create absorption: absorption is an authoring
// convention (a create written in a sequence owns the rest of the rule); a
// create a rewrite introduces is a bounded region, and swallowing its
// neighbours would widen it.
template <class... Es>
struct flat_seq;
template <class E>
struct flat_seq<E> {
  using type = E;
};
template <class A, class B, class... Rest>
struct flat_seq<A, B, Rest...>
    : flat_seq<typename FlatCat<Seq, A, B>::type, Rest...> {};
template <class... Es>
using flat_seq_t = typename flat_seq<Es...>::type;

// One part is the shape: a choice of one is no choice (Seq and the choices
// assert arity two at class scope).
template <class L>
struct seq_of;
template <class E>
struct seq_of<TypeList<E>> {
  using type = E;
};
template <class... Es>
struct seq_of<TypeList<Es...>> : flat_seq<Es...> {};
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
