#pragma once

// Drawn grammars, reduced, whose valid document fills the room with readings
// frozen at a fault they cannot repair while the deriving reading still waits:
// the search from the frozen ones runs out of budget first, at no cost.

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace troom {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  vector<Node*> children;
};
struct N0 : cursiv::Extends<Node> {};
struct N1 : cursiv::Extends<Node> {};
struct N2 : cursiv::Extends<Node> {};
}  // namespace ast

namespace seed221 {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct R0;
  struct R1 : rule<create<ast::N1>(lit<"a">)> {};
  struct R0
      : rule<create<ast::N0>(
            (option(lit<"d">) +
             option(lit<"b"> | append<&ast::Node::children>(ref<R0>)) +
             option(lit<"c"> + option(append<&ast::Node::children>(ref<R0>) +
                                      append<&ast::Node::children>(ref<R0>))) +
             option(lit<"a"> + lit<"c"> +
                    append<&ast::Node::children>(ref<R0>)) +
             lit<"a"> + option(lit<"d">)) |
            (option(append<&ast::Node::children>(ref<R1>)) +
             append<&ast::Node::children>(ref<R0>) +
             append<&ast::Node::children>(ref<R0>)))> {};
  using entry = R0;
  using ignored = WS;
};
}  // namespace seed221

}  // namespace troom
