// A directive over a choice distributes, and a member that takes a node is
// refused an alternative that holds none, as a text member is refused one
// that pays no value.
#pragma once

#include <cstdint>
#include <variant>

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  std::variant<std::monostate, std::int64_t, Node*> v;
};
}  // namespace ast

struct G : Base {
  struct NUM : terminal<some(d), std::int64_t> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root : rule<create<ast::Holder>(
                    lit<"v"> + assign<&ast::Holder::v>(ref<NUM> | ref<Item> |
                                                       option(lit<"[">)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
