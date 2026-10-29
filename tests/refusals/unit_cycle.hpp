// `A -> B`, `B -> A`: the reduction derives itself and never stops.
// expect LR: refused .unitProductionCycle..*rule .A. stands on a cycle of reductions that read no text.*cycle: .A. -> .B. -> .A.
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
