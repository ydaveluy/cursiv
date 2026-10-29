// A sequence of keywords into an enum member: the joined spelling `NOTIN`
// names no enumerator, so every read of it would be a conversion diagnostic.
// expect LR: [ '‘]EnumLiteral.*refuse::ast::Bin::op.*NOTIN.*refuse::Op::add.*refuse::Op::mul
#pragma once

#include <cstdint>

#include "refusals/head.hpp"

namespace refuse {

enum class Op : std::uint8_t { add, mul };

namespace ast {
struct Bin : cursiv::Extends<cursiv::AstNode> {
  Op op = Op::mul;
};
}  // namespace ast

struct G : Base {
  struct Root
      : rule<create<ast::Bin>(assign<&ast::Bin::op>(kw<"NOT"> + kw<"IN">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
