// An `as` directly around another: the outer value would hide the inner, so
// the two conversions are one function.
// expect LR: as<X>.as<Y>.e.. is not allowed: combine the two conversions in one function
#pragma once

#include "refusals/head.hpp"

namespace refuse {
enum class Mode { on, off };
struct G : Base {
  struct Flag : rule<as<Mode::on>(as<Mode::off>(kw<"on">))> {};
  struct Root : rule<create<ast::Node>(ref<Flag>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
