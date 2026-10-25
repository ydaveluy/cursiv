#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Flag : cursiv::Extends<cursiv::AstNode> {
  bool enabled = false;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Flag>(assign<&ast::Flag::enabled>(lit<"!">))> {};
  using entry = Root;
};

}  // namespace refuse
