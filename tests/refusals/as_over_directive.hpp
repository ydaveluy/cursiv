// `as` gives a value to text: a directive inside it would write the AST.
#pragma once

#include "refusals/head.hpp"

namespace refuse {
enum class Mode { on, off };
struct G : Base {
  struct Root : rule<create<ast::Node>(
                    as<Mode::on>(assign<&ast::Node::name>(ref<ID>)))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
