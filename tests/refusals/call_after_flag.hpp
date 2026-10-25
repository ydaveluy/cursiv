// A flag written ahead of a call is a write: the called node is not adopted
// over it. A fragment spelled in each alternative is how the flag is shared.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Decl : cursiv::Extends<cursiv::AstNode> {
  boolean exported;
  string_view name;
};
struct Fn : cursiv::Extends<Decl> {};
}  // namespace ast
struct G : Base {
  struct Body
      : rule<create<ast::Fn>(kw<"fn"> + assign<&ast::Decl::name>(ref<ID>))> {};
  struct Root
      : rule<option(flag<&ast::Decl::exported>(kw<"export">)) + ref<Body>> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
