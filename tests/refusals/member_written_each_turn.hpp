// A single-valued member written on every turn of a repetition: only the last
// turn's value would stay.
// expect LR: [ '‘]WrittenEachTurn<.*refuse::G::Root.*refuse::ast::Holder::first
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  string_view first;
};
}  // namespace ast
struct G : Base {
  struct Root
      : rule<create<ast::Holder>(some(assign<&ast::Holder::first>(ref<ID>)))> {
  };
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
