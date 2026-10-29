// `A -> B`, `B -> A`: the reduction derives itself and never stops.
// expect LR: refused .unitProductionCycle..*rule .A. is part of a cycle of rules that call each other without matching any text.*cycle: .A. -> .B. -> .A.
// expect LL: refused .leftRecursion..*rule .A. reaches itself before matching anything.*cycle: .A. -> .B. -> .A.
#pragma once

#include "refusals/head.hpp"

namespace refuse {

struct G : Base {
  struct A;
  struct B : rule<ref<A>> {};
  struct A
      : rule<ref<B> | create<ast::Node>(assign<&ast::Node::name>(ref<ID>))> {};
  using entry = A;
  using ignored = WS;
};

}  // namespace refuse
