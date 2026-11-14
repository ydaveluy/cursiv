// A type tag asked of a class that is only declared: its bases are unknown.
// expect LR at compile: ast_type_tag<T> needs the definition of T
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Later;
}  // namespace ast

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
  static constexpr const cursiv::AstTypeTag* later =
      &cursiv::ast_type_tag<ast::Later>;
};

}  // namespace refuse
