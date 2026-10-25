// A repetition that may match nothing, written in a helper that keeps its
// call, since it captures: the refusal names the helper, not its caller.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct Run {};

struct G : Base {
  struct REST;
  struct LOOP : terminal<lit<"a"> + ref<REST> + option(ref<LOOP>)> {};
  struct REST : terminal<mark<Run>(some(lit<"c">)) + many(option(lit<"b">)) +
                         same<Run>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<LOOP>))> {};
  using entry = Root;
};

}  // namespace refuse
