#include "arithmetics/Interpreter.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "arithmetics/Operator.hpp"

namespace arith {

// Every failure is `nullopt`, never an exception: most questions a validator
// asks have no answer. The walk runs on explicit stacks, kept between
// questions, so a long expression or chain of calls never uses the thread's.
class Interpreter::Machine {
 public:
  // `remaining` is the steps left before the evaluation gives up, null for
  // unlimited. A bounded run forgets the values earlier runs computed: what
  // it can answer within its steps then depends on its own expression only.
  [[nodiscard]] std::optional<double> run(const ast::Expression& expression,
                                          std::size_t* remaining) {
    // Most divisors a validator asks about are a number as written.
    if (expression.type == &cursiv::ast_type_tag<ast::NumberLiteral>)
      return static_cast<const ast::NumberLiteral&>(expression).value;
    _remaining = remaining;
    if (remaining != nullptr) _constants.clear();
    // Only the calls a failed run left open: clearing the whole set would
    // cost every bucket it ever grew, on each question.
    for (const Frame& frame : _frames) _active.erase(frame.definition);
    _tasks.clear();
    _values.clear();
    _frames.clear();
    Task task{Kind::evaluate, &expression};
    for (;;) {
      switch (task.kind) {
        case Kind::evaluate:
          task = evaluate(static_cast<const ast::Expression&>(*task.node));
          break;
        case Kind::apply:
          if (!apply(static_cast<const ast::BinaryExpression&>(*task.node)))
            return std::nullopt;
          task = {Kind::done, nullptr};
          break;
        case Kind::call:
          task = enter(static_cast<const ast::Definition&>(*task.node));
          break;
        case Kind::leave:
          leave(static_cast<const ast::Definition&>(*task.node));
          task = {Kind::done, nullptr};
          break;
        default:
          return std::nullopt;
      }
      if (task.kind == Kind::failed) return std::nullopt;
      if (task.kind != Kind::done) continue;
      if (_tasks.empty()) return _values.back();
      task = _tasks.back();
      _tasks.pop_back();
    }
  }

 private:
  enum class Kind : std::uint8_t { evaluate, apply, call, leave, done, failed };
  struct Task {
    Kind kind;
    const cursiv::AstNode* node;
  };
  // A call being evaluated: its parameters occupy `_values` from `first`
  // on, in declaration order, below every temporary its body pushes.
  struct Frame {
    const ast::Definition* definition;
    std::size_t first;
  };

  Task evaluate(const ast::Expression& expression) {
    if (_remaining != nullptr) {
      if (*_remaining == 0) return {Kind::failed, nullptr};
      --*_remaining;
    }

    if (expression.type == &cursiv::ast_type_tag<ast::NumberLiteral>) {
      _values.push_back(
          static_cast<const ast::NumberLiteral&>(expression).value);
      return {Kind::done, nullptr};
    }

    if (expression.type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
      const ast::Expression* const inner =
          static_cast<const ast::GroupedExpression&>(expression).expression;
      if (inner == nullptr) return {Kind::failed, nullptr};
      return {Kind::evaluate, inner};
    }

    if (expression.type == &cursiv::ast_type_tag<ast::BinaryExpression>) {
      const auto& binary =
          static_cast<const ast::BinaryExpression&>(expression);
      if (binary.left == nullptr || binary.right == nullptr)
        return {Kind::failed, nullptr};
      // Popped in reverse: the left operand first, then the right, then the
      // operator over both.
      _tasks.emplace_back(Kind::apply, &binary);
      _tasks.emplace_back(Kind::evaluate, binary.right);
      return {Kind::evaluate, binary.left};
    }

    if (expression.type == &cursiv::ast_type_tag<ast::FunctionCall>)
      return call(static_cast<const ast::FunctionCall&>(expression));

    return {Kind::failed, nullptr};
  }

  Task call(const ast::FunctionCall& call) {
    const ast::AbstractDefinition* const target = call.func.get();
    if (target == nullptr) return {Kind::failed, nullptr};

    // A parameter used as a value: the name stands for what the enclosing
    // call bound, and it takes no argument.
    if (target->type == &cursiv::ast_type_tag<ast::DeclaredParameter>) {
      if (_frames.empty() || !call.args.empty()) return {Kind::failed, nullptr};
      const Frame& frame = _frames.back();
      if (target->parent != frame.definition) return {Kind::failed, nullptr};
      const auto place = _places.find(target);
      if (place == _places.end()) return {Kind::failed, nullptr};
      const double value = _values[frame.first + place->second];
      _values.push_back(value);
      return {Kind::done, nullptr};
    }

    if (target->type != &cursiv::ast_type_tag<ast::Definition>)
      return {Kind::failed, nullptr};
    const auto& definition = static_cast<const ast::Definition&>(*target);
    if (definition.args.size() != call.args.size())
      return {Kind::failed, nullptr};
    if (_active.contains(&definition)) return {Kind::failed, nullptr};
    if (definition.expr == nullptr) return {Kind::failed, nullptr};

    if (const auto known = _constants.find(&definition);
        known != _constants.end()) {
      _values.push_back(known->second);
      return {Kind::done, nullptr};
    }

    // The arguments are evaluated in the caller's frame, the body in the
    // callee's: a parameter of the same name does not leak between the two.
    if (call.args.empty()) return enter(definition);
    // Each argument costs a step: stop before queuing more than the budget.
    if (_remaining != nullptr && *_remaining < call.args.size())
      return {Kind::failed, nullptr};
    _tasks.emplace_back(Kind::call, &definition);
    for (std::size_t at = call.args.size(); at-- > 1;) {
      if (definition.args[at] == nullptr || call.args[at] == nullptr)
        return {Kind::failed, nullptr};
      _tasks.emplace_back(Kind::evaluate, call.args[at]);
    }
    if (definition.args.front() == nullptr || call.args.front() == nullptr)
      return {Kind::failed, nullptr};
    return {Kind::evaluate, call.args.front()};
  }

