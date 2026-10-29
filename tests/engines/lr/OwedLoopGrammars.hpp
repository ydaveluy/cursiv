#pragma once

// Grammars whose owed insertion at the end of the text can lead back to a
// state that owes it again, one frame higher: a closure that follows the
// stack it grows never ends. `upe` reaches it only through a unit production
// the table eliminated.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace towed {

struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
};

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> kids;
};
struct Inner : cursiv::Extends<Node> {};
struct Outer : cursiv::Extends<Node> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

namespace loop {
struct G : Words {
  struct R0;
  struct R1 : rule<create<ast::Inner>(
                  (lit<"a"> +
                   many(lit<"b"> + option(lit<"b"> +
                                          append<&ast::Node::kids>(ref<R0>)))) |
                  (many(option(lit<"a">) + append<&ast::Node::kids>(ref<R0>)) +
                   lit<"a"> + append<&ast::Node::kids>(ref<R1>) +
                   append<&ast::Node::kids>(ref<R0>)))> {};
  struct R0 : rule<create<ast::Outer>(
                  (lit<"a"> + lit<"b">) |
                  (append<&ast::Node::kids>(ref<R1>) +
                   many(lit<"c"> + append<&ast::Node::kids>(ref<R0>) +
                        lit<"a">) +
                   lit<"a">))> {};
};
}  // namespace loop

namespace upe {
struct G : Words {
  struct R0;
  struct R2 : rule<many(lit<"b">) |
                   (((lit<"d"> + ref<R2>) | lit<"c">) +
                    some(option(lit<"a">) +
                         option(option(lit<"c"> + ref<R0>) + lit<"c">) +
                         lit<"a">) +
                    lit<"c"> + lit<"d"> + lit<"b">)> {};
  struct R0 : rule<option(lit<"a">) + some(lit<"c"> + ref<R2> + ref<R2>) +
                   lit<"c"> + lit<"d"> + lit<"b">> {};
  struct Top : rule<create<ast::Doc>(ref<R0>)> {};
};
}  // namespace upe

// Two grammars the random oracle drew whose closure at the end never ended:
// one reading conditions on the gap, one runs a committed loop.
namespace ast201 {
struct Node : cursiv::Extends<cursiv::AstNode> { vector<Node*> kids; };
struct N0 : cursiv::Extends<Node> {};
struct N1 : cursiv::Extends<Node> {};
struct N2 : cursiv::Extends<Node> {};
struct N3 : cursiv::Extends<Node> {};
struct Ta : cursiv::Extends<Node> {};
struct Tb : cursiv::Extends<Node> {};
struct Tc : cursiv::Extends<Node> {};
struct Td : cursiv::Extends<Node> {};
}  // namespace ast201

namespace seed201 {
struct G : Words {
  struct R0;
  struct R1;
  struct R2;
  struct R3;
  struct Ta : rule<create<ast201::Ta>(lit<"a">)> {};
  struct Tb : rule<create<ast201::Tb>(lit<"b">)> {};
  struct Tc : rule<create<ast201::Tc>(lit<"c">)> {};
  struct Td : rule<create<ast201::Td>(lit<"d">)> {};
  struct R3 : rule<create<ast201::N3>((lit<"c"> | append<&ast201::Node::kids>(ref<Td>) | (option((option((repeat<1, 3>(append<&ast201::Node::kids>(ref<R3>)) + append<&ast201::Node::kids>(ref<Td>))) + append<&ast201::Node::kids>(ref<R0>) + repeat<1, 3>((lit<"b"> + append<&ast201::Node::kids>(ref<Tc>))))) + append<&ast201::Node::kids>(ref<R1>) + lit<"b">)))> {};
  struct R2 : rule<create<ast201::N2>(((append<&ast201::Node::kids>(ref<Ta>) + (gap::line | eof)) | append<&ast201::Node::kids>(ref<R3>) | (lit<"c"> + append<&ast201::Node::kids>(ref<Ta>))))> {};
  struct R1 : rule<create<ast201::N1>(((gap::none + lit<"a"> + eof) | (append<&ast201::Node::kids>(ref<R0>) + lit<"c">)))> {};
  struct R0 : rule<create<ast201::N0>(((lit<"b"> + many(some((lit<"b"> + many((append<&ast201::Node::kids>(ref<Td>) + lit<"c">), lit<",">) + (!gap::none))))) | (option((lit<"a"> + many((repeat<1, 3>((append<&ast201::Node::kids>(ref<R3>) + lit<"c">)) + append<&ast201::Node::kids>(ref<R0>))))) + lit<"a">)))> {};
};
}  // namespace seed201

