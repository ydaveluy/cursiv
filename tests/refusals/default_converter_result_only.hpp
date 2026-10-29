// A `default_converter` announcing a `result` the tree's own conversion cannot
// produce: `result` names a target `default_convert` already reaches, and a
// target of one's own is reached by a `convert` on the same specialisation.
#pragma once

#include "refusals/head.hpp"

namespace refuse::own {
struct Percent {
  int v = 0;
};
}  // namespace refuse::own

template <>
struct cursiv::default_converter<refuse::own::Percent> {
  using result = refuse::own::Percent;
};

namespace refuse {

namespace own {
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Percent share;
};
}  // namespace own
struct G : Base {
  struct PCT : terminal<as<own::Percent>(some(d))> {};
  struct Root
      : rule<create<own::Holder>(assign<&own::Holder::share>(ref<PCT>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
