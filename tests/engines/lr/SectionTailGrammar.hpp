#pragma once

// A token that stays open inside an embedded grammar: `<aaa>` never reads
// `LONG`, and the section it opens ends at `>`, which its own grammar does not
// know.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tsection {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Host : cursiv::Extends<cursiv::AstNode> {
  Node* inner = nullptr;
};

struct Inner : cursiv::GrammarSpec {
  struct LONG : terminal<some(cr<"a">) + lit<"b">> {};
  struct Root : rule<create<Node>(assign<&Node::text>(ref<LONG>) + eof)> {};
  using entry = Root;
};

struct Outer : cursiv::GrammarSpec {
  struct Root : rule<create<Host>(
                    assign<&Host::inner>(section<Inner>(lit<"<">, lit<">">)))> {
  };
  using entry = Root;
};
struct Ll : Outer {};
struct Lr : Outer {};

}  // namespace tsection
