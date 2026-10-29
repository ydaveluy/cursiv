// A directive's payload creates its own node, and a nest inside that region
// buries it in a member of another family.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Label : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Call : cursiv::Extends<cursiv::AstNode> {
  Expr* callee = nullptr;
  string_view arg;
};
struct Root : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* item = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Root
      : rule<create<ast::Root>(
            assign<&ast::Root::item>(create<ast::Label>(
                assign<&ast::Label::name>(ref<ID>) +
                nest<ast::Call, &ast::Call::callee>(
                    lit<"("> + assign<&ast::Call::arg>(ref<ID>) + lit<")">))) +
            lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
