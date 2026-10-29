// A rule whose only content is a reference the text may skip: it hands over
// a node on some paths only.
// expect LR: this alternative hands over a node the text may leave out
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
  struct Maybe : rule<option(ref<Item>)> {};
  struct Root : rule<create<ast::Doc>(assign<&ast::Doc::item>(ref<Maybe>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
