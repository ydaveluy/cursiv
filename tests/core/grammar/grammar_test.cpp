// What a grammar declares and what it compiles to: the expression nodes, their
// source, rendering and utilities, word boundaries, documented words, the
// embedded NULs, and a class spelled among bytes and among code points.

#include "cursiv/core/grammar/Grammar.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/grammar/DerivedGrammar.hpp"
#include "core/grammar/SpellingGrammars.hpp"
#include "cursiv/core/grammar/DefaultGrammarSource.hpp"
#include "cursiv/core/grammar/GrammarDump.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/GrammarUtils.hpp"
#include "cursiv/core/grammar/dump.hpp"
#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/test/Check.hpp"
#include "derivebase/generated/Base/Module.hpp"
#include "derivedialect/generated/Dialect/Module.hpp"
#include "deriveguardbase/generated/Base/Module.hpp"
#include "deriveguarddialect/generated/Dialect/Module.hpp"
#include "deriverule/generated/RuleDialect/Module.hpp"
#include "derivetextdialect/generated/Dialect/Module.hpp"
#include "derivetextdialectll/generated/DialectLl/Module.hpp"
#include "docswide/generated/G/Module.hpp"
#include "docswidell/generated/Ll/Module.hpp"
#include "dualll/generated/Ll/Module.hpp"
#include "duallr/generated/Lr/Module.hpp"
#include "sentblob/generated/Blob/Module.hpp"
#include "sentfree/generated/Free/Module.hpp"
#include "sentword/generated/Word/Module.hpp"
#include "spelldashed/generated/Dashed/Module.hpp"
#include "spellplain/generated/Plain/Module.hpp"

