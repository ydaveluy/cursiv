// A write the text may skip is no node to bury on every path.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Wrapper : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  cursiv::AstNode* inner = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Root : rule<option(assign<&ast::Wrapper::name>(ref<ID>)) +
                     nest<ast::Wrapper, &ast::Wrapper::inner>(lit<"!">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
