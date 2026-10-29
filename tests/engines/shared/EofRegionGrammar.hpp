#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "EofRegionAst.hpp"
namespace teofregion {

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct TEXT : terminal<some(cr<"^<">)> {};
  struct WORD : terminal<lit<"x">> {};
  struct IslandRule
      : rule<skip(ignore<WS>, hide<>,
                  lit<"<?"> + many(append<&Island::words>(ref<WORD>)) +
                      (lit<"?>"> | eof))> {};
  struct Root : rule<create<Document>(many(
                    append<&Document::parts>(ref<TEXT> | ref<IslandRule>)))> {};
  using entry = Root;
};

struct ChainBase : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" ">)> {};
  struct InnerEnd : rule<skip(ignore<WS>, hide<>, eof)> {};
  struct StrictEnd : rule<tight(eof)> {};
  struct EmptyRule : rule<create<Empty>> {};
  struct Root
      : rule<create<ChainDocument>(
            (lit<"inner"> + skip(ignore<WS>, hide<>, eof) + eof) |
            (lit<"outer"> + eof + skip(ignore<WS>, hide<>, eof)) |
            (lit<"ref-inner"> + ref<InnerEnd> + eof) |
            (lit<"ref-outer"> + eof + ref<InnerEnd>) |
            (lit<"direct"> + skip(ignore<WS>, hide<>,
                                  lit<"x"> + tight(eof))) |
            (lit<"call"> +
             skip(ignore<WS>, hide<>, lit<"x"> + ref<StrictEnd>)) |
            (lit<"node"> + skip(ignore<WS>, hide<>, eof) +
             assign<&ChainDocument::empty>(ref<EmptyRule>)))> {};
  using entry = Root;
};

struct PureBase : ChainBase {
  struct Root
      : rule<create<ChainDocument>(skip(ignore<WS>, hide<>, eof) + eof)> {};
  using entry = Root;
};
struct PureLl : PureBase {};
struct PureLr : PureBase {};

}  // namespace teofregion

namespace tnullableregion {
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct OPEN : terminal<lit<"#{"> + many(s | ref<COMMENT>)> {};
  struct NUMBER : terminal<some(d)> {};
  struct BlockRule;
  struct NumberRule : rule<assign<&Number::text>(ref<NUMBER>)> {};
  struct StringRule
      : rule<create<String>(
            tight(lit<"\""> + ref<OPEN> +
                      skip(ignore<WS>, hide<COMMENT>,
                           assign<&String::hole>(ref<BlockRule>) + lit<"}">) +
                      lit<"\"">))> {};
  struct BlockRule : rule<create<Block>(option(assign<&Block::value>(
                         ref<NumberRule> | ref<StringRule>)))> {};
  using entry = BlockRule;
  using ignored = WS;
  using hidden = COMMENT;
};
}  // namespace tnullableregion

