#pragma once

// A word two terminals read where one state takes both: the literal `a`, which
// the lexer ranks above the name, and the name. Only the last token `x`
// leaves them both standing; the name's arm is written first, and the
// lexer's rank still comes first.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tkeyword {

struct Keyed : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};
struct Named : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Keyed* keyed{};
  Named* named{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};
  struct KW : terminal<lit<"a">> {};
  struct KeyedRule
      : rule<create<Keyed>(many(append<&Keyed::words>(ref<KW>)) + lit<"x">)> {};
  struct NamedRule
      : rule<create<Named>(many(append<&Named::words>(ref<ID>)) + lit<"x">)> {};
  struct Root : rule<create<Doc>(assign<&Doc::named>(ref<NamedRule>) |
                                 assign<&Doc::keyed>(ref<KeyedRule>)) +
                     eof> {};
  using entry = Root;
  using ignore = WS;
};

// The same behind a token both arms share.
struct Behind : Base {
  struct KeyedRule
      : rule<create<Keyed>(lit<"p"> + many(append<&Keyed::words>(ref<KW>)) +
                           lit<"x">)> {};
  struct NamedRule
      : rule<create<Named>(lit<"p"> + many(append<&Named::words>(ref<ID>)) +
                           lit<"x">)> {};
  struct Root : rule<create<Doc>(assign<&Doc::named>(ref<NamedRule>) |
                                 assign<&Doc::keyed>(ref<KeyedRule>)) +
                     eof> {};
  using entry = Root;
};
}  // namespace tkeyword
