#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/LexUnicodeGrammars.hpp"
// A class reads one character: a well-formed scalar whole, or the byte where
// none starts. Each reading is a terminal behind a tag, so a document names
// the one it asks.

namespace tchars {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct One : terminal<any> {};
  struct Two : terminal<any + any> {};
  struct QuotedAny : terminal<lit<"'"> + any + lit<"'">> {};
  struct QuotedNot : terminal<lit<"'"> + cr<"^'"> + lit<"'">> {};
  struct StringNot : terminal<lit<"\""> + many(cr<"^\"">) + lit<"\"">> {};
  struct StringAny : terminal<lit<"\""> + many(!lit<"\""> + any) + lit<"\"">> {
  };
  struct Paren : terminal<any + lit<"(">> {};
  struct Raw : terminal<byte> {};
  struct Twice : terminal<some(~~cr<"a">)> {};
  struct Whole : terminal<some(w | W)> {};
  struct Accented : terminal<some(cr<"a-zé">)> {};
  struct NotWord : terminal<W> {};
  struct Strict : terminal<some(cr<R"(\u{0}-\u{10FFFF})">)> {};
  struct Acute : terminal<cr<"é">> {};
  struct AcuteHex : terminal<cr<R"(\xE9)">> {};
  struct NotAcute : terminal<~cr<"é">> {};
  struct Letter : terminal<uc::L> {};
  struct NotLetter : terminal<~uc::L> {};
  struct LetterA : terminal<cr<"a">> {};
  struct NotA : terminal<~cr<"a">> {};
  struct NotNotA : terminal<~~cr<"a">> {};
  struct Either : terminal<~cr<"a"> | uc::L> {};
  struct NonDigit : terminal<cr<R"(\D)">> {};
  struct NotNonDigit : terminal<~cr<R"(\D)">> {};
  struct RunNotA : terminal<some(~cr<"a">)> {};
  struct RunNotAcute : terminal<some(~cr<"é">)> {};
  struct XThenAcute : terminal<lit<"x"> + !~cr<"é">> {};
  struct XThenOther : terminal<lit<"x"> + &~cr<"é">> {};
  struct XThenUpper : terminal<lit<"x"> + !~(cr<"é"> | uc::Lu)> {};
  struct AcuteFirst : terminal<!~cr<"é"> + any> {};
  struct XGuarded : terminal<lit<"x", !~cr<"é">>> {};
  struct Name : terminal<kw<"if">> {};
  struct Delim {};
  struct Run {};
  struct Closed : terminal<through(lit<"<">, lit<"\xA9">)> {};
  struct Heredoc
      : terminal<through(lit<"<<"> + mark<Delim>(some(~cr<" \n">)) + lit<"\n">,
                         lit<"\n"> + same<Delim>)> {};
  struct Letters
      : terminal<through(lit<"<"> + mark<Run>(some(lit<"é">)) + lit<">">,
                         lit<"<"> + same<Run> + lit<">">)> {};

  template <class T>
  struct Pick : rule<create<ast::Item> + assign<&ast::Item::text>(ref<T>)> {};
  template <class T>
  static constexpr auto item = append<&ast::Doc::items>(ref<Pick<T>>);
  template <cursiv::str Tag, class T>
  static constexpr auto arm = lit<Tag> + item<T>;
  template <cursiv::str Tag, class T>
  static constexpr auto then = lit<Tag> + item<T> + many(item<One>);

  struct Root
      : rule<create<ast::Doc> +
             (arm<"one:", One> | arm<"two:", Two> | arm<"qa:", QuotedAny> |
              arm<"qn:", QuotedNot> | arm<"sn:", StringNot> |
              arm<"sa:", StringAny> | arm<"paren:", Paren> |
              arm<"twice:", Twice> | arm<"whole:", Whole> |
              arm<"acc:", Accented> | arm<"nw:", NotWord> |
              arm<"strict:", Strict> | arm<"cre:", Acute> |
              arm<"crx:", AcuteHex> | arm<"ne:", NotAcute> | arm<"l:", Letter> |
              arm<"nl:", NotLetter> | arm<"a:", LetterA> | arm<"na:", NotA> |
              arm<"nna:", NotNotA> | arm<"either:", Either> |
              arm<"nd:", NonDigit> | arm<"nnd:", NotNonDigit> |
              arm<"kw:", Name> |
              arm<"thr:", Closed> | arm<"here:", Heredoc> |
              arm<"run:", Letters> | arm<"first:", AcuteFirst> |
              then<"rna:", RunNotA> | then<"rne:", RunNotAcute> |
              then<"xa:", XThenAcute> | then<"xo:", XThenOther> |
              then<"xu:", XThenUpper> | then<"xg:", XGuarded> |
              (lit<"rn1:"> + !~cr<"é"> + item<Acute>) |
              (lit<"rn2:"> + &~cr<"é"> + item<One>) |
              (lit<"kwt:"> + item<Name> + many(item<One>)) |
              (lit<"chars:"> + some(item<One>)) |
              (lit<"ncs:"> + some(item<NotA>)) |
              (lit<"bytes:"> + some(item<Raw>)) |
              (lit<"rb:"> + lit<"a"> + byte + lit<"b">) |
              (lit<"rc1:"> + &byte + lit<"x">) |
              (lit<"rc2:"> + &byte + lit<"é">) |
              (lit<"rc3:"> + &any + lit<"é">) |
              (lit<"rc4:"> + &cr<"})"> + lit<"}">) |
              (lit<"rc5:"> + &any + lit<"\xE9">) |
              (lit<"rc6:"> + &byte + lit<"\xE9">) |
              (lit<"rt1:"> + &byte + item<One>) |
              (lit<"rt3:"> + &any + item<One>)) +
             eof> {};
  using entry = Root;
  static constexpr auto word_boundary = uc::XID_Continue | cr<"_">;
};

