#pragma once
#include "refusals/head.hpp"
namespace refuse {
struct G : Base {
  struct Data : rule<lit<"END">> {};
  struct Bad : terminal<through(lit<"[">, ref<Data>)> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<Bad>))> {};
  using entry = Root;
};
}  // namespace refuse