// Pins the expression layer: NTTP usability, operator+ flattening, create
// absorption, combinator shapes, directive shapes.
namespace expr {

using namespace cursiv;

namespace ast {
struct Node {
  bool many = false;
  Node* child = nullptr;
};
struct Mid : Node {};
}  // namespace ast

struct R1;

// A complement is the class of the other characters, and a class like any
// other.
static_assert(std::is_same_v<std::remove_const_t<decltype(S)>, decltype(~s)>);
static_assert(std::is_same_v<std::remove_const_t<decltype(W)>, decltype(~w)>);
static_assert(std::is_same_v<std::remove_const_t<decltype(D)>, decltype(~d)>);
static_assert(std::is_same_v<std::remove_const_t<decltype(S)>,
                             std::remove_const_t<decltype(cr<R"(\S)">)>>);
static_assert(std::is_same_v<std::remove_const_t<decltype(W)>,
                             std::remove_const_t<decltype(cr<R"(\W)">)>>);
static_assert(std::is_same_v<std::remove_const_t<decltype(D)>,
                             std::remove_const_t<decltype(cr<R"(\D)">)>>);
static_assert(std::is_same_v<decltype(s | S), AnyChar>);
static_assert(std::is_same_v<decltype(w | W), AnyChar>);
static_assert(std::is_same_v<decltype(W | w), AnyChar>);
static_assert(std::is_same_v<decltype(d | D), AnyChar>);
static_assert(std::is_same_v<decltype(~S), std::remove_const_t<decltype(s)>>);
static_assert(std::is_same_v<decltype(GrammarSpec::S), decltype(S)>);
static_assert(std::is_same_v<decltype(GrammarSpec::W), decltype(W)>);
static_assert(std::is_same_v<decltype(GrammarSpec::D), decltype(D)>);

// An expression as a template argument, round-tripped intact.
template <auto Body>
struct holder {
  static constexpr auto body = Body;
};
using H = holder<lit<"a"> + many(cr<"0-9">)>;
static_assert(std::is_same_v<decltype(H::body),
                             const decltype(lit<"a"> + many(cr<"0-9">))>);

// Flattening: one type whatever the parenthesisation.
static_assert(std::is_same_v<decltype((lit<"a"> + lit<"b">)+lit<"c">),
                             Seq<Lit<"a">, Lit<"b">, Lit<"c">>>);
static_assert(std::is_same_v<decltype(lit<"a"> + (lit<"b"> + lit<"c">)),
                             Seq<Lit<"a">, Lit<"b">, Lit<"c">>>);
static_assert(std::is_same_v<decltype((lit<"a"> | lit<"b">) | lit<"c">),
                             Choice<Lit<"a">, Lit<"b">, Lit<"c">>>);

// Absorption: a create in a sequence is the region over the sequence.
static_assert(
    std::is_same_v<decltype(lit<"a"> + create<ast::Node>(lit<"b">) + lit<"c">),
                   Create<ast::Node, Seq<Lit<"a">, Lit<"b">, Lit<"c">>>>);

// A bare `create<T>` is an expression -- the node with the empty payload --
// and the call form just names a payload; there is no separate factory type
// for the IR to see. The empty payload is the identity of concatenation.
static_assert(std::is_same_v<decltype(create<ast::Node>),
                             const Create<ast::Node, Epsilon>>);
static_assert(std::is_same_v<decltype(create<ast::Node> + lit<"b">),
                             decltype(create<ast::Node>(lit<"b">))>);
static_assert(std::is_same_v<decltype(lit<"a"> + create<ast::Node>),
                             decltype(create<ast::Node>(lit<"a">))>);
static_assert(std::is_same_v<decltype(lit<"a"> + create<ast::Node> + lit<"b">),
                             decltype(create<ast::Node>(lit<"a"> + lit<"b">))>);
// ...and alone it is the empty alternative, the one thing the call form cannot
// spell: `('a' {X} | {Y})`.
static_assert(
    std::is_same_v<decltype((lit<"a"> + create<ast::Node>) | create<ast::Mid>),
                   cursiv::Choice<cursiv::Create<ast::Node, Lit<"a">>,
                                  cursiv::Create<ast::Mid, cursiv::Epsilon>>>);

// A create absorbs elements up to the nest that bounds it, and each nest
// absorbs what follows up to the next one. Confluence holds: every
// parenthesisation of the same run yields the same canonical type.
namespace alg {
struct W1 : ast::Node {
  ast::Node* held = nullptr;
};
struct W2 : ast::Node {
  ast::Node* held = nullptr;
};
inline constexpr auto n1 = nest<W1, &W1::held>(lit<"d">);
inline constexpr auto n2 = nest<W2, &W2::held>(lit<"f">);

using One = Seq<Create<ast::Node, Seq<Lit<"a">, Lit<"b">, Lit<"c">>>,
                Nest<W1, &W1::held, Seq<Lit<"d">, Lit<"e">>>>;
static_assert(std::is_same_v<decltype(lit<"a"> + create<ast::Node>(lit<"b">) +
                                      lit<"c"> + n1 + lit<"e">),
                             One>);
static_assert(std::is_same_v<decltype((lit<"a"> + create<ast::Node>(lit<"b">)) +
                                      (lit<"c"> + n1 + lit<"e">)),
                             One>);
static_assert(std::is_same_v<decltype(lit<"a"> + (create<ast::Node>(lit<"b">) +
                                                  lit<"c">)+(n1 + lit<"e">)),
                             One>);

using Two = Seq<Create<ast::Node, Seq<Lit<"a">, Lit<"b">, Lit<"c">>>,
                Nest<W1, &W1::held, Seq<Lit<"d">, Lit<"e">>>,
                Nest<W2, &W2::held, Seq<Lit<"f">, Lit<"g">>>>;
static_assert(std::is_same_v<decltype(lit<"a"> + create<ast::Node>(lit<"b">) +
                                      lit<"c"> + n1 + lit<"e"> + n2 + lit<"g">),
                             Two>);
static_assert(
    std::is_same_v<decltype((lit<"a"> + create<ast::Node>(lit<"b">) +
                             lit<"c">)+(n1 + lit<"e">)+(n2 + lit<"g">)),
                   Two>);

// Adoption: nothing creates the head, so the leading element stays bare --
// the node comes from the reference and the nest wraps what it built.
static_assert(
    std::is_same_v<decltype(ref<R1> + n1 + lit<"e">),
                   Seq<Ref<R1>, Nest<W1, &W1::held, Seq<Lit<"d">, Lit<"e">>>>>);

// Appending past a nest agrees with writing the whole run inside the create
// in one go: absorbing into a region re-folds that region's body.
static_assert(
    std::is_same_v<decltype(create<ast::Node>(lit<"a"> + n1) + lit<"e">),
                   decltype(create<ast::Node>(lit<"a"> + n1 + lit<"e">))>);

// Parenthesization invariance: any bracketing of one element run yields the
// same canonical type. The trap is a group whose own elements look bare in
// isolation -- folding per group first would strand `e` and `g` bare; reading
// the flat run every time lets the splice re-absorb them.
static_assert(
    std::is_same_v<decltype(lit<"a"> +
                            (create<ast::Node>(lit<"b">) + lit<"c"> + n1) +
                            (lit<"e"> + n2) + lit<"g">),
                   Two>);
static_assert(std::is_same_v<
              decltype((lit<"a"> + create<ast::Node>(lit<"b">)) +
                       (lit<"c"> + nest<W1, &W1::held>(lit<"d"> + lit<"e">))),
              One>);
// A group of bare elements spliced between two nests joins the nest before
// it, one element at a time.
static_assert(
    std::is_same_v<decltype(n1 + (lit<"e"> + lit<"f">)+n2),
                   Seq<Nest<W1, &W1::held, Seq<Lit<"d">, Lit<"e">, Lit<"f">>>,
                       Nest<W2, &W2::held, Lit<"f">>>>);
// ...and a group ending in a create meeting a group starting bare: the
// create absorbs across the splice, rightwards, with no nest to bound it.
static_assert(std::is_same_v<
              decltype((lit<"a"> + create<ast::Node>(lit<"b">)) +
                       (lit<"c"> + lit<"d">)),
              Create<ast::Node, Seq<Lit<"a">, Lit<"b">, Lit<"c">, Lit<"d">>>>);
}  // namespace alg

// Combinators, and the separated forms that spell `a (sep a)*`.
static_assert(
    std::is_same_v<decltype(option(lit<"a">)), Repeat<0, 1, Lit<"a">>>);
static_assert(
    std::is_same_v<decltype(some(lit<"a">)), Repeat<1, kUnbounded, Lit<"a">>>);
static_assert(
    std::is_same_v<decltype(many(lit<"a">)), Repeat<0, kUnbounded, Lit<"a">>>);
static_assert(
    std::is_same_v<decltype(repeat<2, 5>(lit<"a">)), Repeat<2, 5, Lit<"a">>>);
// A run inside a run, an option, an item that may be empty and an exact count
// stay as written.
static_assert(
    std::is_same_v<decltype(many(some(lit<"a">))),
                   Repeat<0, kUnbounded, Repeat<1, kUnbounded, Lit<"a">>>>);
static_assert(
    std::is_same_v<decltype(some(some(lit<"a">))),
                   Repeat<1, kUnbounded, Repeat<1, kUnbounded, Lit<"a">>>>);
static_assert(std::is_same_v<decltype(option(some(lit<"a">))),
                             Repeat<0, 1, Repeat<1, kUnbounded, Lit<"a">>>>);
static_assert(
    std::is_same_v<decltype(many(many(lit<"a">))),
                   Repeat<0, kUnbounded, Repeat<0, kUnbounded, Lit<"a">>>>);
static_assert(std::is_same_v<decltype(some(option(lit<"a">))),
                             Repeat<1, kUnbounded, Repeat<0, 1, Lit<"a">>>>);
static_assert(std::is_same_v<decltype(repeat<2>(some(lit<"a">))),
                             Repeat<2, 2, Repeat<1, kUnbounded, Lit<"a">>>>);

// A commit is the item of its quantifier, and in a separated list a later
// item's prefix opens with the separator.
static_assert(std::is_same_v<
              decltype(many(commit(lit<"a">, lit<"b">))),
              Repeat<0, kUnbounded, Commit<Lit<"a">, Lit<"b">>>>);
static_assert(std::is_same_v<
              decltype(repeat<1, 2>(commit(lit<"a">, lit<"b">))),
              Repeat<1, 2, Commit<Lit<"a">, Lit<"b">>>>);
static_assert(std::is_same_v<
              decltype(some(commit(lit<"a">, lit<"b">), lit<",">)),
              Seq<Lit<"a">, Lit<"b">,
                  Repeat<0, kUnbounded,
                         Commit<Seq<Lit<",">, Lit<"a">>, Lit<"b">>>>>);
static_assert(std::is_same_v<
              decltype(many(commit(lit<"a">, lit<"b">), lit<",">)),
              Repeat<0, 1,
                     Commit<Lit<"a">,
                            Seq<Lit<"b">,
                                Repeat<0, kUnbounded,
                                       Commit<Seq<Lit<",">, Lit<"a">>,
                                              Lit<"b">>>>>>>);

// A rule reads a commit as its item and a condition on the exits: every exit
// is guarded by `!prefix` but the one at Max.
namespace commits {
using A = Lit<"a">;
using B = Lit<"b">;
using Stop = Not<Trigger<A>>;
static_assert(
    std::is_same_v<detail::commit_free_t<decltype(many(commit(A{}, B{})))>,
                   Seq<Repeat<0, kUnbounded, Seq<A, B>>, Stop>>);
static_assert(
    std::is_same_v<detail::commit_free_t<decltype(some(commit(A{}, B{})))>,
                   Seq<Repeat<1, kUnbounded, Seq<A, B>>, Stop>>);
static_assert(
    std::is_same_v<detail::commit_free_t<decltype(option(commit(A{}, B{})))>,
                   Choice<Seq<A, B>, Stop>>);
static_assert(
    std::is_same_v<
        detail::commit_free_t<decltype(flag<&ast::Node::many>(
            option(commit(A{}, B{}))))>,
        Flag<&ast::Node::many, Choice<Seq<A, B>, Stop>>>);
static_assert(std::is_same_v<
              detail::commit_free_t<decltype(repeat<2>(commit(A{}, B{})))>,
              Repeat<2, 2, Seq<A, B>>>);
static_assert(std::is_same_v<
              detail::commit_free_t<decltype(repeat<1, 3>(commit(A{}, B{})))>,
              Seq<A, B, Choice<Seq<A, B, Choice<Seq<A, B>, Stop>>, Stop>>>);
static_assert(
    std::is_same_v<
        detail::commit_free_t<decltype(lit<"x"> + many(commit(A{}, B{})) +
                                       lit<"y">)>,
        Seq<Lit<"x">, Repeat<0, kUnbounded, Seq<A, B>>, Stop, Lit<"y">>>);
static_assert(
    std::is_same_v<detail::commit_free_t<decltype(lit<"x"> + lit<"y">)>,
                   Seq<Lit<"x">, Lit<"y">>>);

// ...and stands nowhere else.
static_assert(detail::holds_commit<decltype(many(commit(A{}, B{})))>);
static_assert(!detail::stray_commit<decltype(many(commit(A{}, B{})))>);
static_assert(!detail::stray_commit<
              decltype(lit<"x"> + option(some(commit(A{}, B{}), lit<",">)))>);
static_assert(detail::stray_commit<decltype(commit(A{}, B{}))>);
static_assert(detail::stray_commit<decltype(commit(A{}, B{}) + lit<"x">)>);
static_assert(detail::stray_commit<decltype(commit(A{}, B{}) | lit<"x">)>);
static_assert(detail::stray_commit<decltype(many(!commit(A{}, B{})))>);
static_assert(!detail::holds_commit<decltype(lit<"x"> + many(lit<"y">))>);
}  // namespace commits
// `kw<".">` carries a sentinel: a factory is spelled before the grammar's
// `word_boundary` declaration exists to resolve it. Under the default the
// guard cannot fire, so the compiled form is `lit<".">`.
static_assert(std::is_same_v<
              decltype(some(ref<R1>, kw<".">)),
              Seq<Ref<R1>,
                  Repeat<0, kUnbounded,
                         Seq<Lit<".", false, detail::WordBound>, Ref<R1>>>>>);
static_assert(
    std::is_same_v<
        decltype(many(ref<R1>, kw<".">)),
        Repeat<0, 1,
               Seq<Ref<R1>,
                   Repeat<0, kUnbounded,
                          Seq<Lit<".", false, detail::WordBound>, Ref<R1>>>>>>);
static_assert(std::is_same_v<std::remove_cv_t<decltype(kw<".">)>,
                             Lit<".", false, detail::WordBound>>);

// `lit<S, G>` is the custom path: the guard is an expression the lexer runs
// right after the text, held in the type exactly as spelled. Nothing judges
// whether it can fire -- that is `kw<>`'s discipline, not this one.
static_assert(
    std::is_same_v<std::remove_cv_t<decltype(lit<"toto", !lit<"titi">>)>,
                   Lit<"toto", false, Not<Lit<"titi">>>>);
static_assert(std::is_same_v<std::remove_cv_t<decltype(lit<"a", cr<"0-9">>)>,
                             Lit<"a", false, CpSet<0, false, "0-9">>>);

// Classes join into one class from either side, spelled in order.
static_assert(std::is_same_v<decltype(cr<"a-z_"> | uc::L),
                             decltype(uc::L | cr<"a-z_">)>);
static_assert(std::is_same_v<
              decltype(cr<"$"> | uc::Nd),
              CpSet<detail::cp_bit(unicode::Property::Nd), false, "$">>);
// The syntax characters are spelled `\xNN`.
static_assert(std::is_same_v<
              decltype(cr<R"(+\\-)"> | uc::Nd),
              CpSet<detail::cp_bit(unicode::Property::Nd), false,
                    "+\\x2d\\x5c">>);

// A union is one class, and `~` complements among all characters, ill-formed
// bytes included: twice gives the class back.
static_assert(
    std::is_same_v<decltype(cr<"a-z"> | cr<"0-9">), CpSet<0, false, "0-9a-z">>);
static_assert(
    std::is_same_v<decltype(~(cr<"a"> | cr<"b">)), CpSet<0, true, "ab">>);
static_assert(
    std::is_same_v<decltype(~cr<"a"> | cr<"b">), CpSet<0, true, "a">>);
static_assert(
    std::is_same_v<decltype(~cr<"a"> | ~cr<"a-b">), CpSet<0, true, "a">>);
static_assert(
    std::is_same_v<decltype(~cr<"ab">), std::remove_cv_t<decltype(cr<"^ab">)>>);
static_assert(
    std::is_same_v<decltype(~cr<"^ab">), std::remove_cv_t<decltype(cr<"ab">)>>);
static_assert(std::is_same_v<decltype(~cr<"ab">), CpSet<0, true, "ab">>);
static_assert(std::is_same_v<decltype(~~cr<"ab">), CpSet<0, false, "ab">>);
static_assert(std::is_same_v<decltype(~cr<R"(\D)">), CpSet<0, false, "0-9">>);
static_assert(std::is_same_v<decltype(~~cr<R"(\D)">), CpSet<0, true, "0-9">>);

// A keyword states the boundary only where its spelling ends in the class: its
// last scalar is in it.
using XC = std::remove_cvref_t<decltype(uc::XID_Continue)>;
static_assert(detail::word_final_in<"if", XC>());
static_assert(!detail::word_final_in<"(", XC>());
static_assert(detail::word_final_in<"où", XC>());
static_assert(!detail::word_final_in<"→", XC>());
static_assert(detail::word_final_in<"IF", CpSet<0, false, "a-z">, true>());
static_assert(!detail::word_final_in<"IF", CpSet<0, false, "a-z">>());
static_assert(
    detail::word_final_in<
        "été", std::remove_cvref_t<decltype(cr<R"(\u{80}-\u{10FFFF})">)>>());
static_assert(!detail::word_final_in<"été", CpSet<0, false, "a-z">>());
static_assert(
    detail::word_final_in<"if+", std::remove_cvref_t<decltype(~uc::L)>>());

// Unary ! and & are the lookaheads; binary & is the unordered group.
static_assert(std::is_same_v<decltype(!lit<"a">), Not<Lit<"a">>>);
static_assert(std::is_same_v<decltype(&lit<"a">), And<Lit<"a">>>);
static_assert(std::is_same_v<decltype(lit<"a"> & lit<"b"> & lit<"c">),
                             Unordered<Lit<"a">, Lit<"b">, Lit<"c">>>);

// The directives.
static_assert(std::is_same_v<decltype(assign<&ast::Node::child>(ref<R1>)),
                             Assign<&ast::Node::child, Ref<R1>>>);
static_assert(std::is_same_v<decltype(append<&ast::Node::child>(ref<R1>)),
                             Append<&ast::Node::child, Ref<R1>>>);
static_assert(std::is_same_v<decltype(flag<&ast::Node::many>(kw<"many">)),
                             Flag<&ast::Node::many,
                                      Lit<"many", false, detail::WordBound>>>);
static_assert(
    std::is_same_v<decltype(nest<ast::Node, &ast::Node::child>(lit<"x">)),
                   Nest<ast::Node, &ast::Node::child, Lit<"x">>>);

// `through` is the node -- unrolled to a sequence it would read two ways (a
// scalar run at O0, a byte search under `reduce`).
static_assert(std::is_same_v<decltype(through(lit<"/*">, lit<"*/">)),
                             Through<Lit<"/*">, Lit<"*/">>>);
static_assert(std::is_same_v<decltype(through(lit<"/*">, lit<"*/"> + lit<"x">)),
                             Through<Lit<"/*">, Seq<Lit<"*/">, Lit<"x">>>>);

// A section is one node, and a directive over it lands on the grammar's entry.
static_assert(std::is_same_v<decltype(section<R1>(lit<"<">, lit<">">)),
                             Embed<R1, Seq<Lit<"<">, Ref<R1>, Lit<">">>>>);
static_assert(
    std::is_same_v<
        decltype(assign<&ast::Node::child>(section<R1>(lit<"<">, lit<">">))),
        Embed<R1, Seq<Lit<"<">, Assign<&ast::Node::child, Ref<R1>>,
                      Lit<">">>>>);
static_assert(
    std::is_same_v<
        decltype(append<&ast::Node::child>(section<R1>(lit<"<">, lit<">">))),
        Embed<R1, Seq<Lit<"<">, Append<&ast::Node::child, Ref<R1>>,
                      Lit<">">>>>);

// Charset semantics: membership; reversed-range rejection is a hard error
// (untestable positively).
static_assert(cursiv::make_charset<"a-z">().test('m'));
static_assert(!cursiv::make_charset<"a-z">().test('M'));
static_assert(cursiv::make_charset<"a-zA-Z_">().test('_'));

// A bare `-` between two atoms is a range operator, even right after a range
// -- `"a-z--x"` is 'a'..'z' unioned with '-'..'x', not the set `"a-z\-x"`
// spells.
inline constexpr auto kDashLiteral = cursiv::make_charset<R"(a-z\-x)">();
static_assert(kDashLiteral.test('a') && kDashLiteral.test('z') &&
              kDashLiteral.test('-') && kDashLiteral.test('x'));
static_assert(!kDashLiteral.test('0') && !kDashLiteral.test('A') &&
              !kDashLiteral.test('.'));
static_assert(kDashLiteral.count() == 27);
// ...and the range spelling admits the whole of 0x2d..0x7a.
static_assert(cursiv::make_charset<"a-z--x">().count() == 78);
static_assert(cursiv::make_charset<"a-z--x">().test('.') &&
              cursiv::make_charset<"a-z--x">().test('A'));

// `\xNN` spells one byte outright, so NUL is expressible even though it
// cannot be a literal character.
static_assert(cursiv::make_charset<R"(\x00)">().test(0x00));
static_assert(cursiv::make_charset<R"(\x00)">().count() == 1);
// A range with `\xNN` bounds on both ends: the C0 control range.
inline constexpr auto kC0 = cursiv::make_charset<R"(\x00-\x1f)">();
static_assert(kC0.test(0x00) && kC0.test(0x1f) && !kC0.test(0x20));
static_assert(kC0.count() == 32);
static_assert(cursiv::make_charset<R"(\\)">().test('\\'));
static_assert(cursiv::make_charset<R"(\\)">().count() == 1);

// A class is keyed on its canonical spelling, not on the text it was written
// as: get the spelling wrong and a grammar silently parses a class it never
// declared.
template <cursiv::str Spec>
using CR = std::remove_cv_t<decltype(cr<Spec>)>;
template <std::uint64_t M, bool N, cursiv::str Sp>
constexpr std::string_view spelled(CpSet<M, N, Sp>) {
  return Sp.view();
}

// What decides identity is the set, never the source text: the key is the
// set's canonical spelling, so every way of writing a class collapses onto
// one type -- and two different classes never do.

static_assert(std::is_same_v<CR<"a-z">, CR<"za-y">>);
static_assert(std::is_same_v<CR<"a-z">, CR<"a-mn-z">>);
static_assert(std::is_same_v<CR<"a-zA-Z_">, CR<"_A-Za-z">>);
static_assert(std::is_same_v<CR<"0-9">, CR<"0123456789">>);
// A negation twice, and a complement escape inside a negation, are the class
// again.
static_assert(
    std::is_same_v<decltype(~~cr<R"(\x00-\x1f)">), CR<R"(\x00-\x1f)">>);
static_assert(std::is_same_v<CR<R"(^\D)">, CR<"0-9">>);
// Different sets stay different types, including neighbours one byte apart.
static_assert(!std::is_same_v<CR<"a-z">, CR<"a-y">>);
static_assert(!std::is_same_v<CR<"a-z">, CR<"A-Z">>);
static_assert(!std::is_same_v<CR<"a-z">, CR<"^a-z">>);
static_assert(!std::is_same_v<CR<R"(\x00)">, CR<"">>);

// The key is the readable form: the text a backtrace, `nm -C` and a compiler
// diagnostic show.
static_assert(spelled(cr<"za-y">) == "a-z");
static_assert(spelled(cr<"a-zA-Z0-9_">) == "0-9A-Z_a-z");
static_assert(spelled(cr<" \t\r\n\f\v">) == "\\x09-\\x0d ");
static_assert(spelled(cr<"^\n\r">) == "\\x0a\\x0d");
// A two-character run spells its characters; three or more spell a range.
static_assert(spelled(cr<"ab">) == "ab");
static_assert(spelled(cr<"abc">) == "a-c");
// `-`, `^` and `\` are always escaped, so the only bare `-` in a key is a range
// operator.
static_assert(spelled(cr<R"(\x2d-\x2f)">) == "\\x2d-/");
// Past ASCII, a scalar is spelled by its width.
static_assert(spelled(cr<"é">) == "\\xe9");
static_assert(spelled(cr<"€">) == "\\u20ac");
static_assert(spelled(cr<"😀">) == "\\u{1f600}");
static_assert(spelled(cr<R"(\u{80}-\u{10FFFF})">) == "\\x80-\\u{10ffff}");

// Case folding is a compile-time table; ikw is the case-insensitive keyword.
static_assert(cursiv::to_lower('A') == 'a' && cursiv::to_lower('z') == 'z' &&
              cursiv::to_lower('_') == '_');
static_assert(std::is_same_v<std::remove_cv_t<decltype(ikw<"and">)>,
                             Lit<"and", true, detail::WordBound>>);

// The generic classes exist and are CpSets (usable by every grammar).
static_assert(std::is_same_v<std::remove_cv_t<decltype(s)>,
                             std::remove_cv_t<decltype(cr<R"( \t\r\n\f\v)">)>>);
static_assert(!std::is_same_v<std::remove_cv_t<decltype(w)>,
                              std::remove_cv_t<decltype(d)>>);

// The only law the emitter owes: a set's key reads back as that set. Pinned
// over sets no literal spells -- every singleton, every contiguous range
// around the edges of the encodings, and 20 000 pseudo-random sets from a
// fixed seed.
std::string key_of(const cursiv::ClassRanges& r) {
  std::string out;
  r.spell([&out](char c) { out.push_back(c); });
  return out;
}

bool round_trips(const cursiv::ClassRanges& r) {
  return cursiv::ClassRanges::of(key_of(r)).r == r.r;
}

int sweep_the_round_trip() {
  int bad = 0;
  const auto check = [&bad](cursiv::ClassRanges r) {
    r.merge();
    if (round_trips(r)) return;
    if (++bad <= 5)
      std::printf("FAIL round trip: key [%s]\n", key_of(r).c_str());
  };
  const char32_t edges[] = {0,      1,      0x2c,    0x2d,    0x2e,  0x5b,
                            0x5c,   0x5d,   0x5e,    0x5f,    0x7e,  0x7f,
                            0x80,   0xff,   0x100,   0x7ff,   0x800, 0xd7ff,
                            0xe000, 0xffff, 0x10000, 0x10ffff};
  for (const char32_t lo : edges)
    for (const char32_t hi : edges)
      if (lo <= hi) check({{{lo, hi}}});
  std::uint64_t seed = 0x9e3779b97f4a7c15ull;
  const auto next = [&seed] {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return seed >> 33;
  };
  for (int k = 0; k < 20000; ++k) {
    cursiv::ClassRanges r;
    for (unsigned n = 1 + next() % 6; n-- > 0;) {
      const unsigned span = next() % 4;
      const char32_t top = span == 0   ? 0x80
                           : span == 1 ? 0x800
                           : span == 2 ? 0xd800
                                       : 0x110000;
      char32_t lo = static_cast<char32_t>(next() % top);
      char32_t hi =
          std::min<char32_t>(lo + static_cast<char32_t>(next() % 40), top - 1);
      if (span == 3 && lo >= 0xd800 && lo <= 0xdfff)
        lo = 0xe000, hi = std::max(hi, lo);
      if (hi >= 0xd800 && hi <= 0xdfff) hi = 0xd7ff;
      if (lo <= hi) r.r.push_back({lo, hi});
    }
    check(std::move(r));
  }
  if (bad == 0) std::printf("expr_test: class spelling round trip OK\n");
  return bad;
}

void run() {
  if (sweep_the_round_trip() != 0) ++cursiv::test::failures;
}

}  // namespace expr

