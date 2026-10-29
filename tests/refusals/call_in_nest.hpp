// A nest makes its wrapper the current node: a call inside it adopts a node
// the wrapper already replaced.
// expect LR: this alternative can create twice
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee = nullptr;
};
}  // namespace ast
struct G : Base {
  struct Name : rule<assign<&ast::Expr::name>(ref<ID>)> {};
  struct Root : rule<ref<Name> + many(nest<ast::Call, &ast::Call::callee>(
                                     lit<"("> + ref<Name> + lit<")">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
