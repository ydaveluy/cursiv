// `A -> B`, `B -> A`: the reduction derives itself and never stops.
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
