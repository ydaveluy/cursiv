// expect LR: echoes a mark<> not set in its scope
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Tag {};

struct G : Base {
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(
                    through(lit<"[">, lit<"]"> + same<Tag> + lit<"]">)))> {};
  using entry = Root;
};

}  // namespace refuse
