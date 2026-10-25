#include "arithmetics/core/validation/ArithmeticsValidator.hpp"

#include <cmath>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "arithmetics/core/Interpreter.hpp"
#include "arithmetics/core/Operator.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace arith {

namespace {

const ast::Definition* definition_of(const ast::FunctionCall& call) {
  const cursiv::AstNode* const target = call.func.get();
  return target != nullptr &&
                 target->type == &cursiv::ast_type_tag<ast::Definition>
             ? static_cast<const ast::Definition*>(target)
             : nullptr;
}

// Parentheses are transparent to every walk below.
const ast::Expression* unwrap(const ast::Expression* expression) {
  while (expression != nullptr &&
         expression->type == &cursiv::ast_type_tag<ast::GroupedExpression>)
    expression =
        static_cast<const ast::GroupedExpression*>(expression)->expression;
  return expression;
}

// A call, and the definition whose body writes it.
struct NestedCall {
  const ast::FunctionCall* call;
  const ast::Definition* host;
};

// The call each call was reached from. Walking back through it is what turns a
// cycle into a path a reader can follow.
using CallTree = std::unordered_map<const ast::FunctionCall*, NestedCall>;

// The resolved calls a body writes, source order. Arguments are not descended
// into: a cycle is made of the calls a body makes, not of what it passes.
void select_calls(const ast::Definition& host,
                  const ast::Expression* expression,
                  std::vector<NestedCall>& out) {
  expression = unwrap(expression);
  if (expression == nullptr) return;
  if (expression->type == &cursiv::ast_type_tag<ast::FunctionCall>) {
    const auto& call = static_cast<const ast::FunctionCall&>(*expression);
    if (definition_of(call) != nullptr) out.push_back({&call, &host});
    return;
  }
  if (expression->type == &cursiv::ast_type_tag<ast::BinaryExpression>) {
    const auto& binary = static_cast<const ast::BinaryExpression&>(*expression);
    select_calls(host, binary.left, out);
    select_calls(host, binary.right, out);
  }
}

// The pair (entry, closing call) when climbing back from `to` re-enters the
// definition `to` calls, nothing when it enters none.
std::vector<NestedCall> select_cycle(const NestedCall& to,
                                     const CallTree& tree) {
  const ast::Definition* const target = definition_of(*to.call);
  auto found = tree.find(to.call);
  while (found != tree.end()) {
    if (found->second.host == target) return {found->second, to};
    found = tree.find(found->second.call);
  }
  return {};
}

// The cycle's calls, its closing one first and its entry last.
std::vector<NestedCall> iterate_back(const std::vector<NestedCall>& cycle,
                                     const CallTree& tree) {
  std::vector<NestedCall> back{cycle.back()};
  if (cycle.front().call == cycle.back().call) return back;
  auto found = tree.find(cycle.back().call);
  while (found != tree.end()) {
    back.push_back(found->second);
    if (found->second.call == cycle.front().call) break;
    found = tree.find(found->second.call);
  }
  return back;
}

// `f()->g()->f()`: the cycle read in call order, which is `back` reversed.
std::string print_cycle(const std::vector<NestedCall>& back) {
  std::string path;
  for (auto at : std::views::reverse(back)) {
    if (!path.empty()) path += "->";
    path += definition_of(*at.call)->name;
    path += "()";
  }
  return path;
}

// The value a sub-expression already stands for: its own if it is a literal,
// the folded one if the walk below produced it.
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

// A sub-expression that is already a number, bottom up. A parent that folds
// takes its children's places: reporting both would offer the author two
// rewrites for one span.
void fold(const ast::Expression& expression,
          std::unordered_map<const ast::Expression*, double>& folded,
          std::vector<const ast::Expression*>& order) {
  if (expression.type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
    if (const ast::Expression* const inner =
            static_cast<const ast::GroupedExpression&>(expression).expression)
      fold(*inner, folded, order);
    return;
  }
  if (expression.type != &cursiv::ast_type_tag<ast::BinaryExpression>) return;

  const auto& binary = static_cast<const ast::BinaryExpression&>(expression);
  if (binary.left == nullptr || binary.right == nullptr) return;
  fold(*binary.left, folded, order);
  fold(*binary.right, folded, order);

  const std::optional<double> left = known(binary.left, folded);
  const std::optional<double> right = known(binary.right, folded);
  if (!left.has_value() || !right.has_value()) return;

  double value = 0.0;
  try {
    value = apply_operator(binary.op)(*left, *right);
  } catch (const std::runtime_error&) {
    return;  // a division by zero is the other check's to report
  }
  if (!std::isfinite(value)) return;

  if (folded.try_emplace(&expression, value).second)
    order.push_back(&expression);
  if (const ast::Expression* const child = unwrap(binary.left))
    folded.erase(child);
  if (const ast::Expression* const child = unwrap(binary.right))
    folded.erase(child);
}

}  // namespace

