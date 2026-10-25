#pragma once
#include "refusals/head.hpp"
namespace refuse {
struct Tag {};
struct G : Base {
  struct Helper : terminal<mark<Tag>(repeat<1>(lit<"#">)) + same<Tag>> {};
  struct OUTER : terminal<ref<Helper> + same<Tag>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<OUTER>))> {
  };
  using entry = Root;
};
}  // namespace refuse
