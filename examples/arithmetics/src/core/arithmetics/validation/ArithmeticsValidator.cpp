#include "arithmetics/validation/ArithmeticsValidator.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arithmetics/Interpreter.hpp"
#include "arithmetics/Operator.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/workspace/Document.hpp"

namespace arith {

namespace {

const ast::Definition* definition_of(const ast::FunctionCall& call) {
  const cursiv::AstNode* const target = call.func.get();
  return target != nullptr
                 && target->type == &cursiv::ast_type_tag<ast::Definition>
             ? static_cast<const ast::Definition*>(target)
             : nullptr;
}

// Parentheses are transparent to every walk below.
const ast::Expression* unwrap(const ast::Expression* expression) {
  while (expression != nullptr
         && expression->type == &cursiv::ast_type_tag<ast::GroupedExpression>)
    expression =
        static_cast<const ast::GroupedExpression*>(expression)->expression;
  return expression;
}

// The resolved calls a body makes, the calls in their arguments included, in
// source order. Walks on its own stack.
std::vector<const ast::FunctionCall*> select_calls(
    const ast::Definition& host) {
  std::vector<const ast::FunctionCall*> calls;
  std::vector<const ast::Expression*> pending{host.expr};
  while (!pending.empty()) {
    const ast::Expression* const expression = unwrap(pending.back());
    pending.pop_back();
    if (expression == nullptr) continue;
    if (expression->type == &cursiv::ast_type_tag<ast::FunctionCall>) {
      const auto& call = static_cast<const ast::FunctionCall&>(*expression);
      if (definition_of(call) != nullptr) calls.push_back(&call);
      pending.insert(pending.end(), call.args.rbegin(), call.args.rend());
    } else if (expression->type
               == &cursiv::ast_type_tag<ast::BinaryExpression>) {
      const auto& binary =
          static_cast<const ast::BinaryExpression&>(*expression);
      pending.push_back(binary.right);
      pending.push_back(binary.left);
    }
  }
  return calls;
}

// `f()->g()->f()`: the definitions a cycle's calls enter, in call order.
std::string print_cycle(std::span<const ast::FunctionCall* const> cycle) {
  std::string path;
  for (const ast::FunctionCall* const call : cycle) {
    if (!path.empty()) path += "->";
    path += definition_of(*call)->name;
    path += "()";
  }
  return path;
}

// The value a sub-expression already stands for: its own if it is a literal,
// the folded one if `fold` produced it.
std::optional<double> known(
    const ast::Expression* expression,
    const std::unordered_map<const ast::Expression*, double>& folded) {
  expression = unwrap(expression);
  if (expression == nullptr) return std::nullopt;
  if (expression->type == &cursiv::ast_type_tag<ast::NumberLiteral>)
    return static_cast<const ast::NumberLiteral*>(expression)->value;
  const auto found = folded.find(expression);
  return found == folded.end() ? std::nullopt
                               : std::optional<double>(found->second);
}

// The binary expressions that fold to a number, bottom up. A parent that folds
// replaces its children, so a span gets one rewrite, and a constant longer
// than eight characters is not kept.
void fold(const ast::Expression& root,
          std::unordered_map<const ast::Expression*, double>& folded,
          std::vector<const ast::Expression*>& order) {
  std::vector<std::pair<const ast::Expression*, bool>> pending{{&root, false}};
  while (!pending.empty()) {
    const auto [node, operandsDone] = pending.back();
    pending.pop_back();
    const ast::Expression* const expression = unwrap(node);
    if (expression == nullptr
        || expression->type != &cursiv::ast_type_tag<ast::BinaryExpression>)
      continue;
    const auto& binary = static_cast<const ast::BinaryExpression&>(*expression);
    if (binary.left == nullptr || binary.right == nullptr) continue;
    if (!operandsDone) {
      pending.emplace_back(expression, true);
      pending.emplace_back(binary.right, false);
      pending.emplace_back(binary.left, false);
      continue;
    }

    const std::optional<double> left = known(binary.left, folded);
    const std::optional<double> right = known(binary.right, folded);
    if (!left.has_value() || !right.has_value()) continue;
    double value = 0.0;
    try {
      value = apply_operator(binary.op)(*left, *right);
    } catch (const std::runtime_error&) {
      continue;  // a division by zero is the other check's to report
    }
    if (!std::isfinite(value) || number_text(value).size() > 8) continue;

    if (folded.try_emplace(expression, value).second)
      order.push_back(expression);
    if (const ast::Expression* const child = unwrap(binary.left))
      folded.erase(child);
    if (const ast::Expression* const child = unwrap(binary.right))
      folded.erase(child);
  }
}

}  // namespace

namespace {

// The divisor is evaluated, so a call worth zero is a division by zero too,
// within a few steps per divisor.
constexpr std::size_t kDivisorStepLimit = 64;

void say_if_zero(const ast::BinaryExpression& binary, Interpreter& interpreter,
                 cursiv::ValidationAcceptor& accept) {
  if ((binary.op != ast::Operator::divide && binary.op != ast::Operator::modulo)
      || binary.right == nullptr)
    return;
  // An unbound name, a cycle, a step limit reached: nullopt, not this check's
  // answer.
  const std::optional<double> divisor =
      interpreter.evaluate(*binary.right, kDivisorStepLimit);
  if (!divisor.has_value() || *divisor != 0.0) return;
  accept.error(binary, "Division by zero is detected.")
      .property<&ast::BinaryExpression::right>();
}

// Source order, on its own stack: an expression a million terms long holds no
// native frame per term. `pending` is that stack, empty again on return, so
// one serves every statement.
void walk_divisions(const ast::Expression* root,
                    std::vector<const ast::Expression*>& pending,
                    Interpreter& interpreter,
                    cursiv::ValidationAcceptor& accept,
                    const cursiv::CancellationToken& token) {
  const ast::Expression* expression = root;
  for (;;) {
    while (expression == nullptr) {
      if (pending.empty()) return;
      expression = pending.back();
      pending.pop_back();
    }
    cursiv::throw_if_cancelled(token);
    if (expression->type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
      expression =
          static_cast<const ast::GroupedExpression&>(*expression).expression;
    } else if (expression->type
               == &cursiv::ast_type_tag<ast::BinaryExpression>) {
      const auto& binary =
          static_cast<const ast::BinaryExpression&>(*expression);
      say_if_zero(binary, interpreter, accept);
      pending.push_back(binary.right);
      expression = binary.left;
    } else if (expression->type == &cursiv::ast_type_tag<ast::FunctionCall>) {
      const auto& call = static_cast<const ast::FunctionCall&>(*expression);
      expression = call.args.empty() ? nullptr : call.args.front();
      if (call.args.size() > 1)
        pending.insert(pending.end(), call.args.rbegin(), call.args.rend() - 1);
    } else {
      expression = nullptr;
    }
  }
}

}  // namespace

void ArithmeticsValidator::checkDivisionsByZero(
    const cursiv::Document& document, cursiv::ValidationAcceptor& accept,
    const cursiv::CancellationToken& token) const {
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::Module>)
    return;
  // One interpreter for the pass, so the divisors share its working memory.
  Interpreter interpreter;
  std::vector<const ast::Expression*> pending;
  for (const ast::Statement* const statement :
       static_cast<const ast::Module&>(*root).statements) {
    if (statement == nullptr) continue;
    if (statement->type == &cursiv::ast_type_tag<ast::Evaluation>)
      walk_divisions(static_cast<const ast::Evaluation&>(*statement).expression,
                     pending, interpreter, accept, token);
    else if (statement->type == &cursiv::ast_type_tag<ast::Definition>)
      walk_divisions(static_cast<const ast::Definition&>(*statement).expr,
                     pending, interpreter, accept, token);
  }
}

