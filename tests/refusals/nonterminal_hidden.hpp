#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct BadHidden : rule<lit<"#">> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = BadHidden;
};
}  // namespace refuse
