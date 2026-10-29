#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// Unordered groups whose items the text may leave out: an absent item writes
// nothing, as an absent `option` does in a sequence.
namespace toptional {

struct Child : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Record : cursiv::Extends<cursiv::AstNode> {
  string_view first;
  string_view second;
  bool flag{};
  vector<string_view> values;
  Child* child{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root
      : rule<create<Record>(
                 // One required item and three optional ones, each writing.
                 (kw<"mix"> +
                  unordered(kw<"a"> + assign<&Record::first>(ref<ID>),
                            option(kw<"b"> + assign<&Record::second>(ref<ID>)),
                            option(flag<&Record::flag>(kw<"c">)),
                            many(append<&Record::values>(kw<"d">)))) |
                 // The same items in order, for the tree an absent item
                 // leaves.
                 (kw<"ordered"> + kw<"a"> + assign<&Record::first>(ref<ID>) +
                  option(kw<"b"> + assign<&Record::second>(ref<ID>)) +
                  option(flag<&Record::flag>(kw<"c">)) +
                  many(append<&Record::values>(kw<"d">))) |
                 // Only optional items: the group may match nothing.
                 (kw<"opt"> +
                  unordered(option(assign<&Record::first>(kw<"a">)),
                            option(assign<&Record::second>(kw<"b">)))) |
                 (kw<"count"> +
                  unordered(repeat<0, 2>(append<&Record::values>(kw<"a">)),
                            kw<"b">)) |
                 // A region over an optional item writes nothing either.
                 (kw<"region"> + unordered(tight(option(assign<&Record::first>(
                                               lit<"<"> + lit<">">))),
                                           kw<"b">)) |
                 // A node made over a group that may match nothing.
                 (kw<"child"> + assign<&Record::child>(create<Child>(unordered(
                                    option(assign<&Child::name>(ref<ID>)),
                                    option(kw<"x">)))))) +
             lit<";"> + eof> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};

// What both engines still refuse: an item that may match nothing and is no
// option...
struct NullableItem : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<unordered(option(kw<"a">) + option(kw<"b">), kw<"c">)> {};
  using entry = Root;
  using ignored = WS;
};
// ...an option whose present form may match nothing...
struct OptionalOption : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<unordered(option(option(kw<"a">)), kw<"c">)> {};
  using entry = Root;
  using ignored = WS;
};
// ...and an optional item in a group that holds a nest.
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Wrap : cursiv::Extends<Expr> {
  Expr* inner{};
  string_view op;
};
struct OptionalNest : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(d)> {};
  struct Root
      : rule<create<Name>(assign<&Name::name>(ref<ID>)) +
             unordered(
                 nest<&Wrap::inner>(assign<&Wrap::op>(lit<"!">)),
                 option(nest<&Wrap::inner>(assign<&Wrap::op>(lit<"?">))))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace toptional
