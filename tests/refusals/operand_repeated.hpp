// The operand of a directive builds one node, and a repetition inside it
// writes the same member of that node on every turn.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view n;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  vector<Part*> items;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<append<&ast::Holder::items>(
                    many(assign<&ast::Part::n>(ref<ID>)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
