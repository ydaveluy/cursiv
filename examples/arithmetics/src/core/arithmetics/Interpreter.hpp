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

// Answers for the expressions of one module, each standing on its own with no
// parameter bound. It keeps the value of each definition without parameters
// it computes, so `d2: d1 + d1` computes `d1` once and later unbounded
// questions reuse it; a module changed since is asked through a new one. It
// also keeps its working memory, so asking many questions of one interpreter
// allocates little.
class Interpreter {
 public:
  Interpreter();
  ~Interpreter();
  Interpreter(const Interpreter&) = delete;
  Interpreter& operator=(const Interpreter&) = delete;

  // `nullopt` is every failure, never an exception. `stepLimit` bounds the
  // nodes this question visits (0 is unlimited), so a caller wanting only
  // cheap answers pays a bounded cost. A bounded question reuses no value an
  // earlier one computed: its answer depends on its own expression only.
  [[nodiscard]] std::optional<double> evaluate(
      const ast::Expression& expression, std::size_t stepLimit = 0);

 private:
  class Machine;
  std::unique_ptr<Machine> _machine;
};

}  // namespace arith
