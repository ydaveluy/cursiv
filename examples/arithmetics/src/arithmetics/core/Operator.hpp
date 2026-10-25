#pragma once

#include <cmath>
#include <stdexcept>

#include "arithmetics/core/ast.hpp"

// What an operator does, in one table. Division and modulo by zero throw rather
// than answering an infinity: a caller either reports it as a diagnostic or
// refuses to fold, and neither wants a NaN travelling silently.

namespace arith {

using BinaryOperator = double (*)(double, double);

inline double add_operator(double left, double right) noexcept {
  return left + right;
}
inline double subtract_operator(double left, double right) noexcept {
  return left - right;
}
inline double multiply_operator(double left, double right) noexcept {
  return left * right;
}
inline double exponent_operator(double left, double right) {
  return std::pow(left, right);
}
inline double modulo_operator(double left, double right) {
  if (right == 0.0) throw std::runtime_error("Modulo by zero");
  return std::fmod(left, right);
}
inline double divide_operator(double left, double right) {
  if (right == 0.0) throw std::runtime_error("Division by zero");
  return left / right;
}

[[nodiscard]] inline BinaryOperator apply_operator(ast::Operator op) {
  switch (op) {
    case ast::Operator::add:
      return &add_operator;
    case ast::Operator::subtract:
      return &subtract_operator;
    case ast::Operator::multiply:
      return &multiply_operator;
    case ast::Operator::divide:
      return &divide_operator;
    case ast::Operator::exponent:
      return &exponent_operator;
    case ast::Operator::modulo:
      return &modulo_operator;
  }
  throw std::runtime_error("Unknown operator");
}

}  // namespace arith