namespace source_test {
using namespace cursiv;
namespace left {
struct ID : TerminalRule<lit<"left">> {};
}  // namespace left
namespace right {
struct ID : TerminalRule<lit<"right">> {};
}  // namespace right
struct Node : AstNode {
  std::string_view ID;
};
struct G : GrammarSpec {
  struct z : terminal<lit<"ignored">> {};
  struct Atom : rule<ref<left::ID> + ref<right::ID>> {};
  struct Recursive;
  struct Branch : rule<ref<Atom> + option(ref<Recursive>)> {};
  struct Recursive : rule<ref<Branch> | ref<Atom>> {};
  struct TextOnly : rule<assign<&Node::ID>(lit<"ID">) + cr<"z">> {};
  struct Root : rule<ref<Recursive> + ref<TextOnly>> {};
  using entry = Root;
  using ignore = z;
};
struct Overridden : extends<G> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, left::ID>
  struct override<T> : terminal<ref<right::ID>> {};
};

// The same data layout as emitted source, with storage owned by this fixture.
struct Source {
  GrammarDump dump;
  std::string wrapped;
  std::vector<GrammarSourceRule> rules;
  explicit Source(GrammarDump value)
      : dump(std::move(value)), wrapped(dump_wrapped(dump.text)) {
    for (const auto& rule : dump.rules)
      rules.push_back(
          {rule.name, std::string_view(dump.text).substr(rule.begin, rule.size),
           rule.dependencies});
  }
  DefaultGrammarSource service() const {
    return DefaultGrammarSource({wrapped, rules});
  }
};

void exact_dependencies_and_cycles() {
  Source fixture(dump_compiled_indexed<G>());
  const auto source = fixture.service();
  CHECK(source.grammarText() == dump_wrapped(dump_compiled<G>()));
  CHECK(source.ruleNames().front() == "Root");
  CHECK(source.ruleText("missing").empty());
  CHECK(source.ruleClosure("missing").empty());

  // Neither the member ID, the quoted ID nor the charset z calls a rule.
  CHECK(source.ruleClosure("TextOnly") == source.ruleText("TextOnly"));
  CHECK(source.ruleText("z").starts_with("terminal z:"));
  CHECK(source.ruleText("source_test::right::ID") ==
        "terminal source_test::right::ID: 'right';");
  const std::string atom =
      std::string(source.ruleText("Atom")) + '\n' +
      std::string(source.ruleText("ID")) + '\n' +
      std::string(source.ruleText("source_test::right::ID"));
  CHECK(source.ruleClosure("Atom") == atom);

  // The diamond reaches Atom twice and the cycle reaches Recursive again;
  // each rule appears only once, in first-encounter order.
  CHECK(source.ruleClosure("Recursive") ==
        std::string(source.ruleText("Recursive")) + '\n' +
            std::string(source.ruleText("Branch")) + '\n' + atom);
}

void overrides_change_the_graph() {
  Source fixture(dump_compiled_indexed<Overridden>());
  const auto source = fixture.service();
  CHECK(source.ruleClosure("ID") ==
        std::string(source.ruleText("ID")) + '\n' +
            std::string(source.ruleText("source_test::right::ID")));
}
}  // namespace source_test

namespace grammar_source {

void run() {
  source_test::exact_dependencies_and_cycles();
  source_test::overrides_change_the_graph();
}

}  // namespace grammar_source

// Which rule a reference names, once a derived grammar has had its say.
// Everything here is a `static_assert`: the resolution is a type function, so
// a runtime check would only say that it compiled. The lane exists so a
// change to `ResolveOverride` says which law it broke.
namespace grammar_utils {

namespace {

using namespace cursiv;

// The base: one terminal, one rule, one entry.
struct Doc : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Word : rule<ref<ID>> {};
  struct Root : rule<ref<Word> + many(lit<","> + ref<Word>)> {};
  using entry = Root;
  using ignore = WS;
};

template <class R>
using base_body = std::remove_cv_t<decltype(R::body)>;

// A sequence spliced into a sequence rebuilds flat: what `super` stands for
// does not become a nested part. This says the expected shape without
// respelling the base's own parts.
template <class Body, class... Tail>
struct spliced;
template <class... Bs, class... Tail>
struct spliced<Seq<Bs...>, Tail...> {
  using type = Seq<Bs..., Tail...>;
};
template <class Body, class... Tail>
using spliced_t = typename spliced<Body, Tail...>::type;

// Identity, not a wrapper that forwards: a mint here would double every
// instantiation keyed on the rule, for a grammar that overrides nothing.

using Plain = ResolveOverride<Doc>;

static_assert(std::same_as<Plain::rule<Doc::ID>, Doc::ID>);
static_assert(std::same_as<Plain::rule<Doc::Root>, Doc::Root>);
static_assert(std::same_as<Plain::entry, Doc::Root>);
static_assert(!Plain::overrides<Doc::ID>);
static_assert(!Plain::overrides<Doc::Root>);

struct Wide : extends<Doc> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Doc::ID>
  struct override<T> : terminal<cr<"a-zA-Z_$"> + many(w | lit<"$">)> {};
};

using Wid = ResolveOverride<Wide>;

static_assert(Wid::overrides<Doc::ID>);
// The rule is minted, and it remembers what it stands for: a diagnostic names
// the rule the author wrote, not the mint.
static_assert(!std::same_as<Wid::rule<Doc::ID>, Doc::ID>);
static_assert(std::same_as<Wid::rule<Doc::ID>::declared, Doc::ID>);
// A terminal stays a terminal -- a kind change is refused where it is spelled.
static_assert(std::same_as<Wid::rule<Doc::ID>::kind, Doc::ID::kind>);
// The body is the override's, and it is not the base's.
static_assert(
    std::same_as<Wid::body<Doc::ID>, base_body<Wide::override<Doc::ID>>>);
static_assert(!std::same_as<Wid::body<Doc::ID>, Plain::body<Doc::ID>>);
// ...and what nothing restates is still itself, in the same grammar.
static_assert(std::same_as<Wid::rule<Doc::Word>, Doc::Word>);

// Splicing produces the type the same text spelled out produces. A shape the
// splice builds and the DSL never would -- a choice nested inside a choice,
// say -- is a different grammar to everything downstream.

// The three body shapes an override can stand over, as a terminal and as a
// rule: the algebra is the same, and saying so is what makes it a law.
struct Shapes : GrammarSpec {
  struct SeqT : terminal<lit<"a"> + lit<"b">> {};
  struct SeqR : rule<lit<"a"> + lit<"b">> {};
  struct AltT : terminal<lit<"a"> | lit<"b">> {};
  struct AltR : rule<lit<"a"> | lit<"b">> {};
  struct OneT : terminal<lit<"a">> {};
  struct OneR : rule<lit<"a">> {};
  using entry = SeqR;
};

// ...and what the author would have written instead of overriding.
struct Spelled : GrammarSpec {
  struct SeqThenZ : terminal<lit<"a"> + lit<"b"> + lit<"z">> {};
  struct ZThenSeq : terminal<lit<"z"> + lit<"a"> + lit<"b">> {};
  struct AltThenZ : terminal<(lit<"a"> | lit<"b">)+lit<"z">> {};
  struct SeqOrZ : terminal<(lit<"a"> + lit<"b">) | lit<"z">> {};
  struct AltOrZ : terminal<lit<"a"> | lit<"b"> | lit<"z">> {};
  struct ManyOne : terminal<many(lit<"a">)> {};
  using entry = SeqThenZ;
};

// One override grammar per (rule, spelling). The macro is the only way to keep
// the twelve cases readable; each expands to the four lines a specialization
// of `override<>` takes.
#define CURSIV_OV(NAME, RULE, KIND, ...)       \
  struct NAME : extends<Shapes> {              \
    template <class>                           \
    struct override;                           \
    template <class T>                         \
      requires std::same_as<T, Shapes::RULE>   \
    struct override<T> : KIND<__VA_ARGS__> {}; \
  };

CURSIV_OV(BareT, SeqT, terminal, super)
CURSIV_OV(BareR, AltR, rule, super)
CURSIV_OV(CatSeqT, SeqT, terminal, super + lit<"z">)
CURSIV_OV(CatSeqR, SeqR, rule, super + lit<"z">)
CURSIV_OV(CatAltT, AltT, terminal, super + lit<"z">)
CURSIV_OV(CatAltR, AltR, rule, super + lit<"z">)
CURSIV_OV(PreSeqT, SeqT, terminal, lit<"z"> + super)
CURSIV_OV(AltSeqT, SeqT, terminal, super | lit<"z">)
CURSIV_OV(AltSeqR, SeqR, rule, super | lit<"z">)
CURSIV_OV(AltAltT, AltT, terminal, super | lit<"z">)
CURSIV_OV(AltAltR, AltR, rule, super | lit<"z">)
CURSIV_OV(RepOneR, OneR, rule, many(super))

#undef CURSIV_OV

template <class G, class R>
using eff = typename ResolveOverride<G>::template body<R>;

// Substituting an empty creation must preserve its boundary: the suffix is
// outside `super`, even though spelling `create<N> + suffix` would absorb it.
struct EmptyCreation : GrammarSpec {
  struct Node : AstNode {};
  struct Root : rule<create<Node>> {};
  using entry = Root;
};
struct SuffixedCreation : extends<EmptyCreation> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, EmptyCreation::Root>
  struct override<T> : rule<super + lit<"suffix">> {};
};
static_assert(std::same_as<eff<SuffixedCreation, EmptyCreation::Root>,
                           Seq<Create<EmptyCreation::Node, Epsilon>,
                               Lit<"suffix">>>);

// A bare `super` gives the base body back, whatever its shape.
static_assert(std::same_as<eff<BareT, Shapes::SeqT>, base_body<Shapes::SeqT>>);
static_assert(std::same_as<eff<BareR, Shapes::AltR>, base_body<Shapes::AltR>>);

// Sequence into sequence: it joins, on either side.
static_assert(
    std::same_as<eff<CatSeqT, Shapes::SeqT>, base_body<Spelled::SeqThenZ>>);
static_assert(
    std::same_as<eff<CatSeqR, Shapes::SeqR>, base_body<Spelled::SeqThenZ>>);
static_assert(
    std::same_as<eff<PreSeqT, Shapes::SeqT>, base_body<Spelled::ZThenSeq>>);
// A choice into a sequence is a part, not a join: `(a|b) + z` has two parts.
static_assert(
    std::same_as<eff<CatAltT, Shapes::AltT>, base_body<Spelled::AltThenZ>>);
static_assert(
    std::same_as<eff<CatAltR, Shapes::AltR>, base_body<Spelled::AltThenZ>>);

// Choice into choice: it joins, so the rule keeps three alternatives and not
// two -- a nested choice would change what every walk downstream counts.
static_assert(
    std::same_as<eff<AltAltT, Shapes::AltT>, base_body<Spelled::AltOrZ>>);
