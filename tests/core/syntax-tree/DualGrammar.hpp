#pragma once

// One grammar both engines generate, each body entered by its own first token:
// documented keywords, a derived node read back through reflection, a string
// body over code points, a root that owns its children, and the terminals an
// override chain replaces -- the chain's last grammar is the one generated.

#include <concepts>
#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "ConvertGrammars.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tdual {

struct Word : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Child : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Base : cursiv::Extends<cursiv::AstNode> {
  Child* head = nullptr;
  reference<Child> target;
};
struct Derived : cursiv::Extends<Base> {
  variant<Child*, int64> choice;
  optional<Child*> tail;
  vector<Child*> items;
  optional<multi_reference<Child>> links;
};

struct Str : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};

struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Mark : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
  boolean sealed = false;
  optional<Mark*> mark;
};

struct N : cursiv::Extends<cursiv::AstNode> {
  std::uint64_t n = 0;
};

struct Decl : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Function : cursiv::Extends<Decl> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // Excludes the quote, the backslash, CR, LF and U+2028: a class of code
  // points, whose backslash is the one the bytes would read.
  struct STR
      : terminal<lit<"'"> + many(cr<R"(^'\\\r\n\u2028)"> | (lit<"\\"> + any)) +
                 lit<"'">> {};
  // A hidden record: kept in the tree, unlike `ignore`'s raw trivia, so a
  // rule name can still find it.
  struct NOTE : terminal<lit<"#"> + many(cr<R"(^\n)">)> {};
  // One terminal per shape an override takes, all three spelled alike.
  static constexpr auto hex_conv = [](std::string_view t) {
    return t.size() * 1000;
  };
  struct HEX : terminal<lit<"0x"> + some(cr<"0-9a-fA-F">),
                        cursiv::text_conv<hex_conv>> {};
  struct DEC : terminal<lit<"0x"> + some(cr<"0-9a-fA-F">),
                        cursiv::text_conv<hex_conv>> {};
  struct TEN : terminal<lit<"0x"> + some(cr<"0-9a-fA-F">),
                        cursiv::text_conv<hex_conv>> {};

  // One word read case-insensitively, one exactly, and a spelling holding a
  // NUL.
  struct Documented
      : rule<create<Word>((ikw<"events">.doc<"Opens the events.">() |
                           kw<"state">.doc<"Names a state.">() |
                           lit<"a\0b">.doc<"binary literal">()) +
                          assign<&Word::name>(ref<ID>))> {};

  struct ChildR : rule<kw<"child"> + assign<&Child::name>(ref<ID>)> {};
  struct Reflected
      : rule<create<Derived> + assign<&Base::head>(ref<ChildR>) +
             assign<&Derived::choice>(ref<ChildR>) +
             option(kw<"tail"> + assign<&Derived::tail>(ref<ChildR>)) +
             many(kw<"item"> + append<&Derived::items>(ref<ChildR>)) +
             option(kw<"target"> + assign<&Base::target>(ref<ID>)) +
             option(kw<"links"> + assign<&Derived::links>(ref<ID>))> {};

  struct Quoted : rule<create<Str> + assign<&Str::text>(ref<STR>)> {};

  struct ItemR : rule<create<Item> + assign<&Item::name>(ref<ID>)> {};
  struct MarkR : rule<create<Mark> + kw<"mark">> {};
  // `item` is spelled at root level, outside the owned child: the keyword
  // walk stops at an owned boundary, so a keyword inside `ItemR` would be
  // the Item's to find, not the root's.
  struct Owner : rule<create<Doc> + kw<"doc"> +
                      option(flag<&Doc::sealed>(kw<"sealed">)) +
                      option(assign<&Doc::mark>(ref<MarkR>)) +
                      many(kw<"item"> + append<&Doc::items>(ref<ItemR>))> {};

  // The create comes after the write and keeps it.
  struct Declared : rule<kw<"fn"> + assign<&Decl::name>(ref<ID>) +
                         create<Function>(lit<"("> + lit<")">)> {};

  struct Converted : rule<assign<&N::n>(ref<HEX>) |
                          (kw<"dec"> + assign<&N::n>(ref<DEC>)) |
                          (kw<"ten"> + assign<&N::n>(ref<TEN>))> {};

  struct Root : rule<ref<Documented> | ref<Reflected> | ref<Quoted> |
                     ref<Owner> | ref<Converted> | ref<Declared>> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<NOTE>;
};

// A chain of three over G, one shape per terminal: HEX keeps the base's
// converter under a new body; DEC names a bare target the base's converter
// does not read; TEN has its converter restated in the middle and its body at
// the end, which keeps the middle one's converter and type.
struct D1 : cursiv::extends<G> {
  static constexpr auto tens_conv = [](std::string_view t) {
    return static_cast<std::uint32_t>(t.size() * 10);
  };
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, G::TEN>
  struct override<T> : terminal<lit<"0x"> + some(cr<"0-9a-fA-F">),
                                cursiv::text_conv<tens_conv>> {};
};
struct D2 : cursiv::extends<D1> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, G::HEX>
  struct override<T> : terminal<lit<"0X"> + some(cr<"0-9a-fA-F">)> {};
  template <class T>
    requires std::same_as<T, G::DEC>
  struct override<T> : terminal<some(cr<"0-9">), std::uint64_t> {};
  template <class T>
    requires std::same_as<T, G::TEN>
  struct override<T> : terminal<lit<"0Y"> + some(cr<"0-9a-fA-F">)> {};
};
struct Lr : D2 {};
struct Ll : D2 {
  // A keyword in a converter's template argument must remain qualified in
  // LL output as well as LR output, without generating all converters twice.
  struct Root
      : rule<ref<G::Root> |
             (lit<"$conv:"> +
              skip(ignored<tconv::G::WS>, hide<tconv::G::CM>,
                   ref<tconv::G::KeywordRoot> + eof))> {};
  using entry = Root;
};

}  // namespace tdual
