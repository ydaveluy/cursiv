#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllrepairgrowth {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct N0 : cursiv::Extends<Node> {
  vector<Node*> kids;
};
struct N1 : cursiv::Extends<Node> {
  vector<Node*> kids;
};
struct N2 : cursiv::Extends<Node> {
  vector<Node*> kids;
};

// Recursive wrapping with four callers of the same prefix. When the first
// token is missing, weighing invented completions must share those callers
// even when another reading at the same position has an unknown caller.
struct Ll : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct R0;
  struct R1;
  struct R2;
  struct R0
      : rule<create<N0>(append<&N0::kids>(ref<R1>) + lit<"a">) |
             create<N0>(append<&N0::kids>(ref<R1>) + option(lit<"a">) +
                        lit<"b">) |
             create<N0>(append<&N0::kids>(ref<R1>) + lit<"a"> + lit<"a">) |
             ref<R1>> {};
  struct R1 : rule<create<N1>(append<&N1::kids>(ref<R2>) +
                              option(append<&N1::kids>(ref<R0>)) + lit<"c">) |
                   ref<R2>> {};
  struct R2 : rule<create<N2>(many(lit<"b">) + lit<"c"> + option(lit<"c">))> {};
  using entry = R0;
  using ignore = WS;
};

}  // namespace tllrepairgrowth
