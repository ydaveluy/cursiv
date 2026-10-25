// A flag over an operand that may read nothing always sets the member: the
// grammar is read, and the generator says so.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on;
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<create<ast::Item>(flag<&ast::Item::on>(option(kw<"on">)) +
                                       assign<&ast::Item::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
