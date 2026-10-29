// A call adopts its node, and a write after it lands there: a member the
// called rule may have written is written twice.
// expect LR: [ '‘]WrittenOverAdopted<.*refuse::G::Root.*refuse::ast::Item::name
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
}  // namespace ast
struct G : Base {
  struct ItemRule : rule<assign<&ast::Item::name>(ref<ID>)> {};
  struct Root
      : rule<ref<ItemRule> + lit<":"> + assign<&ast::Item::name>(ref<ID>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