namespace tregionalreadmode {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct HTML : terminal<some(cr<"^<"> | lit<"<", !lit<"?">>)> {};
  struct SHEBANG : terminal<lit<"#!"> + many(cr<R"(^\r\n)">)> {};
  struct OPEN : terminal<lit<"<?php"> + !cr<R"(^ \t\r\n)">> {};
  struct CLOSE : terminal<lit<"?>"> + option(eol)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct DECIMAL : terminal<lit<"0"> | (cr<"1-9"> + many(d))> {};
  struct OCTAL : terminal<lit<"0"> + some(cr<"0-7">)> {};
  struct SIGNED : terminal<option(cr<"+-">) + some(d)> {};
  static constexpr auto Open = ref<OPEN> | lit<"<?">;
  static constexpr auto Eos = implied(lit<";">, &ref<CLOSE> | eof);
  static constexpr auto BridgeBody =
      lit<"?>"> + many(lit<"<", !lit<"?">> | cr<"^<">);
  struct BRIDGE : terminal<BridgeBody + Open> {};
  struct ECHO_BRIDGE : terminal<BridgeBody + lit<"<?=">> {};
  struct Expr;
  struct Atom
      : rule<!gap::line +
             (create<Node>(lit<"?"> + append<&Node::args>(ref<Atom>)) |
              create<Node>(assign<&Node::name>(ref<SIGNED>)) |
              create<Node>(assign<&Node::name>(ref<DECIMAL> | ref<OCTAL>)) |
              create<Node>(assign<&Node::name>(ref<ID>) +
                           many(lit<"("> +
                                many(append<&Node::args>(ref<Expr>), lit<",">) +
                                lit<")">) +
                           option(lit<"?"> + append<&Node::args>(ref<Expr>) +
                                  lit<":"> +
                                  append<&Node::args>(ref<Expr>))))> {};
  struct Expr : infix<Atom, Node,
                      levels(left(lit<"*">), left(lit<"+">),
                             left(lit<"&", !lit<"&">>), left(lit<"||">))> {};
  struct EchoRule : rule<create<Echo>(
                        ikw<"echo"> +
                        some(append<&Node::args>(ref<Expr>), lit<",">) + Eos)> {
  };
  struct Bridge
      : rule<create<Text>(assign<&Node::name>(ref<BRIDGE>)) |
             create<Echo>(assign<&Node::name>(ref<ECHO_BRIDGE>) +
                          some(append<&Node::args>(ref<Expr>), lit<",">) +
                          Eos)> {};
  struct Label : rule<create<Node>(assign<&Node::name>(ref<ID>) + lit<":">)> {};
  struct Stmt;
  struct Braces
      : rule<create<Node>(lit<"{"> + many(append<&Node::args>(ref<Stmt>)) +
                          lit<"}">)> {};
  struct ExprStmt : rule<!lit<"{"> + ref<Expr> + Eos> {};
  struct Stmt : rule<ref<Bridge> | ref<Braces> | ref<EchoRule> | ref<Label> |
                     ref<ExprStmt>> {};
  struct Code
      : rule<create<Block>(&(Open | lit<"<?=">)+skip(
            ignore<WS>, hide<>,
            (Open |
             (lit<"<?="> +
              append<&Node::args>(create<Echo>(
                  some(append<&Node::args>(ref<Expr>), lit<",">) + Eos)))) +
                many(append<&Node::args>(ref<Stmt>)) + (ref<CLOSE> | eof)))> {};
  struct Html : rule<create<Text>(assign<&Node::name>(ref<HTML>))> {};
  struct Root
      : rule<create<Doc>(option(ref<SHEBANG>) +
                         many(append<&Doc::items>(ref<Html> | ref<Code>)))> {};
  using entry = Root;
  using ignored = list<>;
  using hidden = list<>;
};
}  // namespace tregionalreadmode

// `eof` is the end of the text; `!byte` and `!any`, in a rule, ask the token in
// hand like any condition: they hold before a token longer than one byte, or
// one character.

namespace tbytecond {

struct Inner : cursiv::GrammarSpec {
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Pick : rule<create<ast::Item> + assign<&ast::Item::text>(ref<WORD>)> {
  };
  struct Root
      : rule<create<ast::Doc>(append<&ast::Doc::items>(ref<Pick>) + eof)> {};
  using entry = Root;
};

struct Host : cursiv::GrammarSpec {
  struct Root : rule<create<ast::Section>(assign<&ast::Section::doc>(
                    section<Inner>(lit<"<">, lit<">">)))> {};
  using entry = Root;
};

struct Base : cursiv::GrammarSpec {
  struct WORD : terminal<some(~cr<" ">)> {};
  struct LastByte : terminal<lit<"x"> + !byte> {};
  struct LastEof : terminal<lit<"x"> + eof> {};
  struct Pick : rule<create<ast::Item> + assign<&ast::Item::text>(ref<WORD>)> {
  };
  template <class T>
  struct Take : rule<create<ast::Item> + assign<&ast::Item::text>(ref<T>)> {};
  static constexpr auto item = append<&ast::Doc::items>(ref<Pick>);
  struct Root
      : rule<create<ast::Doc>(
            (lit<"byte:"> + !byte + item) | (lit<"any:"> + !any + item) |
            (lit<"end:"> + item + eof) | (lit<"more:"> + !eof + item) |
            (lit<"stop:"> + &eof) |
            (lit<"tbyte:"> + append<&ast::Doc::items>(ref<Take<LastByte>>)) |
            (lit<"teof:"> + append<&ast::Doc::items>(ref<Take<LastEof>>)))> {};
  struct Entry : rule<ref<Root> | ref<Host::Root>> {};
  using entry = Entry;
};


}  // namespace tbytecond

namespace teofregion {
// EOF, interpolation and section cases share one entry, with local trivia where
// needed.
struct Suite : cursiv::GrammarSpec {
  struct Entry
      : rule<(lit<"island:"> + ref<Base::Root>) |
             (lit<"chain:"> + ref<ChainBase::Root>) |
             section<tnullableregion::Base>(lit<"nullable:">, lit<"@END@">) |
             (lit<"regional:"> + ref<tregionalreadmode::G::Root>) |
             (lit<"bytecond:"> + ref<tbytecond::Base::Entry>)> {};
  using entry = Entry;
};
struct Ll : Suite {};
struct Lr : Suite {};
struct Aux : Suite {};
}  // namespace teofregion
