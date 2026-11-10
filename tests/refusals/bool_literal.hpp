// A literal other than `true` or `false`, written whole into a bool member.
// expect LR: [ '‘]BoolLiteral.*refuse::ast::Flag::enabled.*!
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Flag : cursiv::Extends<cursiv::AstNode> {
  boolean enabled = false;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Flag>(assign<&ast::Flag::enabled>(lit<"!">))> {};
  using entry = Root;
};

}  // namespace refuse
