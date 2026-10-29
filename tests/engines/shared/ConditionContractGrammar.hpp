#pragma once

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include <cstddef>
#include <memory_resource>
#include <string_view>
// What a condition in a rule tests, on both engines: the text of the token the
// lexer chooses among the terminals the parser can take where it stands; the
// first terminals of the conditions there lengthen that token and rank
// nothing. Each shape stands behind its own opening word; blanks are skipped
// and the whole input is read.

namespace tcondcontract {

struct Doc : cursiv::Extends<cursiv::AstNode> {};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

inline std::string_view tagged(std::string_view text, char tag,
                               std::pmr::memory_resource* mem) {
  const auto room = cursiv::text_room(mem, text.size() + 2);
  room[0] = tag;
  room[1] = ':';
  for (std::size_t i = 0; i < text.size(); ++i) room[i + 2] = text[i];
  return {room.data(), room.size()};
}
inline constexpr auto id_text = [](std::string_view t,
                                   std::pmr::memory_resource* mem) {
  return tagged(t, 'I', mem);
};
inline constexpr auto name_text = [](std::string_view t,
                                     std::pmr::memory_resource* mem) {
  return tagged(t, 'N', mem);
};

struct Main : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<as<id_text>(some(cr<"a-zA-Z">))> {};
  struct NAME : terminal<as<name_text>(some(cr<"a-zA-Z">))> {};
  struct LOW : terminal<some(cr<"a-z">)> {};
  struct REST : terminal<some(any)> {};
  struct CLOSER : terminal<cr<"})">> {};
  struct ATERM : terminal<&lit<"a"> + ref<NAME>> {};
  struct Pair : rule<lit<"a"> + lit<"b">> {};
  struct Root
      : rule<create<Holder>(
                 (kw<"spec1"> + &(ref<ID> + lit<"b">)+ref<ID> + lit<"b">) |
                 (kw<"spec2"> + !kw<"AS"> + ref<ID>) |
                 (kw<"spec3"> + !lit<"let"> + ref<ID>) |
                 (kw<"spec4"> + !lit<"<"> + lit<"<<">) |
                 (kw<"tokand"> + &lit<"<"> + lit<"<<">) |
                 (kw<"tokandnot"> + &!lit<"<"> + lit<"<<">) |
                 (kw<"tokphrase"> + &(lit<"<"> + lit<"<">) + lit<"<<">) |
                 (kw<"tokwhole"> + &lit<"<<"> + lit<"<<">) |
                 (kw<"tokchoice"> + !(lit<"<"> | lit<"x">) + lit<"<<">) |
                 (kw<"joined"> + !lit<"=="> + lit<"="> + lit<"=">) |
                 (kw<"head"> + &lit<"a"> + assign<&Holder::name>(ref<NAME>)) |
                 (kw<"inside"> + ref<ATERM>) |
                 (kw<"same"> + &ref<LOW> + assign<&Holder::name>(ref<NAME>)) |
                 (kw<"other"> + !ref<LOW> + assign<&Holder::name>(ref<NAME>)) |
                 (kw<"word"> + &ref<ID> + kw<"if">) |
                 (kw<"notword"> + !ref<ID> + kw<"if">) |
                 (kw<"litword"> + &ref<ID> + lit<"if">) |
                 (kw<"litnotword"> + !ref<ID> + lit<"if">) |
                 (kw<"notif"> + !kw<"if"> + assign<&Holder::name>(ref<ID>)) |
                 (kw<"argv"> + &kw<"ARGV"> + assign<&Holder::name>(ref<ID>)) |
                 (kw<"first"> + lit<"a"> + &lit<"b"> + lit<"bc">) |
                 (kw<"phrase"> + &(lit<"a"> + lit<"b">)+lit<"a"> + lit<"bc">) |
                 (kw<"made"> + !(lit<"ab"> + lit<"x">)+lit<"a"> + lit<"b">) |
                 (kw<"blank"> + !lit<" a"> + assign<&Holder::name>(ref<ID>)) |
                 (kw<"closea"> + &cr<"})"> + lit<"}">) |
                 (kw<"closeb"> + &ref<CLOSER> + lit<"}">) |
                 (kw<"opener"> + !cr<"{[("> + lit<"(">) |
                 (kw<"nest"> +
                  &(lit<"x"> + ((ref<ID> + lit<":">) | (kw<"if"> + lit<"(">))) +
                  lit<"x"> + ref<REST>) |
                 (kw<"shared"> +
                  ((&ref<ID> + assign<&Holder::name>(ref<ID>)) |
                   (!kw<"if"> + assign<&Holder::name>(ref<ID>)))) |
                 (kw<"sharedkw"> +
                  ((&ref<ID> + assign<&Holder::name>(ref<ID>)) |
                   (!kw<"if"> + assign<&Holder::name>(ref<ID>)) |
                   (kw<"if"> + lit<"(">))) |
                 (kw<"real"> + &kw<"if"> + lit<"if"> + lit<"x">) |
                 (lit<"absorb:"> + &lit<"x"> + ref<REST>) |
                 (lit<"absorbnot:"> + !lit<"x"> + ref<REST>) |
                 (lit<"eat:"> + &lit<"b"> + some(any)) |
                 (lit<"eatkw:"> + &kw<"b"> + some(any)) |
                 (lit<"eatnot:"> + !lit<"b"> + some(any)) |
                 (kw<"commit"> + many(commit(lit<"<">, lit<"x">)) + lit<"<<">) |
                 (kw<"run"> + many(commit(ref<ID>, lit<":"> + ref<ID>)) +
                  kw<"end">) |
                 (kw<"paren"> + many(commit(cr<"(">, lit<"x">)) + lit<"(">) |
                 (kw<"sepend"> +
                  many(commit(ref<ID>, lit<":"> + any), lit<",">) +
                  lit<","> + kw<"end">) |
                 (kw<"seplist"> +
                  many(commit(ref<ID>, lit<":"> + any), lit<",">) +
                  option(lit<","> + kw<"end">)) |
                 (kw<"stopc"> + many(commit(lit<"x"> + lit<"a">, lit<"!">)) +
                  lit<"x"> + lit<"ab">) |
                 (kw<"stope"> + many(lit<"x"> + lit<"a"> + lit<"!">) +
                  !(lit<"x"> + lit<"a">)+lit<"x"> + lit<"ab">) |
                 (kw<"pair"> + &ref<Pair> + lit<"a"> + lit<"b">) |
                 (kw<"expand"> + many(lit<"<"> + lit<"x">) + !lit<"<"> +
                  lit<"<<">)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};

struct Own : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LOW : terminal<as<int>(some(cr<"a-z">))> {};
  struct NAME : terminal<some(cr<"a-zA-Z">)> {};
  struct Root
      : rule<create<Holder>(&ref<LOW> + assign<&Holder::name>(ref<NAME>)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};

struct Spelled : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  struct Block : rule<lit<"{"> + many(ref<Stmt>) + lit<"}">> {};
  struct Name : rule<ref<ID> + implied(lit<";">, &cr<"}"> | eof)> {};
  struct Stmt : rule<ref<Block> | ref<Name>> {};
  struct Root : rule<create<Doc>(many(ref<Stmt>))> {};
  using entry = Root;
  using ignored = WS;
};