namespace {

// Every holder of a shared name is an error. A name's first holder is reported
// when its first duplicate turns up, so a name held once costs one entry.
class Duplicates {
 public:
  explicit Duplicates(std::size_t holders) { _first.reserve(holders); }

  void visit(const ast::AbstractDefinition& holder,
             cursiv::ValidationAcceptor& accept) {
    if (holder.name.empty()) return;
    const auto [at, fresh] = _first.try_emplace(holder.name, &holder);
    if (fresh) return;
    if (at->second != nullptr) say(*std::exchange(at->second, nullptr), accept);
    say(holder, accept);
  }

 private:
  static void say(const ast::AbstractDefinition& holder,
                  cursiv::ValidationAcceptor& accept) {
    accept
        .error(holder,
               std::format("Duplicate definition name: {}", holder.name))
        .property<&ast::AbstractDefinition::name>();
  }

  // The first holder of each name, null once reported.
  std::unordered_map<std::string_view, const ast::AbstractDefinition*> _first;
};

}  // namespace

void ArithmeticsValidator::checkUniqueParameters(
    const ast::Definition& definition,
    cursiv::ValidationAcceptor& accept) const {
  if (definition.args.size() < 2) return;
  Duplicates duplicates(definition.args.size());
  for (const ast::DeclaredParameter* const arg : definition.args)
    if (arg != nullptr) duplicates.visit(*arg, accept);
}