static_assert(
    std::same_as<eff<AltAltR, Shapes::AltR>, base_body<Spelled::AltOrZ>>);
// ...and a sequence into a choice is one branch, not three.
static_assert(
    std::same_as<eff<AltSeqT, Shapes::SeqT>, base_body<Spelled::SeqOrZ>>);
static_assert(
    std::same_as<eff<AltSeqR, Shapes::SeqR>, base_body<Spelled::SeqOrZ>>);

// A `super` under a repetition is spliced where it stands.
static_assert(
    std::same_as<eff<RepOneR, Shapes::OneR>, base_body<Spelled::ManyOne>>);

// A terminal and a rule splice alike: the algebra is the body's, not the
// kind's, and the kind is what the mint preserves.
static_assert(
    std::same_as<eff<CatSeqT, Shapes::SeqT>, eff<CatSeqR, Shapes::SeqR>>);
static_assert(
    std::same_as<eff<AltAltT, Shapes::AltT>, eff<AltAltR, Shapes::AltR>>);
static_assert(std::same_as<ResolveOverride<CatSeqT>::rule<Shapes::SeqT>::kind,
                           Shapes::SeqT::kind>);
static_assert(std::same_as<ResolveOverride<CatSeqR>::rule<Shapes::SeqR>::kind,
                           Shapes::SeqR::kind>);

struct Thrice : extends<CatSeqT> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Shapes::SeqT>
  struct override<T> : terminal<super + lit<"y">> {};
};

struct SpelledTwice : GrammarSpec {
  struct Body : terminal<lit<"a"> + lit<"b"> + lit<"z"> + lit<"y">> {};
  using entry = Body;
};

static_assert(ResolveOverride<Thrice>::overrides<Shapes::SeqT>);
static_assert(
    std::same_as<eff<Thrice, Shapes::SeqT>, base_body<SpelledTwice::Body>>);

// A grandchild that restates nothing still sees what its parents made
// effective: the question is asked of the grammar, and it answers up the chain.
struct Quiet : extends<Thrice> {};

static_assert(ResolveOverride<Quiet>::overrides<Shapes::SeqT>);
static_assert(
    std::same_as<eff<Quiet, Shapes::SeqT>, eff<Thrice, Shapes::SeqT>>);

// A diamond is legal while only one side restates the rule; the other's
// silence is not a competing answer. Two would be refused where they meet.

struct Untouched : extends<Shapes> {};

struct Merged : extends<CatSeqT, Untouched> {};

static_assert(ResolveOverride<Merged>::overrides<Shapes::SeqT>);
static_assert(
    std::same_as<eff<Merged, Shapes::SeqT>, eff<CatSeqT, Shapes::SeqT>>);
static_assert(
    std::same_as<ResolveOverride<Merged>::rule<Shapes::AltR>, Shapes::AltR>);

// ...and while both sides reach one override through their common ancestor:
// they restate nothing, so there is one answer and not two.
struct Left : extends<Thrice> {};
struct Right : extends<Thrice> {};
struct Joined : extends<Left, Right> {};

static_assert(ResolveOverride<Joined>::overrides<Shapes::SeqT>);
static_assert(
    std::same_as<eff<Joined, Shapes::SeqT>, eff<Thrice, Shapes::SeqT>>);

// `Overridden` wears the override's declaration: what the override states of
// itself -- its slot, and the converter the slot carries -- is what the rule
// answers, the base's only where the override restates neither.

struct Tagged;

struct Slotted : extends<Doc> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Doc::ID>
  struct override<T> : terminal<super, Tagged> {};
};

static_assert(std::same_as<ResolveOverride<Slotted>::rule<Doc::ID>::slot,
                           TypeList<Tagged>>);
static_assert(std::same_as<Plain::rule<Doc::ID>::slot, TypeList<>>);

// The resolution is a push: a grammar is asked rule by rule, never for the
// list of what it overrides. So a declaration for a rule outside the entry's
// reach costs nothing and answers when asked.

struct Elsewhere : GrammarSpec {
  struct Never : terminal<lit<"never">> {};
};

struct Reaching : extends<Doc> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Elsewhere::Never>
  struct override<T> : terminal<lit<"always">> {};
};

static_assert(ResolveOverride<Reaching>::overrides<Elsewhere::Never>);
static_assert(!ResolveOverride<Reaching>::overrides<Doc::ID>);
static_assert(std::same_as<ResolveOverride<Reaching>::rule<Doc::ID>, Doc::ID>);

struct Made;

struct Built : GrammarSpec {
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Node : rule<create<Made>(ref<ID>)> {};
  using entry = Node;
};

static_assert(
    std::same_as<ResolveOverride<Built>::body<Built::Node>, Ref<Built::ID>>);

// `MaterializeCreate`: the creation a rule implies, spelled out. A node rule
// builds a node, and today that creation lives either in the body -- every
// alternative heading itself -- or nowhere, carried by the rule's own
// element. This stage makes it always the first.

