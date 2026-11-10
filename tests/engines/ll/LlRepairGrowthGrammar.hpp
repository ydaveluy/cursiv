#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tllrepairgrowth {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct N0 : cursiv::Extends<Node> {
  vector<Node*> children;
};
struct N1 : cursiv::Extends<Node> {
  vector<Node*> children;
};
struct N2 : cursiv::Extends<Node> {
  vector<Node*> children;
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
      : rule<create<N0>(append<&N0::children>(ref<R1>) + lit<"a">) |
             create<N0>(append<&N0::children>(ref<R1>) + option(lit<"a">) +
                        lit<"b">) |
             create<N0>(append<&N0::children>(ref<R1>) + lit<"a"> + lit<"a">) |
             ref<R1>> {};
  struct R1
      : rule<create<N1>(append<&N1::children>(ref<R2>) +
                        option(append<&N1::children>(ref<R0>)) + lit<"c">) |
             ref<R2>> {};
  struct R2 : rule<create<N2>(many(lit<"b">) + lit<"c"> + option(lit<"c">))> {};
  // A dangling `else`: a "b" belongs to the deepest level that stands open,
  // whatever follows it.
  struct Dangling
      : rule<create<N1>(lit<"a"> +
                        option(append<&N1::children>(ref<Dangling>)) +
                        option(lit<"b">))> {};
  struct DanglingTwo
      : rule<create<N1>(lit<"a"> +
                        option(append<&N1::children>(ref<DanglingTwo>)) +
                        option(lit<"b">) + option(lit<"c">))> {};
  // ...but where what follows them reads one too, whether a "b" closes this
  // level or a deeper one is read to the end of the document, and the answers
  // of all the levels stand in one plan.
  struct Nested
      : rule<create<N1>(lit<"a"> + option(append<&N1::children>(ref<Nested>)) +
                        option(lit<"b">))> {};
  struct Mixed
      : rule<create<N1>(lit<"a"> + option(append<&N1::children>(ref<Mixed>)) +
                        option(lit<"b">) + option(lit<"c">))> {};
  // Two loops over one token: what the first leaves the second can read, and a
  // choice of the first is decided by the next token, however far the fault.
  struct TwoLoops : rule<create<N1>(lit<"two:"> + many(lit<"a">) + many(lit<"a">) +
                                    lit<"b">)> {};
  struct Root : rule<ref<TwoLoops> | (lit<"dangling:"> + ref<Dangling>) |
                     (lit<"danglingtwo:"> + ref<DanglingTwo>) |
                     (lit<"nested:"> + ref<Nested> + option(lit<"b">)) |
                     (lit<"mixed:"> + ref<Mixed> + option(lit<"b">)) | ref<R0>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tllrepairgrowth
