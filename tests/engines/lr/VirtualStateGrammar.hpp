#pragma once

// Arms a flag tells apart, in grammars that read no trivia: readings that
// imply their tokens at the end of input, and arms that reduce one byte on
// either side of an unbounded run.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tvirtual {

struct Node : cursiv::Extends<cursiv::AstNode> {
  bool first = false;
  bool second = false;
  Node* child = nullptr;
};

// The first reading already implied its semicolon at EOF. Only the second
// can still imply the final one after both readings have reduced Part.
struct G : cursiv::GrammarSpec {
  struct A : terminal<lit<"x">> {};
  struct B : terminal<lit<"x">> {};
  static constexpr auto semi = implied(lit<";">, eof);
  struct First : rule<flag<&Node::first>(ref<A>) + semi> {};
  struct Second : rule<flag<&Node::second>(ref<B>) +
                       implied(lit<":">, eof) + gap::none> {};
  struct Part : rule<lit<"p"> + (ref<First> | ref<Second>)> {};
  struct Root : rule<create<Node>(assign<&Node::child>(ref<Part>) +
                                 implied(lit<",">, eof) + semi)> {};
  using entry = Root;
};

}  // namespace tvirtual

namespace treducecells {

struct Node : cursiv::Extends<cursiv::AstNode> {
  bool first = false;
  bool second = false;
};

struct G : cursiv::GrammarSpec {
  struct First : rule<flag<&Node::first>(lit<"p">)> {};
  struct Second : rule<flag<&Node::second>(lit<"p">)> {};
  static constexpr auto tail = (lit<"x"> | lit<"y"> | lit<"z">) + many(lit<"n">);
  struct Root : rule<((ref<First> + tail + lit<"a">) |
                      (ref<Second> + tail + lit<"b">)) +
                     lit<"!">> {};
  using entry = Root;
};

}  // namespace treducecells
