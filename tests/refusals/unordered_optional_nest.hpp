// An optional item in a group that holds a nest: such a group spells every
// order in full, so both engines refuse the item, naming it.
// expect LR LL: refused .nullableUnorderedItem..*rule .Root. has an optional item, .*Wrap.inner=.*, in an unordered group whose items hold a .nest.
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
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) +
                     unordered(nest<&ast::Wrap::inner>(
                                   assign<&ast::Wrap::op>(lit<"!">)),
                               option(nest<&ast::Wrap::inner>(
                                   assign<&ast::Wrap::op>(lit<"?">))))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
