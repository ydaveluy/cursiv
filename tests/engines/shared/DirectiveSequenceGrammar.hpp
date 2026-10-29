#pragma once

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>
// A sequence of several elements that read, under `assign` or `append`, is an
// unnamed data rule: the member takes its parts joined.

namespace tdirseq {

enum class Op : std::uint8_t { equal, NOTEQUAL };

namespace ast {
struct Box : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Op op = Op::equal;
  string_view text;
  vector<string_view> names;
  Box* box = nullptr;
};
}  // namespace ast

inline constexpr auto upper =
    [](std::string_view text,
       std::pmr::memory_resource* mem) -> std::string_view {
  const auto room = cursiv::text_room(mem, text.size());
  for (std::size_t i = 0; i < text.size(); ++i)
    room[i] = text[i] >= 'a' && text[i] <= 'z'
                  ? static_cast<char>(text[i] - 'a' + 'A')
                  : text[i];
  return {room.data(), text.size()};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct EQ : terminal<as<Op::equal>(lit<"=">)> {};
  struct NE : terminal<as<Op::NOTEQUAL>(lit<"!=">)> {};
  using ignored = WS;
};

struct Dotted : Base {
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::name>(
                         ref<ID> + many(lit<"."> + ref<ID>))) +
                     eof> {};
};

struct Enumerated : Base {
  struct Root
      : rule<create<ast::Doc>(assign<&ast::Doc::op>(kw<"NOT"> + kw<"EQUAL">)) +
             eof> {};
};

struct Spelled : Base {
  struct Root : rule<create<ast::Doc>(
                         assign<&ast::Doc::text>(kw<"NOT"> + kw<"EQUAL">)) +
                     eof> {};
};

struct Listed : Base {
  struct Root
      : rule<create<ast::Doc>(
                 many(append<&ast::Doc::names>(ref<ID> + lit<":"> + ref<ID>))) +
             eof> {};
};

struct Called : Base {
  struct Shout : rule<as<upper>(lit<"!"> + ref<ID>)> {};
  struct Root
      : rule<create<ast::Doc>(assign<&ast::Doc::text>(ref<ID> + ref<Shout>)) +
             eof> {};
};

struct Guarded : Base {
  struct Root
      : rule<create<ast::Doc>(assign<&ast::Doc::op>(ref<NE> + &lit<"(">) +
                              lit<"(">) +
             eof> {};
};

struct Boxed : Base {
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::box>(
                         many(append<&ast::Box::items>(ref<ID>)))) +
                     eof> {};
};

struct Held : Base {
  struct Items
      : rule<create<ast::Box>(some(append<&ast::Box::items>(ref<ID>)))> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::box>(
                         lit<"("> + ref<Items> + lit<")">)) +
                     eof> {};
};

struct Tight : Base {
  struct Root : rule<create<ast::Doc>(
                         assign<&ast::Doc::text>(tight(lit<"<"> + lit<"<">))) +
                     eof> {};
};

struct Skipped : Base {
  struct DASH : terminal<some(lit<"-">)> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::text>(
                         skip(ignore<DASH>, hide<>, lit<"<"> + lit<"<">))) +
                     eof> {};
};

struct Chosen : Base {
  struct Root
      : rule<create<ast::Doc>(assign<&ast::Doc::op>(ref<EQ> | ref<NE>)) + eof> {
  };
};

struct Suite : Base {
  struct Root : rule<(lit<"dotted:"> + ref<Dotted::Root>) |
                     (lit<"enumerated:"> + ref<Enumerated::Root>) |
                     (lit<"spelled:"> + ref<Spelled::Root>) |
                     (lit<"listed:"> + ref<Listed::Root>) |
                     (lit<"called:"> + ref<Called::Root>) |
                     (lit<"guarded:"> + ref<Guarded::Root>) |
                     (lit<"boxed:"> + ref<Boxed::Root>) |
                     (lit<"held:"> + ref<Held::Root>) |
                     (lit<"tight:"> + ref<Tight::Root>) |
                     (lit<"skipped:"> + ref<Skipped::Root>) |
                     (lit<"chosen:"> + ref<Chosen::Root>)> {};
  using entry = Root;
};

}  // namespace tdirseq

// A flag's operand reads and writes in the current node: the flag sets its
// member once the operand has read, on the node then current.

namespace tflagop {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  boolean parenthesized = false;
};
struct Dim : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Decl : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  boolean hasInit = false;
  Expr* init = nullptr;
  boolean isArray = false;
  vector<Dim*> dims;
  boolean raw = false;
  string_view text;
  boolean isStatic = false;
  boolean isConst = false;
  boolean hasSize = false;
  string_view size;
};
struct Wrap : cursiv::Extends<Decl> {
  Decl* inner = nullptr;
  boolean bang = false;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(cr<"0-9">)> {};
  struct STR : terminal<lit<"'"> + many(cr<"a-z">) + lit<"'">> {};
  struct ExprRule
      : rule<create<ast::Expr>(assign<&ast::Expr::text>(ref<NUM>))> {};
  struct DimRule : rule<create<ast::Dim>(assign<&ast::Dim::name>(ref<ID>))> {};
  using ignored = WS;
};

