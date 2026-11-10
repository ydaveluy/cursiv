#pragma once

// Two readings stay alive through a repetition whose two arms read the same
// text, spelled apart so neither is dropped as a twin. The elected rank grows
// with the text; the graph stays under its pruning budget.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace trankgrowth {

struct Node : cursiv::Extends<cursiv::AstNode> {
  boolean first = false;
  boolean second = false;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct First : rule<flag<&Node::first>(lit<"x">)> {};
  struct Second : rule<flag<&Node::second>(lit<"x">)> {};
  struct ElemA : rule<lit<"a">> {};
  struct ElemB : rule<repeat<1>(lit<"a">)> {};
  struct Elem : rule<ref<ElemA> | ref<ElemB>> {};
  struct Root : rule<(ref<First> + many(ref<Elem>) + option(lit<";">)) |
                     (ref<Second> + many(ref<Elem>) + option(lit<"!">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace trankgrowth
