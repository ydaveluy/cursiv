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

// No exception on this path: a validator asks thousands of times and most
// questions have no answer -- an unwind per miss would dwarf the arithmetic.
// `nullopt` is every failure: an unbound name, a cycle, a spent budget, a zero
// divisor.
//
// The walk runs on explicit stacks, not on native frames: an expression a
// million terms long, or a chain of a million definitions, costs heap and
// never the thread's stack. The stacks are kept between questions, emptied.
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
    _tasks.clear();
    _values.clear();
    _arguments.clear();
    _frames.clear();
    _active.clear();
    _tasks.push_back({Kind::evaluate, &expression});
    while (!_tasks.empty()) {
      const Task task = _tasks.back();
      _tasks.pop_back();
      if (!step(task)) return std::nullopt;
    }
    return _values.back();
  }

 private:
  enum class Kind : std::uint8_t { evaluate, apply, call, leave };
  struct Task {
    Kind kind;
    const cursiv::AstNode* node;
  };
  // A call being evaluated: its parameters are bound to the values in
  // `_arguments` from `first` on, in the order they are declared.
  struct Frame {
    const ast::Definition* definition;
    std::size_t first;
  };

  bool step(const Task& task) {
    switch (task.kind) {
      case Kind::evaluate:
        return evaluate(static_cast<const ast::Expression&>(*task.node));
      case Kind::apply:
        return apply(static_cast<const ast::BinaryExpression&>(*task.node));
      case Kind::call:
        enter(static_cast<const ast::Definition&>(*task.node));
        return true;
      case Kind::leave:
        leave(static_cast<const ast::Definition&>(*task.node));
        return true;
    }
    return false;
  }

  bool evaluate(const ast::Expression& expression) {
    if (_remaining != nullptr) {
      if (*_remaining == 0) return false;
      --*_remaining;
    }

    if (expression.type == &cursiv::ast_type_tag<ast::NumberLiteral>) {
      _values.push_back(
          static_cast<const ast::NumberLiteral&>(expression).value);
      return true;
    }

    if (expression.type == &cursiv::ast_type_tag<ast::GroupedExpression>) {
      const ast::Expression* const inner =
          static_cast<const ast::GroupedExpression&>(expression).expression;
      if (inner == nullptr) return false;
      _tasks.push_back({Kind::evaluate, inner});
      return true;
    }

    if (expression.type == &cursiv::ast_type_tag<ast::BinaryExpression>) {
      const auto& binary =
          static_cast<const ast::BinaryExpression&>(expression);
      if (binary.left == nullptr || binary.right == nullptr) return false;
      // Popped in reverse: the left operand first, then the right, then the
      // operator over both.
      _tasks.push_back({Kind::apply, &binary});
      _tasks.push_back({Kind::evaluate, binary.right});
      _tasks.push_back({Kind::evaluate, binary.left});
      return true;
    }

    if (expression.type == &cursiv::ast_type_tag<ast::FunctionCall>)
      return call(static_cast<const ast::FunctionCall&>(expression));

    return false;
  }

  bool call(const ast::FunctionCall& call) {
    const ast::AbstractDefinition* const target = call.func.get();
    if (target == nullptr) return false;

    // A parameter used as a value: the name stands for what the enclosing
    // call bound, and it takes no argument.
    if (!_frames.empty()) {
      const Frame& frame = _frames.back();
      const auto& parameters = frame.definition->args;
      for (std::size_t at = 0; at < parameters.size(); ++at)
        if (parameters[at] == target) {
          if (!call.args.empty()) return false;
          _values.push_back(_arguments[frame.first + at]);
          return true;
        }
    }

    if (target->type != &cursiv::ast_type_tag<ast::Definition>) return false;
    const auto& definition = static_cast<const ast::Definition&>(*target);
    if (definition.args.size() != call.args.size()) return false;
    if (_active.contains(&definition)) return false;
    if (definition.expr == nullptr) return false;

    if (const auto known = _constants.find(&definition);
        known != _constants.end()) {
      _values.push_back(known->second);
      return true;
    }

    // The arguments are evaluated in the caller's frame, the body in the
    // callee's: a parameter of the same name does not leak between the two.
    _tasks.push_back({Kind::call, &definition});
    for (std::size_t at = call.args.size(); at-- > 0;) {
      if (definition.args[at] == nullptr || call.args[at] == nullptr)
        return false;
      _tasks.push_back({Kind::evaluate, call.args[at]});
    }
    return true;
  }

  // The arguments are on the value stack, the first deepest.
  void enter(const ast::Definition& definition) {
    const std::size_t count = definition.args.size();
    const std::size_t first = _values.size() - count;
    _frames.push_back({&definition, _arguments.size()});
    _arguments.insert(_arguments.end(), _values.begin() + first, _values.end());
    _values.resize(first);
    _active.insert(&definition);
    _tasks.push_back({Kind::leave, &definition});
    _tasks.push_back({Kind::evaluate, definition.expr});
  }

  void leave(const ast::Definition& definition) {
    _arguments.resize(_frames.back().first);
    _frames.pop_back();
    _active.erase(&definition);
    if (definition.args.empty())
      _constants.emplace(&definition, _values.back());
  }

  bool apply(const ast::BinaryExpression& binary) {
    const double right = _values.back();
    _values.pop_back();
    const double left = _values.back();
    _values.pop_back();
    // The operator table throws on a zero divisor; on this path that is an
    // answerless question, not an unwind.
    if ((binary.op == ast::Operator::divide
         || binary.op == ast::Operator::modulo)
        && right == 0.0)
      return false;
    _values.push_back(apply_operator(binary.op)(left, right));
    return true;
  }

  std::size_t* _remaining = nullptr;
  std::vector<Task> _tasks;
  std::vector<double> _values;
  std::vector<double> _arguments;
  std::vector<Frame> _frames;
  // The definitions whose bodies are being evaluated, so a cycle answers
  // nothing instead of running for ever.
  std::unordered_set<const ast::Definition*> _active;
  std::unordered_map<const ast::Definition*, double> _constants;
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
