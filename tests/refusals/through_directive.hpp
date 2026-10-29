// expect LR: is inside a through
#pragma once
#include "refusals/head.hpp"
namespace refuse {
struct G : Base {
  struct Root : rule<create<ast::Node>(
                    through(lit<"<">, assign<&ast::Node::name>(lit<">">)))> {};
  using entry = Root;
};
}  // namespace refuse
