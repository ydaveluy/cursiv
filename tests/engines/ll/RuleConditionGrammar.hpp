// Conditions written in a rule: refusals of one token joined, a sequence read
// as the rule reads it, the skipper playing between its elements, and
// conditions nested in the alternatives they guard.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrcond {
struct Item : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> names;
  string_view tag;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CMT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct DASHES : terminal<some(lit<"-">)> {};
  struct NAME : terminal<cr<"a-zA-Z"> + many(cr<"a-zA-Z0-9">)> {};

  static constexpr auto Name = append<&Item::names>(ref<NAME>);
  static constexpr auto Names = some(Name);
  static constexpr auto Call = Name + assign<&Item::tag>(lit<"(">);
  static constexpr auto Bind = Name + assign<&Item::tag>(lit<"=">);
  static constexpr auto A = kw<"a">;
  static constexpr auto B = kw<"b">;
  static constexpr auto C = kw<"c">;
  static constexpr auto E = kw<"e">;
  static constexpr auto X = kw<"x">;

  struct Stmt
      : rule<create<Item>(
            (kw<"r1"> + !A + !B + !C + Name) |
            (kw<"u1"> + !(A | B | C) + Name) |
            (kw<"r1and"> + (&A | &B | &C) + Name) |
            (kw<"u1and"> + &(A | B | C) + Name) |
            (kw<"r1i"> + !ikw<"a"> + !ikw<"b"> + Name) |
            (kw<"u1i"> + !(ikw<"a"> | ikw<"b">) + Name) |
            (kw<"r1opt"> + !A + !B + option(Name)) |
            (kw<"r2"> + !(A + B) + Names) |
            (kw<"r2and"> + &(A + B) + Names) |
            (kw<"r2i"> + !(ikw<"a"> + ikw<"b">) + Names) |
            (kw<"r2opt"> + !(A + B) + many(Name)) |
            (kw<"r2rep"> + !(some(B) + A) + Names) |
            (kw<"r2dash"> +
             skip(ignore<DASHES>, hide<>,
                  lit<"["> + !(A + B) + Names + lit<"]">)) |
            (kw<"r3"> + !A + !B + ((!C + Call) | (!E + Bind) | Name)) |
            (kw<"r3and"> + (&A | &C | &E | &X) +
             ((!C + Call) | (&E + Bind) | (!A + Name))))> {};
  // A terminal's text, named or inline, read as one element.
  struct WORD : terminal<some(cr<"a-zA-Z0-9.">)> {};
  struct ENUM_LEAD : terminal<some(cr<"0-9">) + lit<".">> {};
  static constexpr auto DotNum = ref<terminal<some(cr<"0-9">) + lit<".">>>;
  static constexpr auto Words = some(append<&Item::names>(ref<WORD>));
  // A data rule is the phrase its body reads, the skipper between its
  // elements, a `skip()` in it naming its own.
  struct QName : rule<ref<NAME> + many(lit<"."> + ref<NAME>)> {};
  struct Dotted : rule<ref<NAME> + lit<"."> + ref<QName>> {};
  struct IsNot : rule<kw<"is"> + kw<"not">> {};
  struct Tight : rule<tight(lit<"is"> + lit<"not">)> {};
  struct DOTS : terminal<some(lit<".">)> {};
  struct Dotty
      : rule<kw<"a"> + skip(ignore<DOTS>, hide<>, kw<"b"> + kw<"c">)> {};
  struct Text
      : rule<create<Item>(
            (kw<"q1"> + !ref<QName> + Words) |
            (kw<"q2"> + !ref<Dotted> + Words) |
            (kw<"q2and"> + &ref<Dotted> + Words) |
            (kw<"i1"> + !ref<IsNot> + Words) |
            (kw<"i2"> + !(ref<IsNot> | kw<"zz">)+Words) |
            (kw<"t1"> + !ref<Tight> + Words) |
            (kw<"t2"> + !ref<Dotty> + Words) | (kw<"d1"> + !DotNum + Words) |
            (kw<"d1and"> + &DotNum + Words) |
            (kw<"d2"> + !(DotNum + kw<"x">)+Words) |
            (kw<"d3"> + !ref<terminal<ikw<"ab"> + lit<".">>> + Words) |
            (kw<"d4"> + !ref<terminal<some(cr<"0-9">) + !cr<"a-zA-Z0-9.">>> +
             Words) |
            (kw<"d5"> + !(DotNum | kw<"zz">)+Words) |
            (kw<"n1"> + !ref<ENUM_LEAD> + Words) |
            (kw<"n1and"> + &ref<ENUM_LEAD> + Words) |
            (kw<"n2"> + !(ref<ENUM_LEAD> + kw<"x">)+Words) |
            (kw<"n5"> + !(ref<ENUM_LEAD> | kw<"zz">)+Words))> {};
  struct Root : rule<create<Doc>(
                    many(append<&Doc::items>(ref<Stmt> | ref<Text>) +
                         lit<";">) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = CMT;
};
struct Ll : Base {};
struct Lr : Base {};

