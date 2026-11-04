// A class deriving twice from cursiv::AstNode: no pointer to it converts.
// expect LR: this rule builds a class that reaches cursiv::AstNode through two bases
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Left : cursiv::Extends<cursiv::AstNode> {};
struct Right : cursiv::Extends<cursiv::AstNode> {};
struct Both : cursiv::Extends<Left, Right> {
  string_view name;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Both>(assign<&ast::Both::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
