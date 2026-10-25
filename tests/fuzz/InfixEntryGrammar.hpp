#pragma once

// `infix<>` as the entry: the model gives a climb no children of its own
// (`GrammarCost`'s own note), and a walk that reached the entry through the
// children of a rule wrote nothing at all.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace finfix {

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Node> {
  std::string_view text;
};
struct Bin : cursiv::Extends<Node> {
  Node* left = nullptr;
  std::string_view op;
  Node* right = nullptr;
};
}  // namespace ast

struct InfixEntryG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct Sum;
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>)) |
                        (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Sum : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                     (left(lit<"+"> | lit<"-"> | (kw<"NOT"> + kw<"EQUAL">)))> {};
  using entry = Sum;
  using ignore = WS;
};

// An operator of several keywords is a data rule: the member holds its parts
// joined, never the blank between them.
struct InfixSequenceG : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct Sum;
  struct Primary : rule<create<ast::Num>(assign<&ast::Num::text>(ref<NUM>)) |
                        (lit<"("> + ref<Sum> + lit<")">)> {};
  struct Sum : infix<Primary, &ast::Bin::left, &ast::Bin::op, &ast::Bin::right,
                     (left(lit<"+"> | (kw<"NOT"> + kw<"EQUAL">)))> {};
  using entry = Sum;
  using ignore = WS;
};

}  // namespace finfix