struct Classed : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Stmt;
  struct Block : rule<lit<"{"> + many(ref<Stmt>) + cr<"}">> {};
  struct Name : rule<ref<ID> + implied(lit<";">, &cr<"}"> | eof)> {};
  struct Stmt : rule<ref<Block> | ref<Name>> {};
  struct Root : rule<create<Doc>(many(ref<Stmt>))> {};
  using entry = Root;
  using ignored = WS;
};

struct Remark : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct REMARK : terminal<ikw<"REM"> + many(cr<R"(^\n)">)> {};
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>((lit<"@"> + &kw<"REM"> + ref<X>) |
                                 (lit<"%"> + !kw<"REM"> + ref<X>)) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
  using hidden = REMARK;
};

struct Tie : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root
      : rule<create<Doc>((&lit<"if"> + kw<"if">) | (lit<"if"> + lit<";">)) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tcondcontract

// A template tail opens on `}`, as a block's end does: it is lexed only
// where a template continuation can stand, so an implied `;` before `}` is
// never read as the start of a tail that ends on a later line.

namespace ttemplatetail {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUM : terminal<some(d)> {};
  struct TplHead : terminal<lit<"`"> + many(cr<"^`$">) + lit<"${">> {};
  struct TplTail : terminal<lit<"}"> + many(cr<"^`">) + lit<"`">> {};
  struct Expr;
  struct Template : rule<ref<TplHead> + ref<Expr> + ref<TplTail>> {};
  struct Expr : rule<(ref<ID> + lit<"="> + ref<Expr>) | ref<Template> |
                     ref<ID> | ref<NUM>> {};
  struct Stmt : rule<ref<Expr> + implied(lit<";">, &lit<"}"> | eof)> {};
  struct Fn
      : rule<kw<"function"> + ref<ID> + lit<"{"> + many(ref<Stmt>) + lit<"}">> {
  };
  struct Root : rule<create<Doc>(many(ref<Fn>)) + eof> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace ttemplatetail

namespace tcondcontract {
struct Suite : Main {
  // These three forms keep their original start-of-input and gap conditions.
  struct Root : rule<ref<Main::Root> | ref<Own::Root> |
                     ref<ttemplatetail::Base::Root> |
                     (lit<"spelled:"> + ref<Spelled::Root>) |
                     (lit<"classed:"> + ref<Classed::Root>) |
                     (lit<"tie:"> + ref<Tie::Root>) |
                     skip(ignore<Remark::WS>, hide<Remark::REMARK>,
                          ref<Remark::Root> + eof)> {};
  using entry = Root;
};
}  // namespace tcondcontract
