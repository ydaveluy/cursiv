#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tunordered {

struct Child : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Wrap : cursiv::Extends<Expr> {
  Expr* inner{};
  string_view op;
  bool flag{};
  Child* child{};
};
struct Record : cursiv::Extends<cursiv::AstNode> {
  string_view first;
  string_view second;
  bool flag{};
  vector<string_view> values;
  Child* child{};
  Expr* expression{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(d)> {};
  struct Item : rule<assign<&Child::name>(ref<ID>)> {};
  struct Inner : rule<unordered(kw<"a">, kw<"b">)> {};
  struct Postfix
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             unordered(nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)),
                       nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">)))> {};
  struct PostfixWide
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             unordered(
                 nest<&Wrap::inner>(lit<"("> + assign<&Wrap::op>(lit<"!">) +
                                    lit<")">),
                 nest<&Wrap::inner>(lit<"("> + assign<&Wrap::op>(lit<"?">) +
                                    lit<")">))> {};
  struct PostfixBlock
      : rule<unordered(create<Name>(assign<&Name::name>(ref<ID>)) +
                           nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)),
                       lit<"x">)> {};
  // Three nests in any order, and a nest writing over its own group as over a
  // run.
  struct PostfixThree
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             unordered(nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)),
                       nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">)),
                       nest<&Wrap::inner>(assign<&Wrap::op>(lit<"~">)))> {};
  struct FlagGroup
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             nest<&Wrap::inner>(unordered(assign<&Wrap::child>(ref<Item>),
                                          flag<&Wrap::flag>(lit<"!">)))> {};
  struct FlagRun : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                        nest<&Wrap::inner>(assign<&Wrap::child>(ref<Item>) +
                                           flag<&Wrap::flag>(lit<"!">))> {};
  struct Root
      : rule<
            create<Record>(
                (kw<"pair"> + unordered(assign<&Record::first>(kw<"a">),
                                        assign<&Record::second>(kw<"b">))) |
                (kw<"fields"> +
                 unordered(kw<"name"> + assign<&Record::first>(ref<ID>),
                           kw<"value"> + assign<&Record::second>(ref<ID>),
                           flag<&Record::flag>(kw<"flag">))) |
                (kw<"four"> + unordered(append<&Record::values>(kw<"a">),
                                        append<&Record::values>(kw<"b">),
                                        append<&Record::values>(kw<"c">),
                                        append<&Record::values>(kw<"d">))) |
                (kw<"blocks"> +
                 unordered(some(append<&Record::values>(kw<"a">)),
                           flag<&Record::flag>(kw<"b">))) |
                (kw<"bounded"> +
                 unordered(repeat<2>(append<&Record::values>(kw<"a">)),
                           kw<"b">)) |
                (kw<"nested"> + unordered(ref<Inner>, kw<"c">)) |
                // The same group spelled in place: an item still.
                (kw<"group"> +
                 unordered(unordered(kw<"a">, kw<"b">), kw<"c">)) |
                (kw<"prefix"> +
                 unordered(kw<"x"> + kw<"a">, kw<"x"> + kw<"b">)) |
                (kw<"duplicate"> + unordered(kw<"a">, kw<"a">)) |
                (kw<"twin"> + unordered(assign<&Record::first>(ref<ID>),
                                        assign<&Record::second>(ref<ID>),
                                        kw<"z">)) |
                (kw<"child"> +
                 unordered(kw<"node"> + assign<&Record::child>(ref<Item>),
                           flag<&Record::flag>(kw<"flag">))) |
                (kw<"optional"> + option(unordered(kw<"a">, kw<"b">))) |
                (kw<"wrap"> + assign<&Record::expression>(ref<Postfix>)) |
                (kw<"wrapwords"> +
                 assign<&Record::expression>(ref<PostfixWide>)) |
                (kw<"headblock"> +
                 assign<&Record::expression>(ref<PostfixBlock>)) |
                (kw<"edges"> + tight(unordered(lit<"a">, lit<"b">))) |
                (kw<"trio"> + tight(unordered(lit<"a">, lit<"b">, lit<"c">))) |
                (kw<"nests"> + assign<&Record::expression>(ref<PostfixThree>)) |
                (kw<"gather"> +
                 unordered(some(append<&Record::values>(kw<"a">)),
                           some(append<&Record::values>(kw<"b">)))) |
                (kw<"flaggroup"> +
                 assign<&Record::expression>(ref<FlagGroup>)) |
                (kw<"flagrun"> + assign<&Record::expression>(ref<FlagRun>)) |
                (kw<"tight"> +
                 tight(lit<"["> + unordered(lit<"a">, lit<"b">) + lit<"]">))) +
            lit<";"> + eof> {};
  using entry = Root;
  using ignored = WS;
  using hidden = list<COMMENT>;
};
struct Ll : Base {};
struct Lr : Base {};

// The widest groups both engines spell: eleven positions, and six nests,
// whose orders stay whole -- and the next one wider, which both refuse.
#define TUNORDERED_NEST(OP) \
  nest<&Wrap::inner>(assign<&Wrap::op>(lit<OP>))
struct Eleven : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<unordered(kw<"a">, kw<"b">, kw<"c">, kw<"d">, kw<"e">, kw<"f">,
                       kw<"g">, kw<"h">, kw<"i">, kw<"j">, kw<"k">)> {};
  using entry = Root;
  using ignored = WS;
};
// Eleven items within that width, each of which the text may leave out: both
// spell the group, which may then match nothing.
struct ElevenOptional : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<unordered(option(kw<"a">), option(kw<"b">), option(kw<"c">),
                       option(kw<"d">), option(kw<"e">), option(kw<"f">),
                       option(kw<"g">), option(kw<"h">), option(kw<"i">),
                       option(kw<"j">), option(kw<"k">))> {};
  using entry = Root;
  using ignored = WS;
};
struct Twelve : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<unordered(kw<"a">, kw<"b">, kw<"c">, kw<"d">, kw<"e">, kw<"f">,
                       kw<"g">, kw<"h">, kw<"i">, kw<"j">, kw<"k">, kw<"l">)> {
  };
  using entry = Root;
  using ignored = WS;
};
struct SixNests : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                     unordered(TUNORDERED_NEST("1"), TUNORDERED_NEST("2"),
                               TUNORDERED_NEST("3"), TUNORDERED_NEST("4"),
                               TUNORDERED_NEST("5"), TUNORDERED_NEST("6"))> {};
  using entry = Root;
  using ignored = WS;
};
struct SevenNests : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                     unordered(TUNORDERED_NEST("1"), TUNORDERED_NEST("2"),
                               TUNORDERED_NEST("3"), TUNORDERED_NEST("4"),
                               TUNORDERED_NEST("5"), TUNORDERED_NEST("6"),
                               TUNORDERED_NEST("7"))> {};
  using entry = Root;
  using ignored = WS;
};
#undef TUNORDERED_NEST

}  // namespace tunordered