  // The arguments are on the value stack, the first deepest.
  Task enter(const ast::Definition& definition) {
    const std::size_t count = definition.args.size();
    const std::size_t first = _values.size() - count;
    // Learn the parameters' places on the first call, so the body reads them
    // without a search.
    if (count != 0 && !_places.contains(definition.args.front()))
      for (std::size_t at = 0; at < count; ++at)
        _places.emplace(definition.args[at], at);
    _frames.push_back({&definition, first});
    _active.insert(&definition);
    _tasks.emplace_back(Kind::leave, &definition);
    return {Kind::evaluate, definition.expr};
  }

  void leave(const ast::Definition& definition) {
    const std::size_t first = _frames.back().first;
    const double result = _values.back();
    _values[first] = result;
    _values.erase(_values.begin() + first + 1, _values.end());
    _frames.pop_back();
    _active.erase(&definition);
    if (definition.args.empty())
      _constants.emplace(&definition, result);
  }

  bool apply(const ast::BinaryExpression& binary) {
    const double right = _values.back();
    _values.pop_back();
    const double left = _values.back();
    // The operator table throws on a zero divisor; on this path that is an
    // answerless question, not an unwind.
    if ((binary.op == ast::Operator::divide
         || binary.op == ast::Operator::modulo)
        && right == 0.0) {
      _values.pop_back();
      return false;
    }
    // The left operand's slot now holds the result; the stack only shrinks.
    _values.back() = apply_operator(binary.op)(left, right);
    return true;
  }

  std::size_t* _remaining = nullptr;
  std::vector<Task> _tasks;
  std::vector<double> _values;
  std::vector<Frame> _frames;
  // The definitions whose bodies are being evaluated, so a cycle answers
  // nothing instead of running for ever.
  std::unordered_set<const ast::Definition*> _active;
  std::unordered_map<const ast::Definition*, double> _constants;
  // Each parameter's index among its definition's.
  std::unordered_map<const ast::AbstractDefinition*, std::size_t> _places;
};

Interpreter::Interpreter() : _machine(std::make_unique<Machine>()) {}

Interpreter::~Interpreter() = default;

std::optional<double> Interpreter::evaluate(const ast::Expression& expression,
                                            std::size_t stepLimit) {
  std::size_t steps = stepLimit;
  return _machine->run(expression, stepLimit != 0 ? &steps : nullptr);
}

EvaluationResult interpret_evaluations(const ast::Module& module) {
  EvaluationResult results;
  Interpreter interpreter;
  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr
        || statement->type != &cursiv::ast_type_tag<ast::Evaluation>)
      continue;
    const auto& evaluation = static_cast<const ast::Evaluation&>(*statement);
    // One that answers nothing does not stop the others: they are still
    // answers.
    results.emplace(&evaluation,
                    evaluation.expression == nullptr
                        ? std::nullopt
                        : interpreter.evaluate(*evaluation.expression));
  }
  return results;
}

std::string number_text(double value) {
  if (std::isnan(value)) return "NaN";
  if (value == 0.0) return "0";
  if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";

  // The shortest digits that read back as the value, `d.ddde+x`, taken apart
  // into the digits and the position of the decimal point after the first n.
  std::array<char, 32> buffer{};
  const auto [end, failure] =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                    std::fabs(value), std::chars_format::scientific);
  if (failure != std::errc{}) return {};
  const std::string_view written(buffer.data(),
                                 static_cast<std::size_t>(end - buffer.data()));
  const std::size_t e = written.find('e');
  std::string digits(1, written.front());
  if (e > 1) digits += written.substr(2, e - 2);
  std::string_view power = written.substr(e + 1);
  if (power.front() == '+') power.remove_prefix(1);
  int exponent = 0;
  (void)std::from_chars(power.data(), power.data() + power.size(), exponent);
  const int n = exponent + 1;
  const int k = static_cast<int>(digits.size());

  std::string text = value < 0 ? "-" : "";
  if (k <= n && n <= 21) {
    text += digits;
    text.append(static_cast<std::size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    text += digits.substr(0, static_cast<std::size_t>(n));
    text += '.';
    text += digits.substr(static_cast<std::size_t>(n));
  } else if (-6 < n && n <= 0) {
    text += "0.";
    text.append(static_cast<std::size_t>(-n), '0');
    text += digits;
  } else {
    text += digits.front();
    if (k > 1) {
      text += '.';
      text += digits.substr(1);
    }
    text += n - 1 < 0 ? "e-" : "e+";
    text += std::to_string(std::abs(n - 1));
  }
  return text;
}

}  // namespace arith
