// A list of nodes by value: a node is built in the tree and never copied into
// a list, whether or not a directive writes the member.
// expect LR: an AST list member holds nodes by pointer
#pragma once

#include "refusals/head.hpp"

namespace refuse {

namespace byvalue {
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item> items;
};
}  // namespace byvalue

struct G : Base {
  struct Root : rule<create<byvalue::Doc>(ref<ID>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
