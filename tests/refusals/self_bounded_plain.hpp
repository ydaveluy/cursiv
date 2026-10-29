// Arms that begin and end with their own rule, in rules that build no node:
// an operator between, nothing between, a mixfix. The order of the arms does
// not matter.
// expect LR LL: refused .selfBoundedArm..*rule .Sum.: the alternative .* begins and ends with .Sum., so it does not say how its operands group.*rule .Pair.: the alternative .* begins and ends with .Pair.*rule .Cond.: the alternative .* begins and ends with .Cond.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct NUM : terminal<some(d)> {};
  struct Sum : rule<ref<NUM> | (ref<Sum> + lit<"+"> + ref<Sum>)> {};
  struct Pair : rule<(ref<Pair> + ref<Pair>) | ref<NUM>> {};
  struct Cond : rule<(ref<Cond> + lit<"?"> + ref<Cond> + lit<":"> + ref<Cond>) |
                     ref<NUM>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(
                         ref<Sum> | ref<Pair> | ref<Cond>)) +
                     eof> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