void ArithmeticsValidator::checkUniqueDefinitions(
    const ast::Module& module, cursiv::ValidationAcceptor& accept) const {
  Duplicates duplicates(module.statements.size());
  for (const ast::Statement* const statement : module.statements)
    if (statement != nullptr
        && statement->type == &cursiv::ast_type_tag<ast::Definition>)
      duplicates.visit(static_cast<const ast::Definition&>(*statement), accept);
}

void ArithmeticsValidator::checkMatchingParameters(
    const ast::FunctionCall& call, cursiv::ValidationAcceptor& accept) const {
  const ast::AbstractDefinition* const target = call.func.get();
  if (target == nullptr) return;
  // A parameter stands for a number, so it takes no argument.
  if (target->type == &cursiv::ast_type_tag<ast::DeclaredParameter>) {
    if (!call.args.empty())
      accept.error(
          call, std::format("Parameter {} takes no arguments.", target->name));
    return;
  }
  if (target->type != &cursiv::ast_type_tag<ast::Definition>) return;
  const auto* const definition = static_cast<const ast::Definition*>(target);
  if (definition->args.size() == call.args.size()) return;
  accept.error(
      call,
      std::format("Function {} expects {} parameters, but {} were "
                  "given.",
                  definition->name, definition->args.size(), call.args.size()));
}

// A cycle of calls, reported at each of its call sites with its path. A
// depth-first walk: a call into a definition still on the stack closes a cycle,
// and each definition is followed once, so a shared cycle is reported once.
void ArithmeticsValidator::checkFunctionRecursion(
    const ast::Module& module, cursiv::ValidationAcceptor& accept) const {
  // A definition on the stack, the call that entered it (null at the start)
  // and its own calls.
  struct Visit {
    const ast::Definition* definition;
    const ast::FunctionCall* entry;
    std::vector<const ast::FunctionCall*> calls;
    std::size_t next = 0;
  };
  // A definition's depth in `open` while walked, `kDone` once left.
  constexpr std::size_t kDone = std::numeric_limits<std::size_t>::max();
  std::unordered_map<const ast::Definition*, std::size_t> levelOf;
  std::vector<Visit> open;

  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr
        || statement->type != &cursiv::ast_type_tag<ast::Definition>)
      continue;
    const auto& start = static_cast<const ast::Definition&>(*statement);
    if (!levelOf.try_emplace(&start, 0).second) continue;
    open.push_back({&start, nullptr, select_calls(start)});
    while (!open.empty()) {
      Visit& top = open.back();
      if (top.next == top.calls.size()) {
        levelOf[top.definition] = kDone;
        open.pop_back();
        continue;
      }
      const ast::FunctionCall* const call = top.calls[top.next++];
      const ast::Definition* const target = definition_of(*call);
      const auto [level, fresh] = levelOf.try_emplace(target, open.size());
      if (fresh) {
        open.push_back({target, call, select_calls(*target)});
        continue;
      }
      if (level->second == kDone) continue;

      std::vector<const ast::FunctionCall*> cycle;
      for (auto above = open.begin() + level->second + 1; above != open.end();
           ++above)
        cycle.push_back(above->entry);
      cycle.push_back(call);
      const std::string message =
          std::format("Recursion is not allowed [{}]", print_cycle(cycle));
      // The closing call first, back to the one that entered the cycle.
      for (const ast::FunctionCall* const site : std::views::reverse(cycle))
        accept.error(*site, message).property<&ast::FunctionCall::func>();
    }
  }
}

// The value is carried as diagnostic data, not spelled into the message: a
// quick fix must not have to parse prose to know what to write.
void ArithmeticsValidator::checkNormalizable(
    const ast::Definition& definition,
    cursiv::ValidationAcceptor& accept) const {
  if (definition.expr == nullptr
      || definition.expr->type == &cursiv::ast_type_tag<ast::NumberLiteral>)
    return;

  std::unordered_map<const ast::Expression*, double> folded;
  std::vector<const ast::Expression*> order;
  fold(*definition.expr, folded, order);

  for (const ast::Expression* const expression : order) {
    const auto found = folded.find(expression);
    if (found == folded.end()) continue;
    cursiv::JsonValue::Object payload;
    payload["constant"] = cursiv::JsonValue(found->second);
    accept
        .information(*expression,
                     "Expression could be normalized to constant "
                         + number_text(found->second),
                     kExpressionNormalizable)
        .data(cursiv::JsonValue(std::move(payload)));
  }
}

}  // namespace arith
