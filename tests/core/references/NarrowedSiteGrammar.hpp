#pragma once

// A member admits both kinds and the site narrows to one: the name is looked
// up among that kind only.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tns {

struct Symbol : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Kind1 : cursiv::Extends<Symbol> {};
struct Kind2 : cursiv::Extends<Symbol> {};
struct Use : cursiv::Extends<cursiv::AstNode> {
  reference<Symbol> what;
  multi_reference<Symbol> all;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<cursiv::AstNode*> items;
};

struct NarrowedSite : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct K1 : rule<create<Kind1> + kw<"one"> + assign<&Kind1::name>(ref<ID>)> {
  };
  struct K2 : rule<create<Kind2> + kw<"two"> + assign<&Kind2::name>(ref<ID>)> {
  };
  struct U : rule<create<Use> + kw<"use"> +
                  assign<cursiv::xref<&Use::what, Kind1>>(ref<ID>) +
                  option(kw<"all"> +
                         assign<cursiv::xref<&Use::all, Kind1>>(ref<ID>))> {};
  struct Root : rule<create<Doc> +
                     some(append<&Doc::items>(ref<K1> | ref<K2> | ref<U>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tns
