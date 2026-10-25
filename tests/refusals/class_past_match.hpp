// A class of scalars refused where a match may end one byte into one of
// its scalars: the rest of that scalar is text past the token.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NUM : terminal<some(cr<"0-9">) + !uc::Lo + option(lit<"\xe4">)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<NUM>) +
                                     many(ref<NUM> | ref<ID>))> {};
  using entry = Root;
};

}  // namespace refuse
