// One branch of a choice adopts a node and the other does not: the rule hands
// one over on one path only.
// expect LR: this alternative returns a node that the text may leave out
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Item* item = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Item : rule<create<ast::Item>(assign<&ast::Item::name>(ref<ID>))> {};
  struct Maybe : rule<lit<"let"> + (ref<Item> | lit<"nothing">)> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::item>(ref<Maybe>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