struct Init : Base {
  struct Root
      : rule<create<ast::Decl>(
                 assign<&ast::Decl::name>(ref<ID>) +
                 option(flag<&ast::Decl::hasInit>(
                     lit<"="> + assign<&ast::Decl::init>(ref<ExprRule>))) +
                 flag<&ast::Decl::isConst>(option(lit<";">)) +
                 flag<&ast::Decl::isStatic>(
                     option(commit(lit<"%">, lit<"1">)))) +
             eof> {};
};

struct Counted : Base {
  struct Root
      : rule<create<ast::Decl>(
                 assign<&ast::Decl::name>(ref<ID>) +
                 flag<&ast::Decl::isStatic>(many(lit<"+">)) +
                 flag<&ast::Decl::isConst>(repeat<0, 3>(lit<"-">)) +
                 flag<&ast::Decl::isArray>(
                     many(append<&ast::Decl::dims>(ref<DimRule>)))) +
             eof> {};
};

struct Turned : Base {
  struct Root
      : rule<create<ast::Decl>(
                 assign<&ast::Decl::name>(ref<ID>) +
                 option(flag<&ast::Decl::hasInit>(many(lit<"*">)) +
                        lit<"!">) +
                 many(flag<&ast::Decl::raw>(many(lit<"~">)) +
                      append<&ast::Decl::dims>(ref<DimRule>))) +
             eof> {};
};

struct Dims : Base {
  struct Root
      : rule<create<ast::Decl>(
                 assign<&ast::Decl::name>(ref<ID>) +
                 option(flag<&ast::Decl::isArray>(
                     lit<"("> +
                     many(append<&ast::Decl::dims>(ref<DimRule>), lit<",">) +
                     lit<")">))) +
             eof> {};
};

struct Raw : Base {
  struct Root : rule<create<ast::Decl>(
                         assign<&ast::Decl::name>(ref<ID>) +
                         option(flag<&ast::Decl::raw>(
                             lit<"r"> + assign<&ast::Decl::text>(ref<STR>)))) +
                     eof> {};
};

struct Wrapped : Base {
  struct Root : rule<create<ast::Decl> + assign<&ast::Decl::name>(ref<ID>) +
                     option(flag<&ast::Wrap::bang>(
                         nest<ast::Wrap, &ast::Wrap::inner>(lit<"!">))) +
                     eof> {};
};

struct Pair : Base {
  struct Root
      : rule<create<ast::Decl>(
                 option(flag<&ast::Decl::isStatic>(lit<"$">)) +
                 option(flag<&ast::Decl::isConst>(lit<"#">)) +
                 assign<&ast::Decl::name>(ref<ID>) +
                 option(flag<&ast::Decl::hasInit>(
                     lit<"="> + assign<&ast::Decl::init>(ref<ExprRule>))) +
                 option(flag<&ast::Decl::hasSize>(
                     lit<"["> + assign<&ast::Decl::size>(ref<NUM>) +
                     lit<"]">))) +
             eof> {};
};

struct Inside : Base {
  struct Root : rule<flag<&ast::Expr::parenthesized>(lit<"("> + ref<ExprRule> +
                                                     lit<")">) +
                     eof> {};
};

struct Outside : Base {
  struct Root : rule<lit<"("> + ref<ExprRule> +
                     flag<&ast::Expr::parenthesized>(lit<")">) + eof> {};
};

struct Nested : Base {
  struct Item
      : rule<create<ast::Expr>(assign<&ast::Expr::text>(ref<NUM>)) |
             flag<&ast::Expr::parenthesized>(lit<"("> + ref<Item> + lit<")">)> {
  };
  struct Root : rule<ref<Item> + eof> {};
};

struct Doubled : Base {
  struct Root
      : rule<create<ast::Decl>(
                 assign<&ast::Decl::name>(ref<ID>) +
                 option(flag<&ast::Decl::isArray>(
                     lit<"("> +
                     flag<&ast::Decl::hasInit>(
                         lit<"="> + assign<&ast::Decl::init>(ref<ExprRule>)) +
                     lit<")">))) +
             eof> {};
};

struct Prefixed : Base {
  struct Root : rule<create<ast::Decl>(
                         option(flag<&ast::Decl::raw>(
                             assign<&ast::Decl::text>(lit<"++"> | lit<"--">))) +
                         assign<&ast::Decl::name>(ref<ID>)) +
                     eof> {};
};

struct Created : Base {
  struct Root : rule<(flag<&ast::Decl::isStatic>(
                          lit<"$"> + create<ast::Decl>(
                                         assign<&ast::Decl::name>(ref<ID>))) |
                      create<ast::Decl>(assign<&ast::Decl::name>(ref<ID>))) +
                     eof> {};
};

