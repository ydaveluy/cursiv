#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "arithmetics/ast.hpp"

// What each `Evaluation` of a module comes to. The walk follows the linked
// reference of a call, so what a name means here is what the linker decided --
// the interpreter never resolves a second time.

namespace arith {

// Every evaluation of the module, `nullopt` for one with no value -- a zero
// divisor, a recursive call -- rather than aborting the module: the others
// are still answers.
using EvaluationResult =
    std::unordered_map<const ast::Evaluation*, std::optional<double>>;

[[nodiscard]] EvaluationResult interpret_evaluations(const ast::Module& module);

// A value written as JavaScript writes a number: the shortest digits that
// read back as the same value, in plain decimal from 1e-6 up to 1e21 and with
// an exponent beyond, `Infinity` and `NaN` by name.
[[nodiscard]] std::string number_text(double value);

// Answers for the expressions of one module, with no parameter bound. It keeps
// the values of parameterless definitions and its working memory between
// questions, so ask a changed module through a new interpreter.
class Interpreter {
 public:
  Interpreter();
  ~Interpreter();
  Interpreter(const Interpreter&) = delete;
  Interpreter& operator=(const Interpreter&) = delete;

  // `nullopt` on any failure, never an exception. `stepLimit` bounds the nodes
  // visited (0 for no limit); a bounded question reuses no earlier value.
  [[nodiscard]] std::optional<double> evaluate(
      const ast::Expression& expression, std::size_t stepLimit = 0);

 private:
  class Machine;
  std::unique_ptr<Machine> _machine;
};

}  // namespace arith
