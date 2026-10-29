#pragma once

// A word refused in a rule is refused there alone; elsewhere a keyword is a
// name wherever the state reads names alone.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/ConditionRefsGrammar.hpp"

namespace trefuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view alias;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct Words : cursiv::GrammarSpec {
  static constexpr auto word_boundary = uc::XID_Continue;
  struct WS : terminal<some(s)> {};
  struct ID : terminal<uc::XID_Start + many(uc::XID_Continue)> {};
};

// An alias after AS may be a join keyword, a bare one may not: `t AS LEFT`
// is an alias, `t LEFT JOIN u` a join.
namespace alias {
struct G : Words {
  struct BareAlias : rule<!(ikw<"AS"> | ikw<"LEFT">) + ref<ID>> {};
  struct FullAlias : rule<!ikw<"AS"> + ref<ID>> {};
  struct Item
      : rule<create<ast::Item> + assign<&ast::Item::name>(ref<ID>) +
             option((ikw<"AS"> + assign<&ast::Item::alias>(ref<FullAlias>)) |
                    assign<&ast::Item::alias>(ref<BareAlias>)) +
             many(ikw<"LEFT"> + ikw<"JOIN"> + ref<ID>)> {};
  struct Doc : rule<create<ast::Doc> + append<&ast::Doc::items>(ref<Item>) +
                    many(lit<";"> + append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace alias

// `int` opens a declaration; where the state reads a name alone, `int` and
// `if` are names, and what the language refuses is its validator's to say.
namespace soft {
struct G : Words {
  struct Item : rule<create<ast::Item> + kw<"int"> +
                     assign<&ast::Item::name>(ref<ID>) + lit<"="> +
                     assign<&ast::Item::alias>(ref<ID>) + lit<";">> {};
  struct Doc
      : rule<create<ast::Doc> + many(append<&ast::Doc::items>(ref<Item>))> {};
};
}  // namespace soft

}  // namespace trefuse

// A condition that reads past its token, as far as the text a completion
// replaces: the text the prefix was read on cannot decide it.
namespace tcaretlook {

namespace ast {
struct Part : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Part* part = nullptr;
};
}  // namespace ast

struct G : trefuse::Words {
  struct Shifted
      : rule<(lit<"+"> + ((&(lit<"a"> + !lit<"by">) + lit<"a"> + lit<"b"> +
                           lit<"x">) |
                          (&(lit<"a"> + !lit<"bx">) + lit<"a"> + lit<"b"> +
                           lit<"y">))) |
             (lit<"-"> + &(lit<"a"> + !lit<"by">) + lit<"a"> + lit<"b"> +
              (lit<"x"> | lit<"y">))> {};
  // The same guard, decided inside a record the caret stands behind, in a
  // table with no conflict: the replay may step over the record whole.
  struct X : rule<create<ast::Part>(&(lit<"a"> + !lit<"bcy">) + lit<"a"> +
                                    lit<"b">)> {};
  struct Root : rule<create<ast::Doc>(
                    ref<Shifted> | (lit<"/"> + assign<&ast::Doc::part>(ref<X>) +
                                    lit<"c"> + (lit<"x"> | lit<"y">)))> {};
};

}  // namespace tcaretlook

// A paragraph reads on over `/` unless its next word opens a numbered one,
// and `/` also parts two paragraphs: both readings of a `/` stand as far as
// the `;` that closes its group, and the guard reads digits to any length.
namespace tlines {

struct G : trefuse::Words {
  struct NUM : terminal<some(cr<"0-9">) + lit<".">> {};
  struct Para : rule<some(ref<ID>) + many(lit<"/"> +
                                          !(some(cr<"0-9">) + lit<".">) +
                                          some(ref<ID>))> {};
  struct Item : rule<option(ref<NUM>) + ref<Para>> {};
  struct Group : rule<ref<Item> + many(lit<"/"> + ref<Item>)> {};
  struct Root : rule<create<tcaretlook::ast::Doc>(
                    ref<Group> + many(lit<";"> + ref<Group>))> {};
};

}  // namespace tlines

namespace trefuse {

// `!e` and `&e` are the two lookaheads on the token in hand: it is not, it is,
// all that `e` reads. A word carries its boundary (`iffy` is not `if`), a
// literal its guard, a terminal its body, and a bound decides on the scalar
// behind the spelling, however many bytes it takes.
namespace head {
struct G : Words {
  struct BRACED : terminal<lit<"{"> + many(!cr<"}"> + any) + lit<"}">> {};
  struct WORD : terminal<some(cr<"a-z0-9.">)> {};
  struct STR : terminal<lit<"'"> + many(!cr<"'"> + any) + lit<"'">> {};
  struct DOTNUM : terminal<some(cr<"0-9">) + lit<".">> {};
  struct DIGIT : terminal<cr<"0-9">> {};
  struct MINUS : terminal<lit<"-", !ref<DIGIT>>> {};
  struct Go : rule<create<ast::Item> + kw<"go"> + !kw<"if"> +
                   assign<&ast::Item::name>(lit<"iffy"> | ref<ID>)> {};
  struct Put : rule<create<ast::Item> + kw<"put"> + !lit<"{"> +
                    assign<&ast::Item::name>(ref<BRACED> | ref<ID>)> {};
  struct Ask : rule<create<ast::Item> + kw<"ask"> +
                    option(&kw<"if"> + kw<"if">) +
                    assign<&ast::Item::name>(ref<ID>)> {};
  struct Num : rule<create<ast::Item> + kw<"num"> +
                    !(some(cr<"0-9">) + lit<".">) +
                    assign<&ast::Item::name>(ref<WORD>)> {};
  // Digits and nothing behind them, as one token's text: the guard at the end
  // is the boundary, read past the token.
  struct Only : rule<create<ast::Item> + kw<"only"> +
                     !ref<terminal<some(cr<"0-9">) + !cr<"a-z0-9.">>> +
                     assign<&ast::Item::name>(ref<WORD>) +
                     option(ref<WORD>)> {};
  struct Say : rule<create<ast::Item> + kw<"say"> + !ref<STR> +
                    assign<&ast::Item::name>(ref<WORD> | ref<STR>)> {};
  struct Head : rule<create<ast::Item> + kw<"head"> + !ref<DOTNUM> +
                     assign<&ast::Item::name>(ref<WORD>)> {};
  struct Neg : rule<create<ast::Item> + kw<"neg"> + ref<MINUS> +
                    assign<&ast::Item::name>(ref<WORD>)> {};
  // A predicate reads its own whole token, even when the next parse splits it.
  struct LA : terminal<lit<"a">> {};
  struct LB : terminal<lit<"b">> {};
  struct Pair : rule<create<ast::Item> + kw<"pair"> + !lit<"ab"> +
                     assign<&ast::Item::name>(ref<LA>) + ref<LB>> {};
  // A choice asks what its words ask alone, boundary decoded.
  struct Both : rule<create<ast::Item> + kw<"both"> +
                     !(kw<"if"> | lit<"zz">) +
                     assign<&ast::Item::name>(ref<ID>)> {};
  struct Also : rule<create<ast::Item> + kw<"also"> +
                     &(kw<"if"> | lit<"zz">) +
                     assign<&ast::Item::name>(ref<ID>)> {};
  // A literal guarded by a word refuses what the lexer's guard refuses.
  struct Dash : rule<create<ast::Item> + kw<"dash"> +
                     option(!lit<"-", !lit<"--">> + lit<"-">) +
                     assign<&ast::Item::name>(ref<WORD>)> {};
  // The end of input as an operand: asserted, refused, or one arm of a choice.
  struct Ends : rule<create<ast::Item> + kw<"ends"> +
                     assign<&ast::Item::name>(ref<WORD>) + &eof> {};
  struct More : rule<create<ast::Item> + kw<"more"> + !eof +
                     assign<&ast::Item::name>(ref<WORD>)> {};
  struct Next : rule<create<ast::Item> + kw<"next"> +
                     assign<&ast::Item::name>(ref<WORD>) +
                     option(!(lit<";"> | eof) + ref<WORD>)> {};
  static constexpr auto Item = ref<Go> | ref<Put> | ref<Ask> | ref<Num> |
                               ref<Only> | ref<Say> | ref<Head> | ref<Neg> |
                               ref<Pair> | ref<Both> | ref<Also> | ref<Dash> |
                               ref<Ends> | ref<More> | ref<Next>;
  struct Doc : rule<create<ast::Doc> + append<&ast::Doc::items>(Item) +
                    many(lit<";"> + append<&ast::Doc::items>(Item))> {};
  // The grammars above, each behind its own opening keyword.
  struct Case : rule<(kw<"alias"> + ref<alias::G::Doc>) |
                     (kw<"soft"> + ref<soft::G::Doc>) |
                     (kw<"caret"> + ref<tcaretlook::G::Root>) |
                     (kw<"lines"> + ref<tlines::G::Root>) |
                     ref<tcondref::G::Root> | ref<Doc>> {};
  using entry = Case;
  using ignored = WS;
};
}  // namespace head

}  // namespace trefuse