namespace mk {

struct Node : cursiv::AstNode {
  std::string_view name;
};

struct G : GrammarSpec {
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  // No alternative heads itself: the rule owed the creation.
  struct Plain : rule<assign<&Node::name>(ref<ID>)> {};
  // Every alternative already heads itself.
  struct Both : rule<create<Node>(lit<"+">) | create<Node>(lit<"1">)> {};
  // One does, one does not.
  struct Mixed : rule<create<Node>(lit<"+">) | assign<&Node::name>(ref<ID>)> {};
  // A rule that builds no node owes nothing.
  struct Data : rule<ref<ID>> {};
  using entry = Plain;
};

// The same language with the creation spelled out. It shares `G::ID`, so the
// two bodies differ in nothing but where the creation was written.
struct Spelled : GrammarSpec {
  struct Plain : rule<create<Node>(assign<&Node::name>(ref<G::ID>))> {};
  struct Mixed : rule<create<Node>(lit<"+">) |
                      create<Node>(assign<&Node::name>(ref<G::ID>))> {};
  using entry = Plain;
};

using M = Normalize<G>;
using R = ResolveOverride<G>;

// A body's alternatives, each asked whether it heads itself. This is the
// invariant the transformation exists to establish.
template <class B>
inline constexpr bool every_alt_heads = detail::mat::look_heads<B>;
template <class... As>
inline constexpr bool every_alt_heads<Choice<As...>> =
    (detail::mat::look_heads<As> && ...);

// Nothing headed it: the whole alternative becomes the region of one creation.
static_assert(M::owed<G::Plain>);
static_assert(std::same_as<M::body<G::Plain>, Create<Node, R::body<G::Plain>>>);
// The two spellings converge: whether the author wrote the creation or left
// the rule to imply it, the body after this is one type. A `create` at the
// root of a declared body is shed, so the explicit spelling arrives here owing
// one just as the implicit does.
static_assert(
    std::same_as<M::body<G::Plain>, Normalize<Spelled>::body<Spelled::Plain>>);

// Every alternative headed itself: nothing to do, and the body is untouched.
static_assert(!M::owed<G::Both>);
static_assert(std::same_as<M::body<G::Both>, R::body<G::Both>>);

// One branch owed it: the other keeps the creation it spelled, and the arity
// of the choice does not move -- an alternative in is an alternative out.
static_assert(M::owed<G::Mixed>);
static_assert(
    std::same_as<M::body<G::Mixed>, Normalize<Spelled>::body<Spelled::Mixed>>);

// A rule that builds no node, and a terminal, owe nothing.
static_assert(!M::owed<G::Data>);
static_assert(!M::owed<G::ID>);
static_assert(std::same_as<M::body<G::ID>, R::body<G::ID>>);

// "Alternative" is not a shape a rule must have: a body that is one sequence
// is one alternative, and it heads itself or it does not.

struct Flat : GrammarSpec {
  struct Sub : rule<create<Node>(ref<G::ID>)> {};
  // Nothing in the sequence heads it.
  struct SeqPlain : rule<lit<"a"> + assign<&Node::name>(ref<G::ID>)> {};
  // A `create` written inside a sequence absorbs its neighbours, so the
  // declared body is already headed -- and being at the root, it is shed
  // before this reads it, and put back over the same region.
  struct SeqInner : rule<lit<"a"> + create<Node>(ref<G::ID>) + lit<"b">> {};
  // The head comes from the callee, and the rule adopts it.
  struct RefAdopt : rule<ref<Sub>> {};
  // A repetition that may match nothing heads nothing...
  struct RepMany : rule<create<Node> + many(assign<&Node::name>(ref<G::ID>))> {
  };
  using entry = SeqPlain;
};

using F = Normalize<Flat>;
using FR = ResolveOverride<Flat>;

static_assert(F::owed<Flat::SeqPlain>);
static_assert(
    std::same_as<F::body<Flat::SeqPlain>,
                 Create<Node, Seq<Lit<"a">, Assign<&Node::name, Ref<G::ID>>>>>);

// The absorbed create came back over exactly the region it had claimed.
static_assert(std::same_as<F::body<Flat::SeqInner>, base_body<Flat::SeqInner>>);

static_assert(!F::owed<Flat::RefAdopt>);
static_assert(std::same_as<F::body<Flat::RefAdopt>, Ref<Flat::Sub>>);

static_assert(F::owed<Flat::RepMany>);
// Each further turn of a repetition of a head is a head of its own.
static_assert(!detail::mat::look<Repeat<1, 1, Ref<Flat::Sub>>>::inside);
static_assert(detail::mat::look<Repeat<1, kUnbounded, Ref<Flat::Sub>>>::inside);

// ...and every one of them heads itself afterwards.
static_assert(every_alt_heads<F::body<Flat::SeqPlain>>);
static_assert(every_alt_heads<F::body<Flat::SeqInner>>);
static_assert(every_alt_heads<F::body<Flat::RefAdopt>>);
static_assert(every_alt_heads<F::body<Flat::RepMany>>);

// A nest makes its wrapper the current node. What follows belongs to the
// wrapper, so the head creation claims only what stands before it -- and its
// type is what the head writes, never the rule's own answer, which with a
// nest in the body is the wrapper.

namespace nst {

// `cursiv::Extends` and not plain inheritance: the deduction walks a node's
// bases through `ast_bases`, and a common base it cannot see is a common base
// it answers `void` for.
struct Inner : cursiv::Extends<cursiv::AstNode> {
  std::string_view name;
};
struct Wrap : cursiv::Extends<Inner> {
  Inner* inner = nullptr;
};

struct G : GrammarSpec {
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  // The head is written, then wrapped.
  struct AN : rule<assign<&Inner::name>(ref<ID>) +
                   nest<Wrap, &Wrap::inner>(lit<"!">)> {};
  // ...and wrapped again on every turn.
  struct ANR : rule<assign<&Inner::name>(ref<ID>) +
                    many(nest<Wrap, &Wrap::inner>(lit<"!">))> {};
  using entry = AN;
};

using M = Normalize<G>;
using R = ResolveOverride<G>;

// The head is the class declaring the members written, an `Inner`; the rule
// returns the wrapper, a `Wrap`.
static_assert(std::same_as<rule_type_t<R::rule<G::AN>>, Wrap>);
static_assert(std::same_as<M::body<G::AN>,
                           Seq<Create<Inner, Assign<&Inner::name, Ref<G::ID>>>,
                               Nest<Wrap, &Wrap::inner, Lit<"!">>>>);

// A repetition holding a nest sets the same boundary. The DSL's algebra does
// not stop there on its own -- a spelled create absorbs the repetition whole,
// and being at the root it is then shed -- so this is where the shape would
// be lost without asking the question.
static_assert(std::same_as<rule_type_t<R::rule<G::ANR>>, Inner>);
static_assert(std::same_as<M::body<G::ANR>,
                           Seq<Create<Inner, Assign<&Inner::name, Ref<G::ID>>>,
                               Repeat<0, static_cast<std::size_t>(-1),
                                      Nest<Wrap, &Wrap::inner, Lit<"!">>>>>);

//     A returns A : name=ID | ref<B>;      where B returns a class deriving
//     from A
//
// The two branches answer differently and both are right: one writes the
// head, the other has it built for it. So one owes a creation and the other
// owes nothing -- the alternative, not the rule, is the unit.

struct Sub : cursiv::Extends<Inner> {
  std::string_view extra;
};
struct Apart : cursiv::Extends<cursiv::AstNode> {
  std::string_view x;
};

struct Fam : GrammarSpec {
  struct BRule : rule<create<Sub>(assign<&Sub::extra>(ref<G::ID>))> {};
  struct ARule : rule<assign<&Inner::name>(ref<G::ID>) | ref<BRule>> {};
  // The reference leaves the family altogether: the common base is all the
  // rule can answer, and it still bases what the branch builds.
  struct ORule : rule<create<Apart>(assign<&Apart::x>(ref<G::ID>))> {};
  struct Mixed
      : rule<create<Inner>(assign<&Inner::name>(ref<G::ID>)) | ref<ORule>> {};
  using entry = ARule;
};

using FM = Normalize<Fam>;

// The rule answers what both branches are: the class the writes name, and the
// one the callee builds -- their common base, which here is the first.
static_assert(std::same_as<rule_type_t<Fam::ARule>, Inner>);

// One branch owes, the other does not. `ref<BRule>` is a head: `Inner` bases
// `Sub`, so the rule adopts what the callee built -- wrapping it would put a
// second node over the first.
static_assert(FM::owed<Fam::ARule>);
static_assert(
    std::same_as<FM::body<Fam::ARule>,
                 Choice<Create<Inner, Assign<&Inner::name, Ref<G::ID>>>,
                        Ref<Fam::BRule>>>);
static_assert(detail::mat::look_heads<FM::body<Fam::ARule>>);

// A branch that leaves the family: the rule can only answer the base both
// share, and a reference to a class it bases is a head all the same.
static_assert(std::same_as<rule_type_t<Fam::Mixed>, cursiv::AstNode>);
static_assert(!FM::owed<Fam::Mixed>);

// `ref<A> + many(nest<&B::a>(...))` where A builds a node: the left-nested
// chain of a postfix expression, the one shape here that uses `nest<>` for
// real. The head comes from the callee, so the rule adopts it and owes
// nothing.

struct Chain : GrammarSpec {
  struct ARule : rule<create<Inner>(assign<&Inner::name>(ref<G::ID>))> {};
  // The head is adopted...
  struct Post : rule<ref<ARule> + many(nest<&Wrap::inner>(lit<"!">))> {};
  using entry = Post;
};

using CM = Normalize<Chain>;

// The rule answers what both paths are: the node the callee built, and the
// wrapper the chain leaves -- their common base.
static_assert(std::same_as<rule_type_t<Chain::Post>, Inner>);

// Nothing is owed, and the body is untouched.
static_assert(!CM::owed<Chain::Post>);
static_assert(std::same_as<CM::body<Chain::Post>,
                           ResolveOverride<Chain>::body<Chain::Post>>);
static_assert(detail::mat::look_heads<CM::body<Chain::Post>>);

//     [bare...] [Create<T, ...>] Nest<W1, ...> Nest<W2, ...> ...
//
// One head, then a chain of wrappers. The DSL's own algebra already refuses
// two creates in a sequence, and a create after a nest -- so only a nest with
// nothing in front of it is this stage's to catch.
template <class A>
inline constexpr bool one_head =
    !detail::mat::chain<typename detail::mat::alt_parts<A>::type>::twice &&
    !detail::mat::chain<typename detail::mat::alt_parts<A>::type>::bare;

struct Shape : GrammarSpec {
  // A written head, then two wrappers in a row: the second buries the first.
  struct Two
      : rule<assign<&Inner::name>(ref<G::ID>) + nest<&Wrap::inner>(lit<"!">) +
             nest<&Wrap::inner>(lit<"?">)> {};
  // A head that only a repetition can make, and it must run at least once.
  struct Some : rule<create<Wrap> + some(nest<&Wrap::inner>(lit<"!">))> {};
  using entry = Two;
};

using ShM = Normalize<Shape>;

// One branch of a nested choice is enough to be a second head, and a create
// absorbs what surrounds it -- so the second head is buried inside the
// first's region rather than standing beside it. A branch that merely calls
// a rule building a node of the family counts the same.
//
//   create<N>(a) + (create<N>(b) | 'c')   refused, for the first branch
//   create<N>(a) + (ref<B>       | 'c')   refused, B building a node
//   create<N>(a) + ('c'          | 'd')   legal
static_assert(
    !one_head<Create<
        Inner, Seq<Lit<"a">, Choice<Create<Inner, Lit<"b">>, Lit<"c">>>>>);
static_assert(
    one_head<Create<Inner, Seq<Lit<"a">, Choice<Lit<"c">, Lit<"d">>>>>);

// A chain of nests is one head and N wrappers: the second nest has the first
// one's wrapper in front of it, which is a current node like any other.
static_assert(one_head<ShM::body<Shape::Two>>);
static_assert(std::same_as<ShM::body<Shape::Two>,
                           Seq<Create<Inner, Assign<&Inner::name, Ref<G::ID>>>,
                               Nest<Wrap, &Wrap::inner, Lit<"!">>,
                               Nest<Wrap, &Wrap::inner, Lit<"?">>>>);

// ...and the shape holds after the stage for the ones seen above.
static_assert(one_head<ShM::body<Shape::Some>>);
static_assert(one_head<FM::body<Fam::ARule>>);
static_assert(one_head<CM::body<Chain::Post>>);

// `nest<T, &T::f>` names the wrapper; `nest<&T::f>` lets the member name it.
// A pointer to member already says which class it belongs to.

struct Two : GrammarSpec {
  struct Long : rule<assign<&Inner::name>(ref<G::ID>) +
                     nest<Wrap, &Wrap::inner>(lit<"!">)> {};
  struct Short
      : rule<assign<&Inner::name>(ref<G::ID>) + nest<&Wrap::inner>(lit<"!">)> {
  };
  using entry = Long;
};

// The same node, so everything downstream reads one shape.
static_assert(std::same_as<base_body<Two::Long>, base_body<Two::Short>>);
static_assert(std::same_as<Normalize<Two>::body<Two::Long>,
                           Normalize<Two>::body<Two::Short>>);

// A member declared in a base names that base, not the class the author
// spelled it through: `&Wrap::name` is an `Inner` member, so the short form
// deduces `Inner`. The long form is how a wrapper deeper than its member is
// named.
static_assert(std::same_as<decltype(nest<&Wrap::name>(lit<"!">)),
                           Nest<Inner, &Wrap::name, Lit<"!">>>);
static_assert(std::same_as<decltype(nest<Wrap, &Wrap::name>(lit<"!">)),
                           Nest<Wrap, &Wrap::name, Lit<"!">>>);

// ...and the other way round. Where the rule's answer is the common base of
// a choice, a branch that writes a derived class's members builds that
// class: the rule's type is not in its family, so the writes decide.
struct Split : GrammarSpec {
  struct Deep
      : rule<create<Inner>(lit<"b">) | assign<&Wrap::inner>(ref<G::ID>)> {};
  using entry = Deep;
};

using SM = Normalize<Split>;

static_assert(std::same_as<rule_type_t<Split::Deep>, Inner>);
static_assert(
    std::same_as<SM::body<Split::Deep>,
                 Choice<Create<Inner, Lit<"b">>,
                        Create<Wrap, Assign<&Wrap::inner, Ref<G::ID>>>>>);

// Every alternative heads itself afterwards, wrapper or not.
static_assert(detail::mat::look_heads<M::body<G::AN>>);
static_assert(detail::mat::look_heads<M::body<G::ANR>>);

}  // namespace nst

// A head built in a choice, then wrapped by nests: the rule hands back
// whatever a path ends on, so its type is their common base.
namespace hand {

struct Base : cursiv::Extends<cursiv::AstNode> {};
struct Leaf : cursiv::Extends<Base> {
  std::string_view name;
};
struct Leafy : cursiv::Extends<Leaf> {};
struct Paren : cursiv::Extends<Base> {
  Base* inner = nullptr;
};
struct Call : cursiv::Extends<Base> {
  Base* callee = nullptr;
};
struct Mid : cursiv::Extends<Base> {};
struct Dot : cursiv::Extends<Mid> {
  Base* object = nullptr;
};
struct Bang : cursiv::Extends<Mid> {
  Base* operand = nullptr;
};

struct G : GrammarSpec {
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  static constexpr auto Head =
      create<Leaf>(assign<&Leaf::name>(ref<ID>)) |
      create<Paren>(lit<"("> + assign<&Paren::inner>(ref<ID>) + lit<")">);
  // No wrap, or one per turn.
  struct Many : rule<Head + many(nest<Call, &Call::callee>(lit<"!">))> {};
  // At least one wrap.
  struct Some : rule<Head + some(nest<Call, &Call::callee>(lit<"!">))> {};
  // A turn that wraps nothing hands the head on.
  struct SomeOr
      : rule<Head + some(nest<Call, &Call::callee>(lit<"!">) | lit<";">)> {};
  // One of two wrappers, always: what they share.
  static constexpr auto Either =
      nest<Dot, &Dot::object>(lit<".">) | nest<Bang, &Bang::operand>(lit<"?">);
  struct Wrapped : rule<Head + Either> {};
  // The same over a node adopted, which the wrapper buries.
  struct AdoptedWrapped : rule<ref<Many> + Either> {};
  // And over the rule's own creation, which never stands alone.
  struct RootWrapped
      : rule<create<Leaf>(assign<&Leaf::name>(ref<ID>)) + Either> {};
  // The head is the node a call adopts, or the one a write implies.
  struct LeafRule : rule<assign<&Leaf::name>(ref<ID>)> {};
  struct AdoptMany
      : rule<ref<LeafRule> + many(nest<Call, &Call::callee>(lit<"!">))> {};
  struct AdoptSome
      : rule<ref<LeafRule> + some(nest<Call, &Call::callee>(lit<"!">))> {};
  struct WroteMany : rule<assign<&Leaf::name>(ref<ID>) +
                          many(nest<Call, &Call::callee>(lit<"!">))> {};
  // A member answers the class that declares it, whichever class names it.
  struct Named : rule<assign<&Leafy::name>(ref<ID>)> {};
  struct Stated : rule<create<Leafy>(assign<&Leafy::name>(ref<ID>))> {};
  using entry = Many;
};
using R = ResolveOverride<G>;

static_assert(std::same_as<rule_type_t<R::rule<G::Many>>, Base>);
static_assert(std::same_as<rule_type_t<R::rule<G::Some>>, Call>);
static_assert(std::same_as<rule_type_t<R::rule<G::SomeOr>>, Base>);
static_assert(std::same_as<rule_type_t<R::rule<G::Wrapped>>, Mid>);
static_assert(std::same_as<rule_type_t<R::rule<G::AdoptedWrapped>>, Mid>);
static_assert(std::same_as<rule_type_t<R::rule<G::RootWrapped>>, Mid>);
static_assert(std::same_as<rule_type_t<R::rule<G::AdoptMany>>, Base>);
static_assert(std::same_as<rule_type_t<R::rule<G::AdoptSome>>, Call>);
static_assert(std::same_as<rule_type_t<R::rule<G::WroteMany>>, Base>);
static_assert(std::same_as<rule_type_t<R::rule<G::Named>>, Leaf>);
static_assert(std::same_as<rule_type_t<R::rule<G::Stated>>, Leafy>);
// Its head is still the class it creates.
static_assert(std::same_as<Normalize<G>::body<G::RootWrapped>,
                           Seq<Create<Leaf, Assign<&Leaf::name, Ref<G::ID>>>,
                               std::remove_cvref_t<decltype(G::Either)>>>);

}  // namespace hand

// After the transformation every node rule adopts: its element is a pure
// label, in every grammar rather than in some. That is the whole point, and
// it did not hold before.

static_assert(!every_alt_heads<R::body<G::Plain>>);
static_assert(!every_alt_heads<R::body<G::Mixed>>);

static_assert(every_alt_heads<M::body<G::Plain>>);
static_assert(every_alt_heads<M::body<G::Both>>);
static_assert(every_alt_heads<M::body<G::Mixed>>);

// An override restates the body; the creation is owed on what the grammar
// actually compiles, not on what the base declared.

struct Restated : extends<G> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, G::Plain>
  struct override<T> : rule<create<Node>(lit<"x">)> {};
};

// The creation is owed on the body the grammar compiles: the override's, not
// the base's -- and the answer heads itself either way.
static_assert(
    std::same_as<Normalize<Restated>::body<G::Plain>, Create<Node, Lit<"x">>>);
static_assert(
    !std::same_as<Normalize<Restated>::body<G::Plain>, M::body<G::Plain>>);
static_assert(every_alt_heads<Normalize<Restated>::body<G::Plain>>);

}  // namespace mk

// The public surface, answered again by every stage: a transformation of a
// grammar is a grammar. What it does not republish, the stage after it
// cannot ask, so each one answers the whole surface -- the pipeline is a
// chain rather than a set of one-shot readers.

namespace api {

struct Node : cursiv::AstNode {
  std::string_view name;
};

struct Base : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct DOC : terminal<lit<"//"> + many(cr<R"(^\n)">)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<assign<&Node::name>(ref<ID>)> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<DOC>;
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_">;
};

// The separator is a rule like any other, so a derived grammar restates it
// by overriding it -- and the resolution has to reach it, not only the rules
// the entry names.
struct Wider : extends<Base> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Base::WS>
  struct override<T> : terminal<some(s | lit<"\\">)> {};
  template <class U>
    requires std::same_as<U, Base::DOC>
  struct override<U> : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};
};

using P0 = ResolveOverride<Base>;
using P1 = ResolveOverride<Wider>;
using M0 = MaterializeCreate<P0>;
using M1 = MaterializeCreate<P1>;

// `ignore` may be a rule or a list; it comes out a list either way.
static_assert(std::same_as<P0::ignore, TypeList<Base::WS>>);
static_assert(std::same_as<P0::hidden, TypeList<Base::DOC>>);
static_assert(std::same_as<P0::entry, Base::Root>);

// An override reaches the skipper: both lists carry the minted rule, not the
// declared one, and the mint still names what it stands for.
static_assert(!std::same_as<P1::ignore, P0::ignore>);
static_assert(!std::same_as<P1::hidden, P0::hidden>);
static_assert(P1::overrides<Base::WS>);
static_assert(P1::overrides<Base::DOC>);
static_assert(std::same_as<P1::ignore, TypeList<P1::rule<Base::WS>>>);
static_assert(std::same_as<P1::hidden, TypeList<P1::rule<Base::DOC>>>);
static_assert(std::same_as<P1::rule<Base::WS>::declared, Base::WS>);
// ...and what nothing restates is still itself.
static_assert(std::same_as<P1::entry, Base::Root>);

// `str` has no `operator==` -- it is a class NTTP, and equal keys
// are the same template argument. The class the key denotes says it.
static_assert(std::same_as<decltype(P0::word_boundary), decltype(P1::word_boundary)>);

// The second stage hands the surface on -- and hands on what it did. An entry
// whose body still implied its creation would make the stage a matter of
// which way you asked.

