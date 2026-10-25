// Six nests in any order, the most either engine spells.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Wrap : cursiv::Extends<cursiv::AstNode> {
  AstNode* inner{};
  string_view op;
};
}  // namespace ast

struct G : Base {
  struct Root
      : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) +
             (nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"!">)) &
              nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"?">)) &
              nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"~">)) &
              nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"@">)) &
              nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"#">)) &
              nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"$">)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