// Nested comments between the tokens a rule condition reads.
struct Nested : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct CMT
      : terminal<lit<"{-"> + many(ref<CMT> | (!lit<"{-"> + !lit<"-}"> + any)) +
                 lit<"-}">> {};
  struct NAME : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct Stmt : rule<create<Item>(!(kw<"a"> + kw<"b">)+some(
                    append<&Item::names>(ref<NAME>)))> {};
  struct Root : rule<create<Doc>(
                    many(append<&Doc::items>(ref<Stmt>) + lit<";">) + eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = CMT;
};
}  // namespace tllrcond

namespace tllterm {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  string_view left;
  string_view right;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct NUM : terminal<some(d)> {};
  struct DOTTED : terminal<some(cr<"a-z">) + lit<"."> + some(cr<"a-z">)> {};
  struct NOTE : terminal<lit<"#"> + many(cr<"^\n;">)> {};
  struct RBRACE : terminal<lit<"}">> {};
  struct LET : terminal<lit<"let">> {};
  struct No : rule<create<Item>(kw<"no"> + !ref<NUM> +
                                assign<&Item::text>(ref<NAME> | ref<NUM>))> {};
  struct Is
      : rule<create<Item>(kw<"is"> + &ref<NAME> +
                          assign<&Item::text>(ref<NAME> | kw<"if"> |
                                              ref<DOTTED>))> {};
  struct Bare
      : rule<create<Item>(kw<"bare"> + !ref<NOTE> +
                          assign<&Item::text>(ref<NAME> | ref<NOTE>))> {};
  // `let` is the token LET reads; `letter` is a longer token.
  struct Tie : rule<create<Item>(kw<"tie"> + !ref<LET> +
                                 assign<&Item::text>(ref<NAME> | ref<LET>))> {};
  struct Pick : rule<create<Item>(kw<"pick"> + &ref<NAME> +
                                  assign<&Item::text>(ref<LET> | ref<NAME>))> {};
  // The `;` a closing brace stands for: the token the condition asks of is
  // one the state before the `;` does not read.
  struct Stmt : rule<ref<NAME> + implied(lit<";">, &ref<RBRACE>)> {};
  struct Group : rule<create<Item>(kw<"group"> + lit<"{"> + many(ref<Stmt>) +
                                   ref<RBRACE>)> {};
  // A phrase that opens on its gap: a caret past its last token defers it.
  struct EQ : terminal<lit<"=">> {};
  struct Line : rule<create<Item>(kw<"line"> +
                                  &(gap::has(lit<"\n">) + ref<NAME> + ref<EQ> +
                                    ref<NAME>)+assign<&Item::text>(ref<NAME>) +
                                  ref<EQ> + ref<NAME>)> {};
  // The member a name fills turns on its first letter, which a terminal tests.
  struct ANAME : terminal<&lit<"a"> + ref<NAME>> {};
  struct NNAME : terminal<&lit<"n"> + ref<NAME>> {};
  struct Pair
      : rule<create<Item>(
            kw<"pair"> + ((&ref<ANAME> + assign<&Item::left>(ref<NAME>)) |
                          (&ref<NNAME> + assign<&Item::right>(ref<NAME>))))> {};
  struct Root : rule<create<Doc>(
                    many(append<&Doc::items>(ref<No> | ref<Is> | ref<Bare> |
                                             ref<Tie> | ref<Pick> | ref<Group> |
                                             ref<Line> | ref<Pair>) +
                         lit<";">))> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tllterm