// The two ways to ask agree, and that is the whole contract.
static_assert(std::same_as<std::remove_cv_t<decltype(M0::entry::body)>,
                           M0::body<Base::Root>>);
// So the entry is a mint, not the rule that came in -- and it says what it
// stands for at the stage before.
static_assert(!std::same_as<M0::entry, P0::entry>);
static_assert(std::same_as<M0::entry::declared, P0::entry>);
// ...while its identity is untouched: a rule stays the kind it was declared.
static_assert(std::same_as<M0::entry::kind, Base::Root::kind>);

// A root `create` in a stage's body is not an annotation. `rule_body_t`
// sheds one -- it reads a declared body, where a root create only states a
// type a deduction cannot reach. A stage means it, so a stage's body is read
// through the stage.
static_assert(!std::same_as<rule_body_t<M0::entry>, M0::body<Base::Root>>);

// The skipper's rules are terminals: they build no node and owe nothing, so
// they come through as themselves.
static_assert(std::same_as<M0::ignore, P0::ignore>);
static_assert(std::same_as<M0::hidden, P0::hidden>);
static_assert(std::same_as<decltype(M0::word_boundary), decltype(P0::word_boundary)>);

static_assert(std::same_as<M1::ignore, P1::ignore>);
static_assert(std::same_as<M1::hidden, P1::hidden>);

// ...and it is still the stage that materialises: the entry owed a head.
static_assert(M0::owed<Base::Root>);
static_assert(std::same_as<M0::body<Base::Root>,
                           Create<Node, Assign<&Node::name, Ref<Base::ID>>>>);

// A third stage could be written against either: what a stage publishes is
// what a grammar publishes, so the chain has no last link by construction.
using M2 = MaterializeCreate<M0>;
static_assert(std::same_as<M2::ignore, P0::ignore>);
static_assert(std::same_as<M2::body<Base::Root>, M0::body<Base::Root>>);
// Running it again changes nothing -- not even the rule's identity: every
// alternative already heads itself, so there is nothing to mint.
static_assert(!M2::owed<Base::Root>);
static_assert(std::same_as<M2::entry, M0::entry>);

}  // namespace api

}  // namespace

void run() {
  // Every law above is a `static_assert`; reaching here is the verdict.
}

}  // namespace grammar_utils

// The two ways a literal is guarded, and the one node behind both: `kw<S>`
// asks for the grammar's boundary discipline (`word_boundary`, declared like
// `ignore`/`hidden`) and lowers to `lit<S, !boundary>` conditionally; `lit<S,
// G>` is the custom path, any predicate applied as spelled.
namespace grammar_spec {

namespace {

using namespace cursiv;

// `kw<>` carries a sentinel, not a decision: which characters bound a token
// is what a grammar declares, and none is in scope at a factory.
static_assert(std::is_same_v<std::remove_cv_t<decltype(kw<"if">)>,
                             Lit<"if", false, detail::WordBound>>);
static_assert(std::is_same_v<std::remove_cv_t<decltype(kw<"[">)>,
                             Lit<"[", false, detail::WordBound>>);
static_assert(std::is_same_v<std::remove_cv_t<decltype(ikw<"if">)>,
                             Lit<"if", true, detail::WordBound>>);

// `lit<S>` has no guard: the node parameter stays void where none was handed.
static_assert(
    std::is_same_v<std::remove_cv_t<decltype(lit<"toto">)>, Lit<"toto">>);

const cursiv::Parser& spellplain() {
  static const std::unique_ptr<cursiv::Parser> p =
      tspell::generated::Plain::create_parser();
  return *p;
}
const cursiv::Parser& spelldashed() {
  static const std::unique_ptr<cursiv::Parser> p =
      tspell::generated::Dashed::create_parser();
  return *p;
}

bool parses(const cursiv::Parser& engine, std::string_view text) {
  const cursiv::ParseResult r = engine.parse(text);
  return r.accepted && !r.cst().recovered();
}

// One rule, no special case: the guard exists exactly where the text can run
// into the declared class. `[` is in no usual boundary, so `kw<"[">` accepts
// `[abc`; a grammar that puts `[` in its boundary gets the guard, and the
// same input is refused there.

void the_word_final_rule() {
  // '[' is in no usual boundary, so the keyword carries no guard...
  CHECK(cursiv::dump_compiled<tspell::Default>().find("'[' NAME") !=
        std::string::npos);
  // ...nor where the declared class is digits, which '[' is not...
  CHECK(cursiv::dump_compiled<tspell::Digits>().find("'[' NAME") !=
        std::string::npos);
  // ...and it gains one exactly where the class admits it.
  CHECK(cursiv::dump_compiled<tspell::Bracketed>().find("\"[\"\\b[[a-z]") !=
        std::string::npos);
  // A `declared<>` view shows the request, not the answer: `kw<>` asked.
  CHECK(cursiv::dump_declared<tspell::Default>().find("\"[\" NAME") !=
        std::string::npos);
}

void the_declared_class_decides() {
  CHECK(parses(spellplain(), "if x"));
  CHECK(parses(spelldashed(), "if x"));
  CHECK(parses(spellplain(), "if-x"));    // '-' ends the token here
  CHECK(!parses(spelldashed(), "if-x"));  // ...and does not there
  // The guard is a boundary, not a separator: `_` is a word character in both.
  CHECK(!parses(spellplain(), "if_x"));
  CHECK(!parses(spelldashed(), "if_x"));
}

// Dropping the guard that cannot fire is what keeps `kw<"(">` and `lit<"(">`
// one element row -- per grammar, the only scope in which that is true.

struct Fused : cursiv::GrammarSpec {
  struct A : rule<kw<"("> + lit<"("> + kw<"if">> {};
  using entry = A;
};
struct Kept : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"(a-z">;
  struct A : rule<kw<"("> + lit<"("> + kw<"if">> {};
  using entry = A;
};

void the_hub_fuses_what_cannot_fire() {
  // Two spellings of '(' that no reading can tell apart are one matcher, and
  // the dump is where the fusion shows.
  CHECK(cursiv::dump_compiled<Fused>().find("'(' '(' \"if\"") !=
        std::string::npos);
  // ...and a boundary that admits '(' tells them apart, so the `kw` keeps its
  // guard and no fusion happens. Same three elements, two answers: the
  // discipline is what decides, never the spelling.
  CHECK(cursiv::dump_compiled<Kept>().find("'(' '(' \"if\"") ==
        std::string::npos);
}

// The level table is read outside the body walk, by the climb and by the
// element table: it goes through the same resolution, or `kw<"*">` would keep
// a guard the grammar dropped and stop matching `2*3`.

void an_operator_is_resolved_too() {
  CHECK(cursiv::dump_compiled<tspell::Calc>().find("left('*') >> left('+')") !=
        std::string::npos);
}

// ...and the same law from the other side: an `ikw<>` must hold a letter, or
// the case-insensitivity is a no-op and `kw<>` is the spelling.
static_assert(cursiv::detail::holds_a_letter<"if">());
static_assert(cursiv::detail::holds_a_letter<"IF">());
static_assert(!cursiv::detail::holds_a_letter<"==">());
static_assert(!cursiv::detail::holds_a_letter<"5">());

}  // namespace

void run() {
  the_word_final_rule();
  the_declared_class_decides();
  the_hub_fuses_what_cannot_fire();
  an_operator_is_resolved_too();
}

}  // namespace grammar_spec

// Outside the anonymous namespace, so its qualified name is stable across
// compilers when it collides with Dg::ID in one closure.
namespace dumpcol {
struct ID : cursiv::TerminalRule<cursiv::some(cursiv::cr<"0-9">)> {};
}  // namespace dumpcol

// Pins the rendering: every shape's spelling, bracketing by precedence, the
// grammar skeleton, and that compiled<> shows the override where declared<>
// shows the base.
namespace dump {

namespace {

void expect(const std::string& got, std::string_view want, const char* what) {
  if (got != want) {
    std::printf("FAIL %s\n  got  [%s]\n  want [%s]\n", what, got.c_str(),
                std::string(want).c_str());
    ++failures;
  }
}

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  bool many = false;
  std::string_view name;
  Node* target = nullptr;
};
}  // namespace ast

struct Open {};
struct Close {};

using namespace cursiv;

struct Dg : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-z"> + many(w)> {};
  struct Item : rule<kw<"item"> + assign<&ast::Node::name>(ref<ID>)> {};
  using entry = Item;
  using ignore = WS;
};

struct D2 : cursiv::extends<Dg> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Dg::ID>
  struct override<T> : terminal<cr<"A-Z"> + many(w)> {};
};

}  // namespace

namespace {
struct R3 : cursiv::Rule<cursiv::create<ast::Node> + cursiv::ref<Dg::ID> +
                         cursiv::ref<dumpcol::ID>> {};

// data rule, converter-typed terminal, no-skipper grammar.
struct Q : cursiv::GrammarSpec {
  struct NUM : terminal<some(d), int> {};
  // A converter answering the positioned result: the rule's value type is
  // still the value, so the header reads `returns float64`, not the wrapper.
  static constexpr auto real_conv = [](std::string_view t) {
    return cursiv::ConversionResult<double>::ok(static_cast<double>(t.size()));
  };
  struct REAL
      : terminal<some(d) + lit<"."> + some(d), cursiv::text_conv<real_conv>> {};
  struct QN : rule<some(ref<NUM>, lit<",">)> {};
  struct R : rule<assign<&ast::Node::name>(ref<QN>) + ref<REAL>> {};
  using entry = R;
};
}  // namespace

// A skip() region held in a shared sub-expression: it prints inline, where it
// stands, not as a rule of its own.
namespace Q2 {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto Pair =
      tight(ref<ID> + lit<":"> + ref<ID>);
  struct Root : rule<kw<"map"> + Pair + kw<"end">> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace Q2

// An inline rule has no identifier: the dump numbers it like a synthetic one,
// so the printed grammar can still refer back to it.
namespace Q4 {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<ref<terminal<some(cr<"0-9">)>> + ref<rule<kw<"("> + kw<")">>>> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace Q4

namespace Q3 {
struct Bin {
  Bin* left = nullptr;
  std::string_view op;
  Bin* right = nullptr;
};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Num : terminal<some(d)> {};
  struct Expr : infix<Num, &Bin::left, &Bin::op, &Bin::right,
                      (left(kw<"*"> | kw<"/">) >> left(kw<"+"> | kw<"-">) >>
                       nonassoc(kw<"<">))> {};
  using entry = Expr;
  using ignore = WS;
};
}  // namespace Q3

// Overridden rules the compiled view meets twice: as a mint (the entry, the
// skipper) and by the name a body spells.
namespace Q5 {
struct SelfBase : cursiv::GrammarSpec {
  struct ID : terminal<some(cr<"a-z">)> {};
  struct List : rule<ref<ID> + option(lit<","> + ref<List>)> {};
  using entry = List;
};
struct SelfDerived : cursiv::extends<SelfBase> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, SelfBase::List>
  struct override<T> : rule<ref<ID> + option(lit<";"> + ref<List>)> {};
};
struct SkipBase : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Top : rule<ref<ID> + tight(ref<WS> + ref<ID>)> {};
  using entry = Top;
  using ignore = WS;
};
struct SkipDerived : cursiv::extends<SkipBase> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, SkipBase::WS>
  struct override<T> : terminal<some(cr<R"( \t\r\n)">)> {};
};
}  // namespace Q5

