#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Root
      : rule<create<ast::Node>(lit<"a">) & create<ast::Node>(lit<"b">)> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace refuse