// A condition on the gap reads the gap's characters, from a character.
struct Gaps : cursiv::GrammarSpec {
  struct Blank : terminal<some(~cr<"a-z">)> {};
  struct Root
      : rule<create<ast::Doc> +
             ((lit<"hn"> + lit<"a"> + gap::has(cr<"^é">) + lit<"b">) |
              (lit<"isn"> + lit<"a"> + gap::is(cr<"^é">) + lit<"b">) |
              (lit<"hbyte"> + lit<"a"> + gap::has(byte) + lit<"b">) |
              (lit<"isbb"> + lit<"a"> + gap::is(byte + byte) + lit<"b">) |
              (lit<"isaa"> + lit<"a"> + gap::is(any + any) + lit<"b">)) +
             eof> {};
  using entry = Root;
  using ignore = Blank;
};

// Unicode decoding and class membership use the shared lexical machinery.
// The full catalogue runs in Lr; this smaller entry exercises LL's token and
// condition boundaries without duplicating all Unicode-property tables.
struct Integration : Base {
  struct Entry
      : rule<(create<ast::Doc> +
              (arm<"one:", One> | arm<"two:", Two> |
               then<"rna:", RunNotA> | then<"rne:", RunNotAcute> |
               then<"xa:", XThenAcute> | then<"xo:", XThenOther> |
               then<"xg:", XGuarded> | arm<"first:", AcuteFirst> |
               (lit<"rn1:"> + !~cr<"é"> + item<Acute>) |
               (lit<"rn2:"> + &~cr<"é"> + item<One>) |
               (lit<"bytes:"> + some(item<Raw>)) |
               (lit<"rb:"> + lit<"a"> + byte + lit<"b">) |
               (lit<"rc1:"> + &byte + lit<"x">) |
               (lit<"rc2:"> + &byte + lit<"é">) |
               (lit<"rc3:"> + &any + lit<"é">) |
               (lit<"rc4:"> + &cr<"})"> + lit<"}">) |
               (lit<"rc5:"> + &any + lit<"\xE9">) |
               (lit<"rc6:"> + &byte + lit<"\xE9">) |
               (lit<"rt1:"> + &byte + item<One>) |
               (lit<"rt3:"> + &any + item<One>)) +
              eof) |
             (lit<"gaps:"> + skip(ignored<Gaps::Blank>, hide<>,
                                   ref<Gaps::Root> + eof))> {};
  using entry = Entry;
};

}  // namespace tchars

namespace trepeatedunicode {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct Scalar : terminal<uc::XID_Start> {};
  struct Word : terminal<uc::XID_Start + many(uc::XID_Continue)> {};
  struct Alias : terminal<lit<"@"> + ref<Word>> {};
  struct Complement : terminal<~uc::XID_Continue> {};
  struct Space : terminal<some(uc::White_Space)> {};
  struct Guarded : terminal<uc::XID_Start + !uc::XID_Continue> {};
  struct Tag {};
  struct Capture : terminal<mark<Tag>(some(lit<"=">)) + uc::XID_Start +
                            lit<":"> + same<Tag>> {};
  struct Root
      : rule<create<Node>(
            (lit<"scalar:"> + assign<&Node::text>(ref<Scalar>)) |
            (lit<"word:"> + assign<&Node::text>(ref<Word>)) |
            (lit<"alias:"> + assign<&Node::text>(ref<Alias>)) |
            (lit<"complement:"> + assign<&Node::text>(ref<Complement>)) |
            (lit<"space:"> + assign<&Node::text>(ref<Space>)) |
            (lit<"guard:"> + assign<&Node::text>(ref<Guarded>)) |
            (lit<"capture:"> + assign<&Node::text>(ref<Capture>)))> {};
  using entry = Root;
  using ignore = WS;
};


}  // namespace trepeatedunicode

namespace tchars {
struct Suite : Base {
  struct Entry : rule<ref<Base::Root> |
                      (lit<"gaps:"> + skip(ignored<Gaps::Blank>, hide<>,
                          ref<Gaps::Root> + eof)) |
                      (lit<"unicode:"> + skip(ignored<trepeatedunicode::Base::WS>,
                          hide<>, ref<trepeatedunicode::Base::Root> + eof))> {};
  using entry = Entry;
};
// The complete Unicode/byte catalogue runs once; LL keeps its own integration
// cases through Integration in the lexical-backends language.
struct Lr : Suite {
  struct Decimal : terminal<some(uc::Nd)> {};
  struct Where : terminal<kw<"où">> {};
  struct Entry : rule<ref<Suite::Entry> |
      (lit<"lex:"> + skip(ignored<tunicode::Cases::WS>, hide<>,
                          ref<tunicode::Cases::Entry> + eof)) |
      (create<ast::Doc> + ((lit<"digits:"> + item<Decimal>) |
                           (lit<"where:"> + item<Where> + many(item<One>))) +
       eof)> {};
  using entry = Entry;
  static constexpr auto word_boundary = uc::XID_Continue | cr<"_$">;
};
}  // namespace tchars
