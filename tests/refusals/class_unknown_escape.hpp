// A class escapes as std::regex does: a backslash before a letter it gives
// no meaning is no escape, and the spelling does not compile.
#pragma once

#include "refusals/head.hpp"

namespace refuse {
struct G : Base {
  struct NAME : terminal<some(cr<R"(a-z\q)">)> {};
  struct Root : rule<create<ast::Node>(
      assign<&ast::Node::name>(ref<NAME>))> {};
  using entry = Root;
  using ignore = WS;
};
}  // namespace refuse
