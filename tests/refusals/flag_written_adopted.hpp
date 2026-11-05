// A flag written by an adopted rule counts as an earlier write.
// expect LR: WrittenOverAdopted<.*refuse::G::Root.*Item::on
#pragma once
#include "refusals/head.hpp"
namespace refuse {
namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
};
}  // namespace ast
struct G : Base {
  struct Part : rule<flag<&ast::Item::on>(lit<"!">)> {};
  struct Root : rule<ref<Part> + assign<&ast::Item::on>(as<false>(lit<"?">))> {
  };
  using entry = Root;
};
}  // namespace refuse