void run() {
  expect(cursiv::dump_expr<decltype(kw<"let"> + lit<"x"> + ikw<"or">)>(),
         R"("let" 'x' "or"~)", "tokens");
  expect(cursiv::dump_expr<decltype(cr<"a-zA-Z_">)>(), "[A-Z_a-z]", "class");
  expect(cursiv::dump_expr<decltype(cr<"À-ÿ">)>(), "\\p{\\xc0-\\xff}",
         "scalars");
  expect(cursiv::dump_expr<decltype(any)>(), ".", "every character");
  expect(cursiv::dump_expr<decltype(byte)>(), "\\C", "a byte");
  expect(cursiv::dump_expr<decltype(eof)>(), "!.", "the end");
  expect(cursiv::dump_expr<decltype(!byte)>(), "!\\C", "no byte");
  expect(cursiv::dump_expr<decltype(~cr<"a">)>(), "\\p{^a}",
         "complemented scalars");
  expect(cursiv::dump_expr<decltype(uc::L | uc::Nd)>(), "\\p{L|Nd}",
         "properties");
  expect(cursiv::dump_expr<decltype((kw<"a"> | kw<"b">)+kw<"c">)>(),
         R"(("a" | "b") "c")", "brackets");
  expect(cursiv::dump_expr<decltype(many(kw<"a">))>(), "\"a\"*", "many");
  // A raw expression view has no grammar, so a `kw<>` that names no class of
  // its own prints its guard as spelled -- the resolution belongs to the hub,
  // and `compiled<>` is where it shows.
  expect(cursiv::dump_expr<decltype(some(kw<"a">, kw<",">))>(),
         R"("a" ("," "a")*)", "separated");
  expect(cursiv::dump_expr<decltype(repeat<2, 5>(kw<"a">))>(), "\"a\"{2,5}",
         "bounded");
  expect(cursiv::dump_expr<decltype(!kw<"a"> + any)>(), "!\"a\" .",
         "lookahead");
  expect(cursiv::dump_expr<decltype(kw<"x"> & kw<"y">)>(), R"("x" & "y")",
         "unordered");
  expect(cursiv::dump_expr<decltype(assign<&ast::Node::name>(ref<Dg::ID>))>(),
         "name=ID", "assign");
  expect(cursiv::dump_expr<decltype(flag<&ast::Node::many>(kw<"many">))>(),
         "many?=\"many\"", "flag");
  expect(cursiv::dump_expr<decltype(create<ast::Node>(kw<"n"> + any))>(),
         "{Node} \"n\" .", "region");
  expect(cursiv::dump_expr<decltype(nest<ast::Node, &ast::Node::target>(
             kw<".">))>(),
         "{Node.target=} \".\"", "nest");

  expect(cursiv::dump_declared<Q4::G>(),
         "grammar G skip WS;\n"
         "entry data Root: $0 $1;\n"
         "terminal WS: [\\t-\\r ]+;\n"
         "terminal $0: [0-9]+;\n"
         "data $1: \"(\" \")\";",
         "inline rules named");

  expect(cursiv::dump_declared<Dg>(),
         "grammar Dg skip WS;\n"
         "entry Item returns Node: \"item\" name=ID;\n"
         "terminal WS: [\\t-\\r ]+;\n"
         "terminal ID: [a-z] [0-9A-Z_a-z]*;",
         "declared grammar");

  // The compiled view shows the resolved grammar: the override's body under
  // the same rule name.
  expect(cursiv::dump_compiled<D2>(),
         "grammar D2 skip WS;\n"
         "entry Item returns Node: \"item\" name=ID;\n"
         "terminal WS: [\\t-\\r ]+;\n"
         "terminal ID: [A-Z] [0-9A-Z_a-z]*;",
         "compiled grammar with override");

  // rule() and closure(): one rule, and a rule with everything it reaches.
  expect(cursiv::dump_rule<Dg::ID>(), "terminal ID: [a-z] [0-9A-Z_a-z]*;",
         "rule");
  expect(cursiv::dump_closure<Dg::Item>(),
         "Item returns Node: \"item\" name=ID;\n"
         "terminal ID: [a-z] [0-9A-Z_a-z]*;",
         "closure");

  // The positive lookahead.
  expect(cursiv::dump_expr<decltype(&lit<"a">)>(), "&'a'", "and");

  // Prefix and postfix bind apart: a directive moved across a repetition is a
  // visible diff.
  expect(cursiv::dump_expr<decltype(assign<&ast::Node::name>(
             many(ref<Dg::ID>)))>(),
         "name=ID*", "label of many");
  expect(cursiv::dump_expr<decltype(many(
             assign<&ast::Node::name>(ref<Dg::ID>)))>(),
         "(name=ID)*", "many of label");
  expect(cursiv::dump_expr<decltype(!many(kw<"a">))>(), "!\"a\"*",
         "not of many");
  expect(cursiv::dump_expr<decltype(many(!kw<"a"> + any))>(), "(!\"a\" .)*",
         "many of lookahead");

  // Three shapes that print as the source that builds them.
  expect(cursiv::dump_expr<decltype(implied(lit<";">, gap::line) |
                                    lit<"}">)>(),
         "implied(';', gap::line) | '}'", "implied in a wider choice");
  expect(cursiv::dump_expr<decltype(some(many(lit<"a">)))>(), "('a'*)+",
         "repetition of a repetition");
  expect(cursiv::dump_expr<detail::commit_free_t<decltype(
             many(commit(lit<"a">, lit<"b">)) + lit<"c">)>>(),
         "('a' 'b')* !'a' 'c'", "commit");
  expect(cursiv::dump_expr<decltype(mark<Open>(some(lit<"=">)) +
                                    same<Close>)>(),
         "mark<Open>('='+) same<Close>", "tagged runs");

  // An infix rule prints its declaration, not the synthesized chain -- and
  // the node it builds with the three members it writes, which nothing else
  // in the dump says.
  {
    const std::string g = cursiv::dump_declared<Q3::G>();
    expect(
        g.find(
            "infix Expr returns Bin(left, op, right) on Num: "
            "left(\"*\" | \"/\") >> left(\"+\" | \"-\") >> nonassoc(\"<\");") !=
                std::string::npos
            ? "y"
            : g.c_str(),
        "y", "infix header");
    // ...and the one-rule forms reach it too. They render a rule with no
    // grammar in hand, and an infix rule is the one that asks such a view for
    // more than a body.
    expect(cursiv::dump_rule<Q3::G::Expr>(),
           "infix Expr returns Bin(left, op, right) on Num: "
           "left(\"*\" | \"/\") >> left(\"+\" | \"-\") >> nonassoc(\"<\");",
           "infix rule alone");
    const std::string c = cursiv::dump_closure<Q3::G::Expr>();
    expect(c.find("terminal Num: [0-9]+;") != std::string::npos ? "y" : c.c_str(),
           "y",
           "infix closure reaches the primary");
  }

  // A skip() region prints as written, inline where it stands.
  {
    const std::string g = cursiv::dump_declared<Q2::G>();
    expect(g.find(R"("map" tight(ID ':' ID) "end")") !=
                   std::string::npos
               ? "y"
               : g.c_str(),
           "y", "skip region inline");
  }

  // A nest does not absorb its sequence: composite payloads are bracketed.
  expect(
      cursiv::dump_expr<decltype(lit<"a"> + nest<ast::Node, &ast::Node::target>(
                                                lit<"b"> + lit<"c">))>(),
      "'a' {Node.target=} ('b' 'c')", "nest bounded");

  // Two rules named ID in one closure: the second keeps its qualified name.
  expect(cursiv::dump_closure<R3>(),
         "R3 returns Node: ID dumpcol::ID;\n"
         "terminal ID: [a-z] [0-9A-Z_a-z]*;\n"
         "terminal dumpcol::ID: [0-9]+;",
         "name collision");

  // data header, a converter-typed terminal, a skipperless grammar head --
  // and a ConversionResult converter, whose rule returns the value.
  expect(cursiv::dump_declared<Q>(),
         "grammar Q;\n"
         "entry R returns Node: name=QN REAL;\n"
         "data QN: NUM (',' NUM)*;\n"
         "terminal REAL returns float64: [0-9]+ '.' [0-9]+;\n"
         "terminal NUM returns int32: [0-9]+;",
         "data and returns");

  // An overridden rule is one rule, under its own name, whether the view
  // meets it as a mint or by the name a body spells.
  expect(cursiv::dump_compiled<Q5::SelfDerived>(),
         "grammar SelfDerived;\n"
         "entry data List: ID (';' List)?;\n"
         "terminal ID: [a-z]+;",
         "overridden entry");
  expect(cursiv::dump_compiled<Q5::SkipDerived>(),
         "grammar SkipDerived skip WS;\n"
         "entry data Top: ID tight(WS ID);\n"
         "terminal WS: [\\t\\n\\r ]+;\n"
         "terminal ID: [a-z]+;",
         "overridden skipper");

  // Folding: stable, in the library, shared with the golden.
  expect(cursiv::dump_wrapped("Item: 'a' / 'b';\n", 92), "Item: 'a' / 'b';\n",
         "wrap short");
  // A doc's apostrophe is prose, not a quote.
  expect(cursiv::dump_wrapped(
             "Keyword: /** opens the machine's block */ 'state' | /** closes "
             "the block */ 'end' | /** names the start */ 'initialState' | "
             "'events';\n"),
         "Keyword: /** opens the machine's block */ 'state'\n"
         "    | /** closes the block */ 'end'\n"
         "    | /** names the start */ 'initialState'\n"
         "    | 'events';\n",
         "wrap past a doc's apostrophe");
}

}  // namespace dump

// The one `Grammar` over the generated metadata, asked through both engines:
// a documented word answers its prose from any spelling it matches, and only
// from those.
namespace default_grammar {

namespace {

void expect(bool ok, const char* engine, const char* what) {
  if (!ok) {
    std::printf("FAIL %s: %s\n", engine, what);
    ++cursiv::test::failures;
  }
}

void documented_words(const cursiv::Parser& parser, const char* engine) {
  const cursiv::Grammar& g = parser.getGrammar();
  expect(g.spellingDoc("events") == "Opens the events.", engine, "events");
  expect(g.spellingDoc("EvEnTs") == "Opens the events.", engine,
         "a folded word answers any case it matches");
  expect(g.spellingDoc("state") == "Names a state.", engine, "state");
  expect(g.spellingDoc("STATE").empty(), engine,
         "an exact word answers its own spelling alone");
  expect(g.spellingDoc("x").empty(), engine, "a name spells no word");
  expect(g.spellingDoc(std::string_view("a\0b", 3)) == "binary literal", engine,
         "a spelling holding a NUL keeps every byte");
  expect(parser.parse("EVENTS x").accepted, engine, "the folded word parses");
  const char bytes[] = {'E', 'V', 'E', 'N', 'T', 'S', ' ', 'x', '!'};
  const std::string_view view(bytes, 8);
  const cursiv::ParseResult bounded = parser.parse(view);
  expect(bounded.accepted && !bounded.cst().recovered(), engine,
         "a view needs no terminator");
  expect(bounded.cst().text().data() == bytes, engine,
         "the view's bytes are borrowed");
  expect(parser.parse({}).cst().text().empty(), engine,
         "an empty view needs no backing byte");
}

}  // namespace

void run() {
  documented_words(*tdual::generated::Lr::create_parser(), "LR");
  documented_words(*tdual::generated::Ll::create_parser(), "LL");
}

}  // namespace default_grammar

// Prose is not a name: nine words at 8 KiB each pass the 16 bits a table's
// names are indexed in, and every one still answers its own.
namespace wide_docs {

void check(const cursiv::Parser& parser) {
  const cursiv::Grammar& g = parser.getGrammar();
  for (unsigned k = 0; k < 9; ++k) {
    const std::string word = "k" + std::to_string(k);
    const auto doc = g.spellingDoc(word);
    CHECK(doc.size() == 8192);
    CHECK(doc.substr(0, 8) == "01234567");
  }
  CHECK(parser.parse("k0 k4 k8").accepted);
}

void run() {
  check(*tdocs::generated::G::create_parser());
  check(*tdocs::generated::Ll::create_parser());
}

}  // namespace wide_docs

// The view's length ends the document. An embedded NUL is content, and no
// terminator is required after the view.
namespace compiler {

namespace {

const cursiv::Parser& sentblob() {
  static const std::unique_ptr<cursiv::Parser> p =
      tsent::generated::Blob::create_parser();
  return *p;
}
const cursiv::Parser& sentword() {
  static const std::unique_ptr<cursiv::Parser> p =
      tsent::generated::Word::create_parser();
  return *p;
}
const cursiv::Parser& sentfree() {
  static const std::unique_ptr<cursiv::Parser> p =
      tsent::generated::Free::create_parser();
  return *p;
}

// A document with a NUL embedded, counted among its bytes.
[[nodiscard]] std::string doc(std::string_view a, std::string_view b) {
  std::string s(a);
  s.push_back('\0');
  s.append(b);
  return s;
}

[[nodiscard]] bool parses(const cursiv::Parser& engine, std::string_view text) {
  const cursiv::ParseResult r = engine.parse(text);
  return r.accepted && !r.cst().recovered();
}

// The text of the first leaf, which is the token every check below is about.
[[nodiscard]] std::string first_leaf(const cursiv::Parser& engine,
                                     std::string_view text) {
  const cursiv::ParseResult r = engine.parse(text);
  if (!r.accepted) return "<refused>";
  const cursiv::Cst& cst = r.cst();
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    if (cst[i].subtreeSize == 1) return std::string(cst.text(i));
  return "<no leaf>";
}

void a_bounded_view_is_borrowed_without_a_terminator() {
  const char bytes[] = {'a', 'b', 'c', ';', '!'};
  const std::string_view text(bytes, 4);
  cursiv::Cst cst(text);
  CHECK(cst.text().data() == bytes && cst.text() == text);
  CHECK(parses(sentblob(), text));
  CHECK(parses(sentword(), text));
  CHECK(first_leaf(sentword(), text) == "abc");
  // The semicolon just outside this view must not finish the document.
  CHECK(!parses(sentword(), text.substr(0, 3)));
  auto exact = std::make_unique<char[]>(text.size());
  std::copy(text.begin(), text.end(), exact.get());
  cst.reset(std::string_view(exact.get(), text.size()));
  CHECK(cst.text().data() == exact.get() && cst.text() == text);
  CHECK(parses(sentword(), cst.text()));
  cst.reset(std::string_view{});
  CHECK(cst.text().empty());
  CHECK(cst.text().data() != nullptr);
}

// A negated class admits every other byte, including NUL. The run crosses
// NULs until the semicolon or the view's end.

void a_class_that_admits_nul_crosses_it() {
  const std::string d = doc("ab", "cd;");
  // Accepted, and the token is the five bytes before the `;`, the embedded NUL
  // among them. Read the byte as an end and the run would stop at 2 and the
  // document be refused on a `\0` the grammar plainly allows.
  CHECK(parses(sentblob(), d));
  CHECK(first_leaf(sentblob(), d) == doc("ab", "cd"));
  CHECK(first_leaf(sentblob(), d).size() == 5);
  // A token that is nothing but a NUL: one byte of content.
  const std::string only = doc("", ";");
  CHECK(only.size() == 2);
  CHECK(parses(sentblob(), only));
  CHECK(first_leaf(sentblob(), only).size() == 1);
  CHECK(first_leaf(sentblob(), only)[0] == '\0');

  // Several NULs in one run.
  std::string many = doc("a", "b");
  many = doc(many, "c;");  // "a\0b\0c;"
  CHECK(many.size() == 6);
  CHECK(parses(sentblob(), many));
  CHECK(first_leaf(sentblob(), many).size() == 5);

  // Without the closing `;` the run reaches
  // the end of the document and the rule refuses -- it does not read on.
  CHECK(!parses(sentblob(), doc("ab", "cd")));
}

void a_class_that_refuses_nul_stops_on_it() {
  // The run ends on the embedded byte -- exactly where the bounded loop ended
  // it -- so the `;` is not where the rule wants it and the document refuses.
  CHECK(!parses(sentword(), doc("ab", "cd;")));
  // ...and the same bytes without the NUL are accepted, which is what makes
  // the refusal above about the byte and not about the shape.
  CHECK(parses(sentword(), "abcd;"));
  CHECK(first_leaf(sentword(), "abcd;") == "abcd");
  // A run that reaches the view's end is a complete token.
  CHECK(!parses(sentword(), "abcd"));  // the run ends, the `;` is missing
  // Trivia refuses it too -- `s` is space/tab/newline and holds no NUL, so the
  // skip run stops on the byte instead of crossing it.
  CHECK(parses(sentword(), "  abcd ;"));
  CHECK(!parses(sentword(), doc("  ", "abcd;")));
}

void any_takes_the_byte_and_stops_at_the_end() {
  // `any` is a width, and a NUL is one byte wide: three of them cross it.
  CHECK(parses(sentfree(), doc("a", "b!")));
  CHECK(first_leaf(sentfree(), doc("a", "b!")).size() == 3);
  // ...and `any` still fails where there is nothing left, which is what keeps
  // `eof` meaning "no bytes left" rather than "a NUL stands here".
  CHECK(!parses(sentfree(), "ab!"));
  CHECK(!parses(sentfree(), doc("a", "b")));
}

// A core lane owes its own grammar. Naming an example language would compile
// the longest one of the tree a second time, and what is under test is the
// engine's answer at the byte, never that language's.

// A quoted run whose class admits the byte: json's string, its departure 1.
void a_quoted_run_takes_the_byte() {
  // The token is the five bytes of `"x\0y"`, NUL included -- not the two the
  // string would have if the byte ended it.
  const std::string d = doc("\"x", "y\"");
  CHECK(d.size() == 5);
  CHECK(parses(sentword(), d));
  CHECK(first_leaf(sentword(), d) == d);
  // ...and the byte outside the quotes still starts nothing.
  CHECK(!parses(sentword(), doc("\"xy\"", "")));
}

// A run up to a literal closer, and one up to end of line: the two shapes a
// block and a line comment take. The closer is searched for, so a byte inside
// the run does not stop the search.
void a_run_up_to_a_closer_takes_the_byte() {
  CHECK(parses(sentword(), doc("/* a", "b */ name;")));
  // The line comment's closer is zero width, which is the other arm of the
  // same run.
  CHECK(parses(sentword(), doc("// a", "b\nname;")));
}

}  // namespace

void run() {
  a_bounded_view_is_borrowed_without_a_terminator();
  a_class_that_admits_nul_crosses_it();
  a_class_that_refuses_nul_stops_on_it();
  any_takes_the_byte_and_stops_at_the_end();
  a_quoted_run_takes_the_byte();
  a_run_up_to_a_closer_takes_the_byte();
}

}  // namespace compiler