namespace {

// The divisor is evaluated, not folded: a name standing for a number is a
// number, so a call reaching a zero body is a division by zero too. Bounded
// so the check prices O(divisions), never the whole call graph.
constexpr std::size_t kDivisorStepLimit = 64;

void say_if_zero(const ast::BinaryExpression& binary,
                 cursiv::ValidationAcceptor& accept) {
  if ((binary.op != ast::Operator::divide &&
       binary.op != ast::Operator::modulo) ||
      binary.right == nullptr)
    return;
  // An unbound name, a cycle, a spent budget: nullopt, not this check's
  // answer.
  const std::optional<double> divisor =
      eval_expression(*binary.right, kDivisorStepLimit);
  if (!divisor.has_value() || *divisor != 0.0) return;
  accept.error(binary, "Division by zero is detected.")
      .property<&ast::BinaryExpression::right>();
}

void walk_divisions(const ast::Expression* expression,
                    cursiv::ValidationAcceptor& accept,
                    const cursiv::CancellationToken& token) {
  if (expression == nullptr) return;
  cursiv::throw_if_cancelled(token);
  if (expression->type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
    walk_divisions(
        static_cast<const ast::GroupedExpression&>(*expression).expression,
        accept, token);
    return;
  }
  if (expression->type == &cursiv::ast_type_tag<ast::BinaryExpression>) {
    const auto& binary = static_cast<const ast::BinaryExpression&>(*expression);
    say_if_zero(binary, accept);
    walk_divisions(binary.left, accept, token);
    walk_divisions(binary.right, accept, token);
    return;
  }
  if (expression->type == &cursiv::ast_type_tag<ast::FunctionCall>)
    for (const ast::Expression* const argument :
         static_cast<const ast::FunctionCall&>(*expression).args)
      walk_divisions(argument, accept, token);
}

}  // namespace

void ArithmeticsValidator::checkDivisionsByZero(
    const cursiv::Document& document, cursiv::ValidationAcceptor& accept,
    const cursiv::CancellationToken& token) const {
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::Module>)
    return;
  for (const ast::Statement* const statement :
       static_cast<const ast::Module&>(*root).statements) {
    if (statement == nullptr) continue;
    if (statement->type == &cursiv::ast_type_tag<ast::Evaluation>)
      walk_divisions(static_cast<const ast::Evaluation&>(*statement).expression,
                     accept, token);
    else if (statement->type == &cursiv::ast_type_tag<ast::Definition>)
      walk_divisions(static_cast<const ast::Definition&>(*statement).expr,
                     accept, token);
  }
}

void ArithmeticsValidator::checkUniqueParameters(
    const ast::Definition& definition,
    cursiv::ValidationAcceptor& accept) const {
  std::unordered_map<std::string_view,
                     std::vector<const ast::DeclaredParameter*>>
      byName;
  for (const ast::DeclaredParameter* const arg : definition.args)
    if (arg != nullptr && !arg->name.empty()) byName[arg->name].push_back(arg);

  for (const auto& [name, params] : byName) {
    if (params.size() <= 1) continue;
    for (const ast::DeclaredParameter* const param : params)
      accept.error(*param, std::format("Duplicate definition name: {}", name))
          .property<&ast::AbstractDefinition::name>();
  }
}

