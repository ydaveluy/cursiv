// A literal that spells no number, written whole into a number member.
// expect LR at compile: [ '‘]NumberLiteral.*refuse::ast::Count::value
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Count : cursiv::Extends<cursiv::AstNode> {
  int value = 0;
};
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Count>(
                    lit<"s"> + assign<&ast::Count::value>(lit<"abc">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
