// expect LR: refused: terminal .OUTER. echoes a mark<> not set in its scope
#pragma once
#include "refusals/head.hpp"
namespace refuse {
struct Tag {};
struct G : Base {
  struct OUTER
      : terminal<&(mark<Tag>(repeat<1>(lit<"#">)) + same<Tag>)+lit<"#"> +
                 same<Tag>> {};
  struct Root : rule<create<ast::Node>(assign<&ast::Node::name>(ref<OUTER>))> {
  };
  using entry = Root;
};
}  // namespace refuse