namespace ast383 {
struct Node : cursiv::Extends<cursiv::AstNode> { vector<Node*> kids; };
struct N0 : cursiv::Extends<Node> {};
struct N1 : cursiv::Extends<Node> {};
struct N2 : cursiv::Extends<Node> {};
struct N3 : cursiv::Extends<Node> {};
struct N4 : cursiv::Extends<Node> {};
struct Ta : cursiv::Extends<Node> {};
struct Tb : cursiv::Extends<Node> {};
struct Tc : cursiv::Extends<Node> {};
struct Td : cursiv::Extends<Node> {};
}  // namespace ast383

namespace seed383 {
struct G : Words {
  struct R0;
  struct R1;
  struct R2;
  struct R3;
  struct R4;
  struct Ta : rule<create<ast383::Ta>(lit<"a">)> {};
  struct Tb : rule<create<ast383::Tb>(lit<"b">)> {};
  struct Tc : rule<create<ast383::Tc>(lit<"c">)> {};
  struct Td : rule<create<ast383::Td>(lit<"d">)> {};
  struct R4 : rule<create<ast383::N4>(((((lit<"b"> + lit<"c"> + lit<"d">) | (many((lit<"d"> + many(lit<"a">, lit<",">)), lit<",">) + some((lit<"a"> | (lit<"d"> + append<&ast383::Node::kids>(ref<Td>)))) + lit<"c">)) + append<&ast383::Node::kids>(ref<Tb>) + lit<"d">) | (((option(commit(lit<"b">, append<&ast383::Node::kids>(ref<R4>))) + option((lit<"a"> + option((append<&ast383::Node::kids>(ref<Tb>) + append<&ast383::Node::kids>(ref<R0>))))) + some(commit(lit<"b">, many(lit<"a">)))) | (append<&ast383::Node::kids>(ref<Tb>) + append<&ast383::Node::kids>(ref<Tc>) + append<&ast383::Node::kids>(ref<Ta>))) + lit<"d">)))> {};
  struct R3 : rule<create<ast383::N3>((append<&ast383::Node::kids>(ref<Ta>) + lit<"c">))> {};
  struct R2 : rule<create<ast383::N2>(((append<&ast383::Node::kids>(ref<Tc>) + append<&ast383::Node::kids>(ref<Tc>)) | lit<"c">))> {};
  struct R1 : rule<create<ast383::N1>((lit<"a"> + eof))> {};
  struct R0 : rule<create<ast383::N0>(((append<&ast383::Node::kids>(ref<Ta>) + (eof | option(lit<"c">))) | append<&ast383::Node::kids>(ref<R4>) | (some((repeat<1, 3>(((append<&ast383::Node::kids>(ref<R0>) | append<&ast383::Node::kids>(ref<R1>)) + lit<"d">)) + repeat<2, 2>((lit<"d"> + append<&ast383::Node::kids>(ref<R0>))) + append<&ast383::Node::kids>(ref<Tc>))) + lit<"d"> + eof)))> {};
};
}  // namespace seed383

// The four in one grammar, each behind an opening mark the text follows with
// no gap, as a document's first token has none: no rule is shared, so each
// case owes what it owes alone.
struct G : Words {
  struct Case : rule<(lit<"loop:"> + ref<loop::G::R0>) |
                     (lit<"upe:"> + ref<upe::G::Top>) |
                     (lit<"201:"> + ref<seed201::G::R0>) |
                     (lit<"383:"> + ref<seed383::G::R0>)> {};
  using entry = Case;
  using ignored = WS;
};

}  // namespace towed
