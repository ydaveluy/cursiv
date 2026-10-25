#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct Level {};
  struct Other {};
  struct Group : terminal<lit<"("> + option(ref<Group>) + lit<")">> {};
  struct MARKED : terminal<lit<"<"> + mark<Level>(some(cr<"=">)) + lit<">"> +
                           &same<Other> + ref<Group> + same<Level> + lit<";">> {
  };
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<MARKED>))> {
  };
  using entry = Root;
};

}  // namespace refuse
