#pragma once

// Equations `a = b` whose pattern may run on through the next equation's `=`:
// each `=` is the pattern's operator or the equation's own, and only the end
// of the text refuses a reading, so every one stays alive to the last token.
// `inner` writes the longer pattern first, `outer` the shorter.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace trunon {

struct Pat : cursiv::Extends<cursiv::AstNode> {
  string_view head;
  Pat* rest{};
};
struct Def : cursiv::Extends<cursiv::AstNode> {
  Pat* pat{};
  string_view rhs;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Def*> defs;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n)">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct InnerPat
      : rule<create<Pat>(assign<&Pat::head>(ref<ID>) + many(ref<ID>) +
                         option(lit<"="> + assign<&Pat::rest>(ref<InnerPat>)))> {
  };
  struct OuterPat
      : rule<create<Pat>(assign<&Pat::head>(ref<ID>) + many(ref<ID>)) |
             create<Pat>(assign<&Pat::head>(ref<ID>) + many(ref<ID>) +
                         lit<"="> + assign<&Pat::rest>(ref<OuterPat>))> {};
  struct InnerDef
      : rule<create<Def>(assign<&Def::pat>(ref<InnerPat>) + lit<"="> +
                         assign<&Def::rhs>(ref<ID>))> {};
  struct OuterDef
      : rule<create<Def>(assign<&Def::pat>(ref<OuterPat>) + lit<"="> +
                         assign<&Def::rhs>(ref<ID>))> {};
  struct Root
      : rule<create<Doc>(
                 (kw<"inner"> + some(append<&Doc::defs>(ref<InnerDef>))) |
                 (kw<"outer"> + some(append<&Doc::defs>(ref<OuterDef>)))) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Base {};

}  // namespace trunon
