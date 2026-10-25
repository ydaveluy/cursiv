#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>

#include "arithmetics/core/ast.hpp"

// What each `Evaluation` of a module comes to. The walk follows the linked
// reference of a call, so what a name means here is what the linker decided --
// the interpreter never resolves a second time.

namespace arith {

using EvaluationResult = std::unordered_map<const ast::Evaluation*, double>;

// Evaluations that throw -- a division by zero, a recursive call -- are left
// out rather than aborting the module: the others are still answers.
[[nodiscard]] EvaluationResult interpret_evaluations(const ast::Module& module);

// One expression, standing on its own with no parameter bound. `nullopt` is
// every failure, never an exception. `stepLimit` bounds visited nodes (0 is
// unlimited), so a caller wanting only cheap answers pays a bounded cost.
[[nodiscard]] std::optional<double> eval_expression(
    const ast::Expression& expression, std::size_t stepLimit = 0);

}  // namespace arith
