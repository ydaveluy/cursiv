// A flag around a call changes nothing: the call still adopts a node beside
// the `create`, so the alternative builds two nodes.
// expect LR: this alternative can create two nodes
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on;
  string_view name;
};
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct PartRule : rule<create<ast::Part>(assign<&ast::Part::name>(ref<ID>))> {
  };
  struct Root : rule<create<ast::Item>(flag<&ast::Item::on>(ref<PartRule>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
