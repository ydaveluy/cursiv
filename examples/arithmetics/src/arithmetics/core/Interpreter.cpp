#include "arithmetics/core/Interpreter.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "arithmetics/core/Operator.hpp"

namespace arith {

namespace {

struct Context {
  // A parameter bound by the call being evaluated.
  std::unordered_map<const ast::AbstractDefinition*, double> bound;
  // The definitions on the stack, so a cycle answers nothing instead of
  // recursing.
  std::vector<const ast::Definition*> active;
  // Steps left before the evaluation gives up, null for unlimited.
  std::size_t* remaining = nullptr;
};

// No exception on this path: a validator asks thousands of times and most
// questions have no answer -- an unwind per miss would dwarf the arithmetic.
// `nullopt` is every failure: an unbound name, a cycle, a spent budget.
std::optional<double> evaluate(const ast::Expression& expression,
                               const Context& context);

std::optional<double> evaluate_call(const ast::FunctionCall& call,
                                    const Context& context) {
  const ast::AbstractDefinition* const target = call.func.get();
  if (target == nullptr) return std::nullopt;

  // A parameter used as a value: the call carries no argument and the name
  // stands for what the enclosing call bound.
  if (const auto found = context.bound.find(target);
      found != context.bound.end())
    return found->second;

  if (target->type != &cursiv::ast_type_tag<ast::Definition>)
    return std::nullopt;
  const auto& definition = static_cast<const ast::Definition&>(*target);
  if (definition.args.size() != call.args.size()) return std::nullopt;
  if (std::ranges::find(context.active, &definition) != context.active.end())
    return std::nullopt;
  if (definition.expr == nullptr) return std::nullopt;

  // The arguments are evaluated in the caller's context, the body in the
  // callee's: a parameter of the same name does not leak between the two.
  Context local;
  local.active = context.active;
  local.active.push_back(&definition);
  local.remaining = context.remaining;
  for (std::size_t at = 0; at < definition.args.size(); ++at) {
    if (definition.args[at] == nullptr || call.args[at] == nullptr)
      return std::nullopt;
    const std::optional<double> argument = evaluate(*call.args[at], context);
    if (!argument.has_value()) return std::nullopt;
    local.bound[definition.args[at]] = *argument;
  }
  return evaluate(*definition.expr, local);
}

std::optional<double> evaluate(const ast::Expression& expression,
                               const Context& context) {
  if (context.remaining != nullptr) {
    if (*context.remaining == 0) return std::nullopt;
    --*context.remaining;
  }

  if (expression.type == &cursiv::ast_type_tag<ast::NumberLiteral>)
    return static_cast<const ast::NumberLiteral&>(expression).value;

  if (expression.type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
    const ast::Expression* const inner =
        static_cast<const ast::GroupedExpression&>(expression).expression;
    if (inner == nullptr) return std::nullopt;
    return evaluate(*inner, context);
  }

  if (expression.type == &cursiv::ast_type_tag<ast::BinaryExpression>) {
    const auto& binary = static_cast<const ast::BinaryExpression&>(expression);
    if (binary.left == nullptr || binary.right == nullptr) return std::nullopt;
    const std::optional<double> left = evaluate(*binary.left, context);
    if (!left.has_value()) return std::nullopt;
    const std::optional<double> right = evaluate(*binary.right, context);
    if (!right.has_value()) return std::nullopt;
    // The operator table throws on a zero divisor; on this path that is an
    // answerless question, not an unwind.
    if ((binary.op == ast::Operator::divide ||
         binary.op == ast::Operator::modulo) &&
        *right == 0.0)
      return std::nullopt;
    return apply_operator(binary.op)(*left, *right);
  }

  if (expression.type == &cursiv::ast_type_tag<ast::FunctionCall>)
    return evaluate_call(static_cast<const ast::FunctionCall&>(expression),
                         context);

  return std::nullopt;
}

}  // namespace

std::optional<double> eval_expression(const ast::Expression& expression,
                                      std::size_t stepLimit) {
  Context empty;
  std::size_t steps = stepLimit;
  if (stepLimit != 0) empty.remaining = &steps;
  return evaluate(expression, empty);
}

EvaluationResult interpret_evaluations(const ast::Module& module) {
  EvaluationResult results;
  const Context empty;
  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr ||
        statement->type != &cursiv::ast_type_tag<ast::Evaluation>)
      continue;
    const auto& evaluation = static_cast<const ast::Evaluation&>(*statement);
    if (evaluation.expression == nullptr) continue;
    // Left out when it answers nothing: the module's other evaluations are
    // still answers.
    if (const std::optional<double> value =
            evaluate(*evaluation.expression, empty);
        value.has_value())
      results.emplace(&evaluation, *value);
  }
  return results;
}

}  // namespace arith
