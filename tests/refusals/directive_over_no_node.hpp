// A directive over a choice applies to each alternative: a member that takes a
// node refuses an alternative that may hold none.
// expect LR at compile: applies to an expression that can match without a node
#pragma once

#include <cstdint>
#include <variant>

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  variant<std::monostate, int64, Node*> v;
};
}  // namespace ast

struct G : Base {
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Holder>(
                    lit<"v"> + assign<&ast::Holder::v>(ref<NUM> | ref<Item> |
                                                       option(lit<"[">)))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
