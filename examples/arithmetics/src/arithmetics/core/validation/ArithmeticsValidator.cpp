#include <arithmetics/core/validation/ArithmeticsValidator.hpp>

#include <arithmetics/core/Operator.hpp>
#include <arithmetics/core/ast.hpp>

#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <pegium/core/services/JsonValue.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>

namespace arithmetics::validation {
namespace {

using namespace arithmetics::ast;

const Definition *as_definition(const pegium::AstNode *node) {
  return pegium::ast_ptr_cast<const Definition>(node);
}

const Definition *resolved_function_definition(const FunctionCall &call) {
  return as_definition(call.func.get());
}

std::optional<double> evaluate_constant_expression(const Expression &expression) {
  if (const auto *number = pegium::ast_ptr_cast<const NumberLiteral>(&expression)) {
    return number->value;
  }

  if (const auto *grouped = pegium::ast_ptr_cast<const GroupedExpression>(&expression)) {
    if (!grouped->expression) {
      return std::nullopt;
    }
    return evaluate_constant_expression(*grouped->expression);
  }

  const auto *binary = pegium::ast_ptr_cast<const BinaryExpression>(&expression);
  if (binary == nullptr || !binary->left || !binary->right) {
    return std::nullopt;
  }

  const auto left = evaluate_constant_expression(*binary->left);
  const auto right = evaluate_constant_expression(*binary->right);
  if (!left.has_value() || !right.has_value()) {
    return std::nullopt;
  }

  if ((binary->op == "/" || binary->op == "%") && *right == 0.0) {
    return std::nullopt;
  }

  return apply_operator(binary->op)(*left, *right);
}

std::optional<double> resolve_normalizable_value(
    const Expression &expression,
    const std::unordered_map<const Expression *, double> &context) {
  if (const auto *number = pegium::ast_ptr_cast<const NumberLiteral>(&expression)) {
    return number->value;
  }

  if (const auto *grouped = pegium::ast_ptr_cast<const GroupedExpression>(&expression)) {
    if (!grouped->expression) {
      return std::nullopt;
    }
    return resolve_normalizable_value(*grouped->expression, context);
  }

  if (const auto it = context.find(&expression); it != context.end()) {
    return it->second;
  }

  return std::nullopt;
}

void erase_normalizable_value(
    const Expression *expression,
    std::unordered_map<const Expression *, double> &context) {
  if (expression == nullptr) {
    return;
  }

  if (const auto *grouped = pegium::ast_ptr_cast<const GroupedExpression>(expression)) {
    erase_normalizable_value(grouped->expression, context);
  }

  context.erase(expression);
}

void evaluate_normalizable_expressions(
    const Expression &expression,
    std::unordered_map<const Expression *, double> &context,
    std::vector<const Expression *> &order) {
  if (const auto *grouped = pegium::ast_ptr_cast<const GroupedExpression>(&expression)) {
    if (grouped->expression) {
      evaluate_normalizable_expressions(*grouped->expression, context, order);
    }
    return;
  }

  const auto *binary = pegium::ast_ptr_cast<const BinaryExpression>(&expression);
  if (binary == nullptr || !binary->left || !binary->right) {
    return;
  }

  evaluate_normalizable_expressions(*binary->left, context, order);
  evaluate_normalizable_expressions(*binary->right, context, order);

  const auto left = resolve_normalizable_value(*binary->left, context);
  const auto right = resolve_normalizable_value(*binary->right, context);
  if (!left.has_value() || !right.has_value()) {
    return;
  }

  double result = 0.0;
  try {
    result = apply_operator(binary->op)(*left, *right);
  } catch (const std::runtime_error &) {
    return;
  }

  if (!std::isfinite(result) ||
      std::format("{}", result).size() > 8u) {
    return;
  }

  const auto [it, inserted] = context.try_emplace(&expression, result);
  if (inserted) {
    order.push_back(&expression);
  } else {
    it->second = result;
  }

  erase_normalizable_value(binary->left, context);
  erase_normalizable_value(binary->right, context);
}

template <typename Node>
std::unordered_map<std::string_view, std::vector<const Node *>>
group_named_nodes(std::span<Node *const> nodes) {
  std::unordered_map<std::string_view, std::vector<const Node *>> groups;
  groups.reserve(nodes.size());

  for (const auto *node : nodes) {
    if (node == nullptr || node->name.empty()) {
      continue;
    }
    groups[node->name].push_back(node);
  }

  return groups;
}

struct NestedFunctionCall {
  const FunctionCall *call = nullptr;
  const Definition *host = nullptr;
};

using FunctionCallTree = std::unordered_map<const FunctionCall *, NestedFunctionCall>;
using FunctionCallCycle = std::vector<NestedFunctionCall>;

void collect_nested_function_calls(const Definition &host,
                                   const Expression *expression,
                                   std::vector<NestedFunctionCall> &calls) {
  if (expression == nullptr) {
    return;
  }

  if (const auto *grouped = pegium::ast_ptr_cast<const GroupedExpression>(expression)) {
    collect_nested_function_calls(host, grouped->expression, calls);
    return;
  }

  if (const auto *call = pegium::ast_ptr_cast<const FunctionCall>(expression)) {
    if (resolved_function_definition(*call) != nullptr) {
      calls.push_back({.call = call, .host = &host});
    }
    return;
  }

  if (const auto *binary = pegium::ast_ptr_cast<const BinaryExpression>(expression)) {
    collect_nested_function_calls(host, binary->left, calls);
    collect_nested_function_calls(host, binary->right, calls);
  }
}

std::vector<NestedFunctionCall> select_nested_function_calls(
    const Definition &host) {
  std::vector<NestedFunctionCall> calls;
  collect_nested_function_calls(host, host.expr, calls);
  return calls;
}

std::vector<NestedFunctionCall> get_not_traversed_nested_calls(
    const Definition &host,
    std::unordered_set<const Definition *> &traversedFunctions) {
  if (!traversedFunctions.insert(&host).second) {
    return {};
  }
  return select_nested_function_calls(host);
}

std::optional<FunctionCallCycle>
select_function_call_cycle(const NestedFunctionCall &to,
                           const FunctionCallTree &tree) {
  const auto *referencedFunction = resolved_function_definition(*to.call);
  if (referencedFunction == nullptr) {
    return std::nullopt;
  }

  auto parentIt = tree.find(to.call);
  while (parentIt != tree.end()) {
    if (parentIt->second.host == referencedFunction) {
      return FunctionCallCycle{parentIt->second, to};
    }
    parentIt = tree.find(parentIt->second.call);
  }

  return std::nullopt;
}

std::vector<NestedFunctionCall> iterate_back(const FunctionCallCycle &cycle,
                                             const FunctionCallTree &tree) {
  std::vector<NestedFunctionCall> path;
  if (cycle.empty()) {
    return path;
  }

  const auto &start = cycle.front();
  const auto &end = cycle.back();
  path.push_back(end);
  if (start.call == end.call) {
    return path;
  }

  auto parentIt = tree.find(end.call);
  while (parentIt != tree.end()) {
    path.push_back(parentIt->second);
    if (parentIt->second.call == start.call) {
      break;
    }
    parentIt = tree.find(parentIt->second.call);
  }

  return path;
}

std::string to_string(const NestedFunctionCall &call) {
  const auto *definition = resolved_function_definition(*call.call);
  return definition == nullptr ? std::string{} : std::format("{}()", definition->name);
}

std::string print_cycle(const FunctionCallCycle &cycle,
                        const FunctionCallTree &tree) {
  const auto path = iterate_back(cycle, tree);
  if (path.empty()) {
    return {};
  }

  std::string text = to_string(path.front());
  for (std::size_t index = 1; index < path.size(); ++index) {
    text = to_string(path[index]) + "->" + text;
  }
  return text;
}

template <typename Builder>
Builder &apply_arguments_range(Builder &builder, const FunctionCall &call) {
  if (call.args.empty()) {
    return builder;
  }

  const auto [firstBegin, firstEndIgnored] =
      pegium::validation::range_for_feature(call, "args", 0u);
  (void)firstEndIgnored;
  const auto [lastBeginIgnored, lastEnd] = pegium::validation::range_for_feature(
      call, "args", call.args.size() - 1u);
  (void)lastBeginIgnored;
  return builder.range(firstBegin, lastEnd);
}

} // namespace

void ArithmeticsValidator::checkDivByZero(
    const BinaryExpression &expression,
    const pegium::validation::ValidationAcceptor &accept) const {
  if ((expression.op == "/" || expression.op == "%") && expression.right &&
      evaluate_constant_expression(*expression.right) == 0.0) {
    accept.error(expression, "Division by zero is detected.")
        .property<&BinaryExpression::right>();
  }
}

void ArithmeticsValidator::checkUniqueParameters(
    const Definition &definition,
    const pegium::validation::ValidationAcceptor &accept) const {
  const auto params = group_named_nodes<DeclaredParameter>(std::span(definition.args));
  for (const auto &[name, symbols] : params) {
    if (symbols.size() <= 1u) {
      continue;
    }

    for (const auto *symbol : symbols) {
      accept.error(*symbol, std::format("Duplicate definition name: {}", name))
          .property<&DeclaredParameter::name>();
    }
  }
}

void ArithmeticsValidator::checkNormalizable(
    const Definition &definition,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (!definition.expr ||
      pegium::ast_ptr_cast<const NumberLiteral>(definition.expr) != nullptr) {
    return;
  }

  std::unordered_map<const Expression *, double> context;
  std::vector<const Expression *> order;
  evaluate_normalizable_expressions(*definition.expr, context, order);

  for (const auto *expression : order) {
    const auto it = context.find(expression);
    if (it == context.end() || it->second == 0.0 || !std::isfinite(it->second)) {
      continue;
    }

    pegium::JsonValue::Object payload;
    payload["constant"] = pegium::JsonValue(it->second);
    accept
        .info(*expression, std::format(
                               "Expression could be normalized to constant {}",
                               it->second))
        .code(IssueCodes::ExpressionNormalizable)
        .data(pegium::JsonValue(std::move(payload)));
  }
}

void ArithmeticsValidator::checkUniqueDefinitions(
    const Module &module,
    const pegium::validation::ValidationAcceptor &accept) const {
  std::unordered_map<std::string_view, std::vector<const Definition *>> names;
  names.reserve(module.statements.size());

  for (const auto &statement : module.statements) {
    const auto *definition = pegium::ast_ptr_cast<const Definition>(statement);
    if (definition == nullptr || definition->name.empty()) {
      continue;
    }
    names[definition->name].push_back(definition);
  }

  for (const auto &[name, symbols] : names) {
    if (symbols.size() <= 1u) {
      continue;
    }

    for (const auto *symbol : symbols) {
      accept.error(*symbol, std::format("Duplicate definition name: {}", name))
          .property<&Definition::name>();
    }
  }
}

void ArithmeticsValidator::checkFunctionRecursion(
    const Module &module,
    const pegium::validation::ValidationAcceptor &accept) const {
  std::unordered_set<const Definition *> traversedFunctions;
  FunctionCallTree callsTree;
  std::vector<FunctionCallCycle> callCycles;

  for (const auto &statement : module.statements) {
    const auto *definition = pegium::ast_ptr_cast<const Definition>(statement);
    if (definition == nullptr) {
      continue;
    }

    auto remainingCalls =
        get_not_traversed_nested_calls(*definition, traversedFunctions);
    while (!remainingCalls.empty()) {
      std::vector<NestedFunctionCall> nextCalls;
      for (const auto &parent : remainingCalls) {
        const auto *referencedFunction = resolved_function_definition(*parent.call);
        if (referencedFunction == nullptr) {
          continue;
        }

        if (parent.host == referencedFunction) {
          callCycles.push_back({parent});
        }

        for (const auto &child :
             get_not_traversed_nested_calls(*referencedFunction,
                                           traversedFunctions)) {
          callsTree[child.call] = parent;
          if (auto cycle = select_function_call_cycle(child, callsTree);
              cycle.has_value()) {
            callCycles.push_back(std::move(*cycle));
          } else {
            nextCalls.push_back(child);
          }
        }
      }
      remainingCalls = std::move(nextCalls);
    }
  }

  for (const auto &cycle : callCycles) {
    const auto cycleMessage = print_cycle(cycle, callsTree);
    for (const auto &entry : iterate_back(cycle, callsTree)) {
      accept
          .error(*entry.call,
                 std::format("Recursion is not allowed [{}]", cycleMessage))
          .property<&FunctionCall::func>();
    }
  }
}

void ArithmeticsValidator::checkMatchingParameters(
    const FunctionCall &call,
    const pegium::validation::ValidationAcceptor &accept) const {
  const auto *definition = resolved_function_definition(call);
  if (definition == nullptr) {
    return;
  }

  if (definition->args.size() != call.args.size()) {
    auto diagnostic = accept.error(
        call, std::format("Function {} expects {} parameters, but {} were given.",
                          definition->name, definition->args.size(),
                          call.args.size()));
    apply_arguments_range(diagnostic, call);
  }
}

} // namespace arithmetics::validation
