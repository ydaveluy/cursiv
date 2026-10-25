#pragma once

// A root with no name of its own, over two named nodes.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace toutline {

struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Item* first{};
  Item* second{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct ItemRule
      : rule<create<Item>(lit<"@"> + assign<&Item::name>(ref<ID>))> {};
  struct Root
      : rule<create<Holder>(assign<&Holder::first>(ref<ItemRule>) + lit<"|"> +
                            assign<&Holder::second>(ref<ItemRule>) + eof)> {};
  using entry = Root;
  using ignore = WS;
};
struct Lr : Base {};

}  // namespace toutline