struct Suite : Base {
  struct Root : rule<(lit<"init:"> + ref<Init::Root>) |
                     (lit<"counted:"> + ref<Counted::Root>) |
                     (lit<"turned:"> + ref<Turned::Root>) |
                     (lit<"dims:"> + ref<Dims::Root>) |
                     (lit<"raw:"> + ref<Raw::Root>) |
                     (lit<"wrapped:"> + ref<Wrapped::Root>) |
                     (lit<"pair:"> + ref<Pair::Root>) |
                     (lit<"inside:"> + ref<Inside::Root>) |
                     (lit<"outside:"> + ref<Outside::Root>) |
                     (lit<"nested:"> + ref<Nested::Root>) |
                     (lit<"doubled:"> + ref<Doubled::Root>) |
                     (lit<"prefixed:"> + ref<Prefixed::Root>) |
                     (lit<"created:"> + ref<Created::Root>)> {};
  using entry = Root;
};

}  // namespace tflagop

// A data rule's parts: its token occurrences and its data-rule calls, in
// order. A call is one part, its rule's text.

namespace tdataparts {

namespace ast {
struct Name : cursiv::Extends<cursiv::AstNode> {
  string_view parts;
  string_view text;
};
}  // namespace ast

inline constexpr auto bars =
    [](std::span<const std::string_view> parts,
       std::pmr::memory_resource* mem) -> std::string_view {
  std::size_t size = 0;
  for (const auto part : parts) size += part.size() + 1;
  const auto room = cursiv::text_room(mem, size);
  std::size_t at = 0;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) room[at++] = '|';
    for (const char c : parts[i]) room[at++] = c;
  }
  return {room.data(), at};
};

inline constexpr auto upper =
    [](std::string_view text,
       std::pmr::memory_resource* mem) -> std::string_view {
  const auto room = cursiv::text_room(mem, text.size());
  for (std::size_t i = 0; i < text.size(); ++i)
    room[i] = text[i] >= 'a' && text[i] <= 'z'
                  ? static_cast<char>(text[i] - 'a' + 'A')
                  : text[i];
  return {room.data(), text.size()};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct SMALL : terminal<as<std::int8_t>(some(d))> {};

  struct Qualified : rule<as<bars>(some(ref<ID>, lit<".">))> {};

  struct Tail : rule<lit<"."> + ref<ID>> {};
  struct Tailed : rule<as<bars>(ref<ID> + many(ref<Tail>))> {};

  struct Shout : rule<as<upper>(lit<"!"> + ref<ID>)> {};
  struct Loud : rule<as<bars>(ref<ID> + many(ref<Shout>))> {};

  struct Word : rule<ref<ID>> {};
  struct Dotted : rule<lit<"."> + ref<Word>> {};
  struct Deep : rule<as<bars>(ref<Word> + many(ref<Dotted>))> {};

  struct Pair : rule<as<bars>(ref<SMALL> + lit<","> + ref<SMALL>)> {};

  struct Joined : rule<ref<ID> + many(ref<Tail>)> {};

  struct Word2 : rule<as<upper>(ref<ID>)> {};
  struct Words2 : rule<as<bars>(ref<Word2> + many(ref<Word2>))> {};
  struct Opt : rule<option(ref<ID>)> {};
  struct WithOpt : rule<as<bars>(ref<ID> + ref<Opt> + ref<ID>)> {};

  // Calls whose record is their one token, which a repair may misspell.
  struct Lit : rule<as<upper>(lit<"literal">)> {};
  struct WithLit : rule<as<bars>(ref<ID> + ref<Lit> + ref<ID>)> {};
  struct LitParts : rule<as<bars>(lit<"literal">)> {};

  struct Root : rule<create<ast::Name>(
                    (kw<"q"> + assign<&ast::Name::parts>(ref<Qualified>)) |
                    (kw<"t"> + assign<&ast::Name::parts>(ref<Tailed>)) |
                    (kw<"u"> + assign<&ast::Name::parts>(ref<Loud>)) |
                    (kw<"d"> + assign<&ast::Name::parts>(ref<Deep>)) |
                    (kw<"n"> + assign<&ast::Name::parts>(ref<Pair>)) |
                    (kw<"w"> + assign<&ast::Name::parts>(ref<Words2>)) |
                    (kw<"o"> + assign<&ast::Name::parts>(ref<WithOpt>)) |
                    (kw<"l"> + assign<&ast::Name::parts>(ref<WithLit>)) |
                    (kw<"p"> + assign<&ast::Name::parts>(ref<LitParts>) +
                     lit<";">) |
                    (kw<"k"> + assign<&ast::Name::text>(ref<Lit>) + lit<";">) |
                    (kw<"j"> + assign<&ast::Name::text>(ref<Joined>)))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

}  // namespace tdataparts

namespace tdirseq {
struct Combined : Suite {
  struct Entry : rule<ref<Suite::Root> | ref<tflagop::Suite::Root> |
                      (lit<"data:"> + skip(ignore<tdataparts::Base::WS>,
                          hide<tdataparts::Base::COMMENT>,
                          ref<tdataparts::Base::Root> + eof))> {};
  using entry = Entry;
};
}  // namespace tdirseq
