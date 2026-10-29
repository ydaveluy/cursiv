// A generic lambda given to `as` could read the text and the parts: its
// parameter type has to say which.
// expect LR: as<f>.e.: f accepts more than one kind of input
#pragma once

#include "refusals/head.hpp"

namespace refuse {
inline constexpr auto count = [](auto in) { return in.size(); };
struct G : Base {
  struct Count : rule<as<count>(some(ref<ID>))> {};
  struct Root : rule<create<ast::Node>(ref<Count>)> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace refuse
