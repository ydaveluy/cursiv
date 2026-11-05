// A temporary dies at the end of the expression, and the tree would view its text.
// expect LR: this name is a temporary that owns its text
#pragma once

#include <string>

#include "cursiv/parser/Parser.hpp"
#include "refusals/head.hpp"

namespace refuse {

namespace gone {
struct Node : cursiv::Extends<cursiv::AstNode> {};
inline auto read(const cursiv::Parser& parser) {
  return parser.parse(std::string("text"));
}
}  // namespace gone
struct G : Base {
  struct Root : rule<create<gone::Node>(kw<"x">)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
