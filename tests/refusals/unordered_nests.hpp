// Seven nests in any order: a group of nests is expanded into every order, and
// seven items give too many.
// expect LR LL: refused: rule .Root. has an unordered group with too many items
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
             unordered(
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"!">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"?">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"~">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"@">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"#">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"$">)),
                 nest<&ast::Wrap::inner>(assign<&ast::Wrap::op>(lit<"%">)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