// A class is a set of scalars, whatever it is spelled with.
namespace spec_scalar {

namespace {

using namespace cursiv;

bool holds(const LexCodePoints& c, char32_t cp) {
  for (const auto& r : c.ranges)
    if (r.lo <= cp && cp <= r.hi) return true;
  return false;
}

template <std::uint64_t M, bool N, str S>
LexCodePoints points(CpSet<M, N, S>) {
  return LexCodePoints::of(M, N, S.view());
}

bool same_below(const LexCodePoints& a, const LexCodePoints& b,
                char32_t end) {
  for (char32_t cp = 0; cp < end; ++cp)
    if (holds(a, cp) != holds(b, cp)) return false;
  return true;
}

void a_spelling_reads_as_scalars() {
  const auto wide = LexCodePoints::of(0, true, R"('\\\r\n\u2028)");
  CHECK(!holds(wide, U'\\') && !holds(wide, U'\r') && holds(wide, U'r') &&
        !holds(wide, 0x2028));
  CHECK(wide.strays());
  const auto wideSign = LexCodePoints::of(0, false, R"(\u00e9+\\-)");
  CHECK(holds(wideSign, U'\\') && holds(wideSign, U'-') &&
        holds(wideSign, 0xE9));
  CHECK(!wideSign.strays());
}

// A union reads as the spelling that holds them all.
void a_union_reads_as_one_spelling() {
  CHECK(same_below(points(cr<R"(+\\-)"> | cr<R"(é)">), points(cr<R"(é+\\-)">),
                   0x100));
  CHECK(same_below(points(cr<R"(\\-^a)"> | uc::Nd),
                   points(cr<R"(\\-^aé)"> | uc::Nd), 0xE9));
  const auto outside = points(~cr<R"(-\\^)">);
  CHECK(!holds(outside, U'\\') && !holds(outside, U'-') &&
        !holds(outside, U'^') && holds(outside, U'a'));
}

// Whether a class of code points holds exactly `in` among `probe`; a class
// the generator refuses holds nothing.
template <std::uint64_t M, bool N, str S>
bool holds_exactly(CpSet<M, N, S> set, std::u32string_view probe,
                   std::u32string_view in) {
  try {
    const LexCodePoints c = points(set);
    for (const char32_t cp : probe)
      if (holds(c, cp) != (in.find(cp) != std::u32string_view::npos))
        return false;
    return true;
  } catch (const std::invalid_argument&) {
    return false;
  }
}

// Each side of a union reads as it did alone: a literal `-` at the seam opens
// no range, and a `\0` before a digit stays NUL.
void a_union_reads_each_side_as_alone() {
  CHECK(holds_exactly(cr<R"(é\0)"> | cr<"1ü">,
                      std::u32string_view(U"\u00e9\0\u00011\u00fc", 5),
                      std::u32string_view(U"\u00e9\0\u00fc1", 4)));
  CHECK(holds_exactly(cr<R"(é-)"> | cr<R"(ü)">, U"\u00e9-\u00ea\u00fc",
                      U"\u00e9-\u00fc"));
  CHECK(holds_exactly(cr<R"(é)"> | cr<R"(-ü)">, U"\u00e9-\u00ea\u00fc",
                      U"\u00e9-\u00fc"));
  CHECK(holds_exactly(cr<R"(é-)"> | cr<R"(-ü)">, U"\u00e9-\u00ea\u00fc",
                      U"\u00e9-\u00fc"));
  CHECK(holds_exactly(cr<R"(aé-)"> | cr<"z">, U"a\u00e9-mz", U"a\u00e9-z"));
  CHECK(holds_exactly(cr<R"(é\\)"> | cr<R"(x41ü)">, U"\u00e9\\x41A\u00fc",
                      U"\u00e9\\x41\u00fc"));
  using Joined = decltype(cr<R"(é-)"> | cr<R"(ü)">);
  CHECK(detail::word_class<Joined>::has(U'-'));
  CHECK(!detail::word_class<Joined>::has(U'\u00ea'));
  // A dash after a range already read alone: the seam leaves it as spelled.
  CHECK(holds_exactly(cr<R"(à-é)"> | cr<R"(-ü)">,
                      U"\u00e0\u00e5\u00e9-\u00ea\u00fc",
                      U"\u00e0\u00e5\u00e9-\u00fc"));
  CHECK(holds_exactly(cr<R"(à-é-)"> | cr<R"(ü)">,
                      U"\u00e0\u00e5\u00e9-\u00ea\u00fc",
                      U"\u00e0\u00e5\u00e9-\u00fc"));
}

// `'\'` escapes its closing quote and stays open; `'\\'` closes.
void a_wide_string_body_excludes_the_backslash(const Parser& parser) {
  const ParseResult open = parser.parse("'\\'");
  CHECK(!(open.accepted && open.diagnostics.empty()));
  const ParseResult closed = parser.parse("'\\\\'");
  CHECK(closed.accepted && closed.diagnostics.empty());
}

}  // namespace

void run() {
  a_spelling_reads_as_scalars();
  a_union_reads_as_one_spelling();
  a_union_reads_each_side_as_alone();
  a_wide_string_body_excludes_the_backslash(
      *tdual::generated::Lr::create_parser());
  a_wide_string_body_excludes_the_backslash(
      *tdual::generated::Ll::create_parser());
}

}  // namespace spec_scalar

// A dialect reads its base's documents as the base does, and what only it
// spells into the base's tree.
namespace derived {

namespace {

const tderive::ast::Doc* doc_of(const cursiv::ParseResult& r) {
  return r.accepted && !r.cst().recovered()
             ? r.value_as<tderive::ast::Doc>()
             : nullptr;
}

void a_dialect_reads_what_its_base_reads() {
  const auto base = tderive::generated::Base::create_parser();
  const auto dialect = tderive::generated::Dialect::create_parser();
  for (const cursiv::Parser* p : {base.get(), dialect.get()}) {
    const cursiv::ParseResult r = p->parse("a = 1\nb = 22\n");
    const tderive::ast::Doc* doc = doc_of(r);
    CHECK(doc != nullptr && doc->items.size() == 2);
    CHECK(doc != nullptr && doc->items[1]->name == "b" &&
          doc->items[1]->value == 22);
  }
}

void only_the_dialect_reads_its_own_spellings() {
  constexpr std::string_view text = "a = 0x1f # hex\nb = 2\n";
  const auto base = tderive::generated::Base::create_parser();
  CHECK(doc_of(base->parse(text)) == nullptr);
  const auto dialect = tderive::generated::Dialect::create_parser();
  const cursiv::ParseResult r = dialect->parse(text);
  const tderive::ast::Doc* doc = doc_of(r);
  CHECK(doc != nullptr && doc->items.size() == 2);
  CHECK(doc != nullptr && doc->items[0]->value == 31);
}

// A dialect adds an alternative to a node rule of its base: the new one builds
// a class derived from the rule's, and the base's own alternative stays.
void a_dialect_adds_an_alternative_to_a_node_rule() {
  constexpr std::string_view text = "*a = 1\nb = 22\n";
  const auto base = tderive::generated::Base::create_parser();
  CHECK(doc_of(base->parse(text)) == nullptr);
  const auto dialect = tderive::generated::RuleDialect::create_parser();
  const cursiv::ParseResult r = dialect->parse(text);
  const tderive::ast::Doc* doc = doc_of(r);
  CHECK(doc != nullptr && doc->items.size() == 2);
  CHECK(doc != nullptr &&
        doc->items[0]->type == &cursiv::ast_type_tag<tderive::ast::Starred> &&
        doc->items[0]->name == "a" && doc->items[0]->value == 1);
  CHECK(doc != nullptr &&
        doc->items[1]->type == &cursiv::ast_type_tag<tderive::ast::Item> &&
        doc->items[1]->name == "b" && doc->items[1]->value == 22);
}

// A refusal in a rule asks the terminal the grammar in force spells, not the
// one its base declared.
void a_refusal_asks_the_dialects_terminal() {
  const auto base = tderive::generated::Base::create_parser();
  const auto dialect = tderive::generated::Dialect::create_parser();
  const auto reads = [](const cursiv::Parser& p, std::string_view text) {
    return doc_of(p.parse(text)) != nullptr;
  };
  CHECK(!reads(*base, "let = 1\n") && reads(*base, "var = 1\n"));
  CHECK(reads(*dialect, "let = 1\n") && !reads(*dialect, "var = 1\n"));
  CHECK(reads(*dialect, "variant = 1\n") && reads(*dialect, "letter = 1\n"));
}

// A literal's guard asks the terminal the grammar in force spells too: `in`
// runs into a letter under the base, and into a digit as well under the
// dialect.
void a_literal_guard_asks_the_dialects_terminal() {
  const auto clean = [](const cursiv::Parser& p, std::string_view text) {
    const cursiv::ParseResult r = p.parse(text);
    return r.accepted && r.diagnostics.empty();
  };
  const auto base = tguard::generated::Base::create_parser();
  const auto dialect = tguard::generated::Dialect::create_parser();
  CHECK(clean(*base, "in1") && clean(*base, "in 1") && !clean(*base, "ina"));
  CHECK(!clean(*dialect, "in1") && clean(*dialect, "in 1") &&
        !clean(*dialect, "ina"));
}

// The text a data rule joins reads each part through the converter the
// dialect restated, in both engines.
void a_joined_part_runs_the_dialects_converter() {
  const auto paths = [](const cursiv::Parser& p, std::string_view text) {
    const cursiv::ParseResult r = p.parse(text);
    const auto* doc = static_cast<const ttext::ast::Doc*>(r.value);
    return r.accepted && r.diagnostics.empty() && doc != nullptr &&
                   doc->paths.size() == 1
               ? std::string(doc->paths[0])
               : std::string("<none>");
  };
  CHECK(paths(*ttext::generated::Dialect::create_parser(), "@@ab.@@cd") ==
        "ab.cd");
  CHECK(paths(*ttext::generated::DialectLl::create_parser(), "@@ab.@@cd") ==
        "ab.cd");
}

}  // namespace

void run() {
  a_dialect_reads_what_its_base_reads();
  only_the_dialect_reads_its_own_spellings();
  a_dialect_adds_an_alternative_to_a_node_rule();
  a_refusal_asks_the_dialects_terminal();
  a_literal_guard_asks_the_dialects_terminal();
  a_joined_part_runs_the_dialects_converter();
}

}  // namespace derived

int main() {
  expr::run();
  grammar_source::run();
  grammar_utils::run();
  grammar_spec::run();
  dump::run();
  default_grammar::run();
  wide_docs::run();
  compiler::run();
  spec_scalar::run();
  derived::run();
  return cursiv::test::report("grammar");
}
