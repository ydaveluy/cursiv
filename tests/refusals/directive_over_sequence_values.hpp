// A directive over a sequence: only a node travels through one, so a member
// that also takes values would never receive the value a rule inside pays.
#pragma once

#include <cstdint>
#include <variant>

#include "refusals/head.hpp"

namespace refuse {

namespace ast {
struct List : cursiv::Extends<cursiv::AstNode> {
  vector<std::variant<std::monostate, std::int64_t, Node*>> items;
};
}  // namespace ast

struct G : Base {
  struct NUM : terminal<some(d), std::int64_t> {};
  struct NL : terminal<lit<"\n">> {};
  struct Item : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  struct Root
      : rule<create<ast::List>(
            lit<"["> +
            some(append<&ast::List::items>(many(ref<NL>) + (ref<NUM> | ref<Item>) +
                                           many(ref<NL>)),
                 lit<",">) +
            lit<"]">)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
