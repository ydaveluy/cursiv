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
  bool sealed{};
  vector<string_view> values;
  Child* child{};
  Expr* expression{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(d)> {};
  struct Item : rule<assign<&Child::name>(ref<ID>)> {};
  struct Inner : rule<kw<"a"> & kw<"b">> {};
  struct Anon : rule<create<Child>(option(assign<&Child::name>(ref<ID>)))> {};
  struct Postfix : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                        (nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)) &
                         nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">)))> {};
  struct PostfixWide
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             (nest<&Wrap::inner>(lit<"("> + assign<&Wrap::op>(lit<"!">) +
                                 lit<")">) &
              nest<&Wrap::inner>(lit<"("> + assign<&Wrap::op>(lit<"?">) +
                                 lit<")">))> {};
  struct PostfixBlock : rule<(create<Name>(assign<&Name::name>(ref<ID>)) +
                              nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">))) &
                             lit<"x">> {};
  // Three nests in any order, and a nest writing over its own group as over a
  // run.
  struct PostfixThree
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             (nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)) &
              nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">)) &
              nest<&Wrap::inner>(assign<&Wrap::op>(lit<"~">)))> {};
  struct PostfixMaybe
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             (option(nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">))) &
              option(nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">))))> {};
  struct FlagGroup : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                          nest<&Wrap::inner>(assign<&Wrap::child>(ref<Item>) &
                                             flag<&Wrap::flag>(lit<"!">))> {};
  struct FlagRun : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                        nest<&Wrap::inner>(assign<&Wrap::child>(ref<Item>) +
                                           flag<&Wrap::flag>(lit<"!">))> {};
  struct Root
      : rule<create<Record>(
                 (kw<"pair"> + (assign<&Record::first>(kw<"a">) &
                                assign<&Record::second>(kw<"b">))) |
                 (kw<"fields"> +
                  ((kw<"name"> + assign<&Record::first>(ref<ID>)) &
                   (kw<"value"> + assign<&Record::second>(ref<ID>)) &
                   flag<&Record::flag>(kw<"flag">))) |
                 (kw<"four"> + (append<&Record::values>(kw<"a">) &
                                append<&Record::values>(kw<"b">) &
                                append<&Record::values>(kw<"c">) &
                                append<&Record::values>(kw<"d">))) |
                 (kw<"blocks"> + (some(append<&Record::values>(kw<"a">)) &
                                  flag<&Record::flag>(kw<"b">))) |
                 (kw<"bounded"> +
                  (repeat<2>(append<&Record::values>(kw<"a">)) & kw<"b">)) |
                 (kw<"nested"> + (ref<Inner> & kw<"c">)) |
                 (kw<"prefix"> + ((kw<"x"> + kw<"a">)&(kw<"x"> + kw<"b">))) |
                 (kw<"duplicate"> + (kw<"a"> & kw<"a">)) |
                 (kw<"twin"> + (assign<&Record::first>(ref<ID>) &
                                assign<&Record::second>(ref<ID>) &
                                kw<"z">)) |
                 (kw<"child"> +
                  ((kw<"node"> + assign<&Record::child>(ref<Item>)) &
                   flag<&Record::flag>(kw<"flag">))) |
                 (kw<"optional"> + option(kw<"a"> & kw<"b">)) |
                 (kw<"wrap"> + assign<&Record::expression>(ref<Postfix>)) |
                 (kw<"wrapwords"> +
                  assign<&Record::expression>(ref<PostfixWide>)) |
                 (kw<"headblock"> +
                  assign<&Record::expression>(ref<PostfixBlock>)) |
                 (kw<"edges"> + tight(lit<"a">& lit<"b">)) |
                 (kw<"trio"> +
                  tight(lit<"a">& lit<"b">& lit<"c">)) |
                 (kw<"nests"> +
                  assign<&Record::expression>(ref<PostfixThree>)) |
                 (kw<"maybe"> +
                  (option(append<&Record::values>(kw<"a">)) &
                   option(append<&Record::values>(kw<"b">)))) |
                 (kw<"gather"> +
                  (many(append<&Record::values>(kw<"a">)) &
                   many(append<&Record::values>(kw<"b">)))) |
                 (kw<"modifiers"> +
                  (option(flag<&Record::flag>(kw<"static">)) &
                   option(flag<&Record::sealed>(kw<"final">)) &
                   assign<&Record::first>(ref<ID>))) |
                 (kw<"emptychild"> +
                  (assign<&Record::child>(ref<Anon>) &
                   flag<&Record::flag>(kw<"y">))) |
                 (kw<"order"> +
                  (option(assign<&Record::first>(ref<ID>)) &
                   flag<&Record::flag>(kw<"y">) &
                   option(assign<&Record::second>(ref<ID>)))) |
                 (kw<"maybenests"> +
                  assign<&Record::expression>(ref<PostfixMaybe>)) |
                 (kw<"flaggroup"> +
                  assign<&Record::expression>(ref<FlagGroup>)) |
                 (kw<"flagrun"> + assign<&Record::expression>(ref<FlagRun>)) |
                 (kw<"tight"> +
                  tight(lit<"["> + (lit<"a"> & lit<"b">)+lit<"]">))) +
             lit<";"> + eof> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<COMMENT>;
};
struct Ll : Base {};
struct Lr : Base {};

// Items that read nothing without being options: an end of input, a rule that
// may read nothing, an implied token and a condition.
struct Ends : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct End : rule<eof> {};
  struct Root
      : rule<create<Name>(
            (kw<"endof"> + (assign<&Name::name>(kw<"a">) & eof)) |
            (kw<"endrule"> + (assign<&Name::name>(kw<"a">) & ref<End>)) |
            (kw<"implied"> + (assign<&Name::name>(kw<"a">) &
                              implied(lit<";">, eof))) |
            (kw<"negated"> + (assign<&Name::name>(kw<"a">) & !kw<"b">) +
             option(kw<"b">)) |
            (kw<"empty"> + (option(assign<&Name::name>(kw<"a">)) &
                            ref<End>)))> {};
  using entry = Root;
  using ignore = WS;
};
struct EndsLl : Ends {};
struct EndsLr : Ends {};

// The widest groups both engines spell: eleven positions, and six nests,
// whose orders stay whole -- and the next one wider, which both refuse.
#define TUNORDERED_NEST(OP) \
  nest<&Wrap::inner>(assign<&Wrap::op>(lit<OP>))
struct Eleven : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<kw<"a"> & kw<"b"> & kw<"c"> & kw<"d"> & kw<"e"> & kw<"f"> &
                     kw<"g"> & kw<"h"> & kw<"i"> & kw<"j"> & kw<"k">> {};
  using entry = Root;
  using ignore = WS;
};
struct ElevenOptional : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<option(kw<"a">) & option(kw<"b">) & option(kw<"c">) &
                     option(kw<"d">) & option(kw<"e">) & option(kw<"f">) &
                     option(kw<"g">) & option(kw<"h">) & option(kw<"i">) &
                     option(kw<"j">) & option(kw<"k">)> {};
  using entry = Root;
  using ignore = WS;
};
struct Twelve : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<kw<"a"> & kw<"b"> & kw<"c"> & kw<"d"> & kw<"e"> & kw<"f"> &
                     kw<"g"> & kw<"h"> & kw<"i"> & kw<"j"> & kw<"k"> &
                     kw<"l">> {};
  using entry = Root;
  using ignore = WS;
};
struct SixNests : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                     (TUNORDERED_NEST("1") & TUNORDERED_NEST("2") &
                      TUNORDERED_NEST("3") & TUNORDERED_NEST("4") &
                      TUNORDERED_NEST("5") & TUNORDERED_NEST("6"))> {};
  using entry = Root;
  using ignore = WS;
};
struct SevenNests : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                     (TUNORDERED_NEST("1") & TUNORDERED_NEST("2") &
                      TUNORDERED_NEST("3") & TUNORDERED_NEST("4") &
                      TUNORDERED_NEST("5") & TUNORDERED_NEST("6") &
                      TUNORDERED_NEST("7"))> {};
  using entry = Root;
  using ignore = WS;
};
#undef TUNORDERED_NEST

// Groups far past both: fourteen positions, and nine nests.
struct TooWide : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<kw<"a"> & kw<"b"> & kw<"c"> & kw<"d"> & kw<"e"> & kw<"f"> &
                     kw<"g"> & kw<"h"> & kw<"i"> & kw<"j"> & kw<"k"> & kw<"l"> &
                     kw<"m"> & kw<"n">> {};
  using entry = Root;
  using ignore = WS;
};
struct TooWideNests : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
                     (nest<&Wrap::inner>(assign<&Wrap::op>(lit<"1">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"2">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"3">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"4">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"5">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"6">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"7">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"8">)) &
                      nest<&Wrap::inner>(assign<&Wrap::op>(lit<"9">)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace tunordered
