#pragma once

#include <pegium/core/syntax-tree/AstNode.hpp>

namespace pegium::test {

struct ValidationNodeA : pegium::AstNode {};
struct ValidationNodeB : pegium::AstNode {};
struct ValidationRefNode : pegium::AstNode {
  reference<ValidationNodeA> ref;
};

} // namespace pegium::test
