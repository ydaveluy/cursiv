// In a terminal, an alternative with its own `as` reads words: a comparison
// with the token's text tells which one it is.
#pragma once

#include "refusals/head.hpp"

namespace refuse {
enum class Count { some, none };
struct G : Base {
  struct W : terminal<as<Count::some>(some(d)) | as<Count::none>(kw<"none">)> {
  };
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<W>))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