void ArithmeticsValidator::checkUniqueDefinitions(
    const ast::Module& module, cursiv::ValidationAcceptor& accept) const {
  std::unordered_map<std::string_view, std::vector<const ast::Definition*>>
      byName;
  byName.reserve(module.statements.size());
  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr ||
        statement->type != &cursiv::ast_type_tag<ast::Definition>)
      continue;
    const auto* const definition =
        static_cast<const ast::Definition*>(statement);
    if (!definition->name.empty())
      byName[definition->name].push_back(definition);
  }

  for (const auto& [name, definitions] : byName) {
    if (definitions.size() <= 1) continue;
    for (const ast::Definition* const definition : definitions)
      accept
          .error(*definition,
                 std::format("Duplicate definition name: {}", name))
          .property<&ast::AbstractDefinition::name>();
  }
}

void ArithmeticsValidator::checkMatchingParameters(
    const ast::FunctionCall& call, cursiv::ValidationAcceptor& accept) const {
  const ast::Definition* const definition = definition_of(call);
  if (definition == nullptr || definition->args.size() == call.args.size())
    return;
  accept.error(
      call,
      std::format("Function {} expects {} parameters, but {} were "
                  "given.",
                  definition->name, definition->args.size(), call.args.size()));
}

// A cycle of calls, said at every call site with its full path: the
// definition's name alone would not say which call closes the loop. Each
// definition's calls are expanded once, so a shared cycle is reported once.
void ArithmeticsValidator::checkFunctionRecursion(
    const ast::Module& module, cursiv::ValidationAcceptor& accept) const {
  std::unordered_set<const ast::Definition*> expanded;
  CallTree tree;
  std::vector<std::vector<NestedCall>> cycles;

  const auto callsOf = [&expanded](const ast::Definition* host) {
    std::vector<NestedCall> calls;
    if (host != nullptr && expanded.insert(host).second)
      select_calls(*host, host->expr, calls);
    return calls;
  };

  // One breadth-first step: what the called body opens, minus what closes.
  const auto step = [&](const NestedCall& parent) {
    std::vector<NestedCall> open;
    const ast::Definition* const target = definition_of(*parent.call);
    if (parent.host == target) cycles.push_back({parent});
    for (const NestedCall& child : callsOf(target)) {
      tree.insert_or_assign(child.call, parent);
      std::vector<NestedCall> cycle = select_cycle(child, tree);
      if (cycle.empty())
        open.push_back(child);
      else
        cycles.push_back(std::move(cycle));
    }
    return open;
  };

  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr ||
        statement->type != &cursiv::ast_type_tag<ast::Definition>)
      continue;
    for (const NestedCall& root :
         callsOf(static_cast<const ast::Definition*>(statement))) {
      std::vector<NestedCall> open{root};
      while (!open.empty()) {
        std::vector<NestedCall> next;
        for (const NestedCall& parent : open) {
          const std::vector<NestedCall> children = step(parent);
          next.insert(next.end(), children.begin(), children.end());
        }
        open = std::move(next);
      }
    }
  }

  for (const std::vector<NestedCall>& cycle : cycles) {
    const std::vector<NestedCall> back = iterate_back(cycle, tree);
    const std::string message =
        std::format("Recursion is not allowed [{}]", print_cycle(back));
    for (const NestedCall& nested : back)
      accept.error(*nested.call, message).property<&ast::FunctionCall::func>();
  }
}

// The value is carried as diagnostic data, not spelled into the message: a
// quick fix must not have to parse prose to know what to write.
void ArithmeticsValidator::checkNormalizable(
    const ast::Definition& definition,
    cursiv::ValidationAcceptor& accept) const {
  if (definition.expr == nullptr ||
      definition.expr->type == &cursiv::ast_type_tag<ast::NumberLiteral>)
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
        .information(
            *expression,
            std::format("Expression could be normalized to constant {}",
                        found->second),
            kExpressionNormalizable)
        .data(cursiv::JsonValue(std::move(payload)));
  }
}

}  // namespace arith
