// A single-valued member written on every turn of a repetition: only the last
// turn's value would stay.
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
  using ignore = WS;
};

}  // namespace refuse
