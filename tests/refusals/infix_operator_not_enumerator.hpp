// An infix operator is a bare literal written into an enum member: the
// spelling `+` names no enumerator, so every operator read would be a
// conversion diagnostic.
// expect LR: [ '‘]EnumLiteral.*refuse::ast::Bin::op.*[+].*refuse::Op::add.*refuse::Op::mul
#pragma once

#include <cstdint>

#include "refusals/head.hpp"

namespace refuse {

enum class Op : std::uint8_t { add, mul };

namespace ast {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expr> {
  string_view name;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  Op op = Op::mul;
  Expr* right = nullptr;
};
}  // namespace ast

struct G : Base {
  struct Primary : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Sum : infix<Primary, ast::Bin, (left(lit<"+">))> {};
  using entry = Sum;
  using ignored = WS;
};

}  // namespace refuse
