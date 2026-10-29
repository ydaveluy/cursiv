// The calculator's six checks, each said and each silent. Two are more than a
// table lookup: the divisor is evaluated, so a name standing for zero is a
// division by zero; and a call cycle is said at every call site it runs
// through, carrying the path that names which call closes it.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "arithmetics/core/CoreServices.hpp"
#include "arithmetics/core/validation/ArithmeticsValidator.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "cursiv/test/ValidationHelper.hpp"
#include "cursiv/test/ValidationResult.hpp"

namespace {

namespace ct = cursiv::test;

using Services = arith::ArithmeticsCoreServices;
using Shared = arith::ArithmeticsSharedCoreServices;

constexpr std::string_view kDivByZero = "Division by zero is detected.";

struct World {
  cursiv::CoreLanguage<Services, Shared> services =
      cursiv::create_language<Services, Shared, arith::install_core_module>();

  [[nodiscard]] ct::ValidationResult validate(std::string text) const {
    return ct::validationHelper(services.language, std::move(text));
  }
};

// How many findings say that sentence, whatever else they carry.
std::size_t count(const ct::ValidationResult& said, std::string_view message) {
  return ct::filterByOptions(said, {.message = std::string(message)}).size();
}

// The span a finding must cover, as the filter every check below states it in.
ct::ExpectDiagnosticOptionsWithoutContent at(std::size_t offset,
                                             std::size_t length) {
  return {.offset = static_cast<cursiv::TextOffset>(offset),
          .length = static_cast<cursiv::TextOffset>(length)};
}

void a_literal_zero_divisor_is_an_error() {
  const World world;
  const std::string text = "module m\n5 / 0;\n";
  const ct::ValidationResult said = world.validate(text);
  CHECK(count(said, kDivByZero) == 1);
  // The divisor, because the check asked for it: `.property<&right>()` names
  // the member, and the member has a record of its own to anchor on.
  ct::expectError(said, kDivByZero, at(text.find("5 / 0") + 4, 1));

  const World modulo;
  CHECK(count(modulo.validate("module m\n5 % 0;\n"), kDivByZero) == 1);
  const World computed;
  CHECK(count(computed.validate("module m\n5 / (3 - 3);\n"), kDivByZero) == 1);
}

// A name standing for zero is zero: the check evaluates the divisor rather
// than folding what is spelled in place.
void a_call_reaching_zero_is_an_error() {
  const World world;
  const std::string text = "module m\ndef zero: 0;\n1 / zero;\n";
  const ct::ValidationResult said = world.validate(text);
  CHECK(count(said, kDivByZero) == 1);
  // The divisor again -- here the name that stands for zero, not the value it
  // reaches: what the check underlines is what the author would have to change.
  ct::expectError(said, kDivByZero, at(text.find("1 / zero") + 4, 4));
}

void a_call_reaching_a_number_says_nothing() {
  const World world;
  ct::expectNoIssues(world.validate("module m\ndef two: 2;\n1 / two;\n"));
}

// A divisor that calls its own host must neither hang nor crash the pass: the
// evaluation stops at the cycle and the check simply has nothing to say.
void a_recursive_divisor_neither_hangs_nor_reports() {
  const World world;
  const ct::ValidationResult said = world.validate("module m\ndef f: 1 / f;\n");
  CHECK(count(said, kDivByZero) == 0);
  CHECK(count(said, "Recursion is not allowed [f()]") == 1);
}

void a_definition_calling_itself_names_its_own_call() {
  const World world;
  const std::string text = "module m\ndef f: f;\n";
  const ct::ValidationResult said = world.validate(text);

  constexpr std::string_view kMessage = "Recursion is not allowed [f()]";
  CHECK(count(said, kMessage) == 1);
  ct::expectError(said, kMessage, at(text.rfind('f'), 1));
}

// The path is the message, and every call site of the cycle carries it: the
// definition's name alone would not say which call closes the loop.
void a_mutual_cycle_is_said_at_each_of_its_call_sites() {
  const World world;
  const std::string text = "module m\ndef f: g;\ndef g: f;\n";
  const ct::ValidationResult said = world.validate(text);

  constexpr std::string_view kMessage = "Recursion is not allowed [g()->f()]";
  CHECK(count(said, kMessage) == 2);
  ct::expectError(said, kMessage, at(text.find("g;"), 1));
  ct::expectError(said, kMessage, at(text.rfind("f;"), 1));
}

void a_three_step_cycle_spells_every_step() {
  const World world;
  const ct::ValidationResult said =
      world.validate("module m\ndef f: g;\ndef g: h;\ndef h: f;\n");
  CHECK(count(said, "Recursion is not allowed [g()->h()->f()]") == 3);
}

void a_call_that_returns_says_nothing() {
  const World world;
  ct::expectNoIssues(world.validate(
      "module m\ndef add(a, b): a + b;\ndef calc: add(2, 3);\n"));
}

void two_definitions_of_one_name_are_two_errors() {
  const World world;
  const std::string text = "module m\ndef x: 1;\ndef x: 2;\n";
  const ct::ValidationResult said = world.validate(text);
  constexpr std::string_view kMessage = "Duplicate definition name: x";
  CHECK(count(said, kMessage) == 2);
  ct::expectError(said, kMessage, at(text.find("x: 1"), 1));
  ct::expectError(said, kMessage, at(text.find("x: 2"), 1));
}

void two_definitions_of_two_names_say_nothing() {
  const World world;
  ct::expectNoIssues(world.validate("module m\ndef x: 1;\ndef y: 2;\n"));
}

void two_parameters_of_one_name_are_two_errors() {
  const World world;
  CHECK(count(world.validate("module m\ndef t(x, x): x + 1;\n"),
              "Duplicate definition name: x")
        == 2);
}

void two_parameters_of_two_names_say_nothing() {
  const World world;
  ct::expectNoIssues(world.validate("module m\ndef t(x, y): x + y;\n"));
}

void a_call_of_the_wrong_arity_names_both_counts() {
  const World world;
  CHECK(
      count(world.validate("module m\ndef add(a, b): a + b;\nadd(1, 2, 3);\n"),
            "Function add expects 2 parameters, but 3 were given.")
      == 1);
  CHECK(count(world.validate("module m\ndef add(a, b): a + b;\nadd(1);\n"),
              "Function add expects 2 parameters, but 1 were given.")
        == 1);
}

void a_call_of_the_right_arity_says_nothing() {
  const World world;
  ct::expectNoIssues(
      world.validate("module m\ndef add(a, b): a + b;\nadd(1, 2);\n"));
}

// The value travels as `data`, not as prose: a code action reads it without
// parsing the message back out.
void a_foldable_body_carries_its_constant() {
  const World world;
  const ct::ValidationResult said = world.validate("module m\ndef t: 2 + 3;\n");

  cursiv::JsonValue::Object constant;
  constant["constant"] = cursiv::JsonValue(5.0);
  ct::expectIssue(
      said,
      {.message = "Expression could be normalized to constant 5",
       .severity = cursiv::DiagnosticSeverity::Information,
       .code =
           cursiv::DiagnosticCode{std::string(arith::kExpressionNormalizable)},
       .data = cursiv::JsonValue(std::move(constant))});
}

// One offer per span: a parent that folds takes its children's place, and the
// fold reaches through every operator the language has.
void only_the_widest_foldable_span_is_offered() {
  const World world;
  const ct::ValidationResult said =
      world.validate("module m\ndef t: 2 ^ 3 + 1;\n");
  CHECK(count(said, "Expression could be normalized to constant 9") == 1);
  CHECK(said.diagnostics.size() == 1);

  // ...and `%` is a remainder, not a division -- computing it as one would
  // fold 7 % 2 to three and a half.
  const World remainder;
  CHECK(count(remainder.validate("module m\ndef t: 7 % 2;\n"),
              "Expression could be normalized to constant 1")
        == 1);
}

void a_body_that_is_already_a_number_says_nothing() {
  const World world;
  ct::expectNoIssues(world.validate("module m\ndef t: 5;\n"));
}

// A statement whose end is written as a colon: every operator could as
// cheaply stand there, whatever level it binds at, and either engine names
// the same ones, in the order the grammar declares them.
void a_misplaced_colon_names_every_operator() {
  const World world;
  const std::string text = "module m\nf(x, y):\n    x^y;\n";
  ct::expectError(world.validate(text),
                  "Expecting one of ';', '%', '^', '*' but found ':'.",
                  at(text.find(':'), 1));
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  a_literal_zero_divisor_is_an_error();
  a_call_reaching_zero_is_an_error();
  a_call_reaching_a_number_says_nothing();
  a_recursive_divisor_neither_hangs_nor_reports();
  a_definition_calling_itself_names_its_own_call();
  a_mutual_cycle_is_said_at_each_of_its_call_sites();
  a_three_step_cycle_spells_every_step();
  a_call_that_returns_says_nothing();
  two_definitions_of_one_name_are_two_errors();
  two_definitions_of_two_names_say_nothing();
  two_parameters_of_one_name_are_two_errors();
  two_parameters_of_two_names_say_nothing();
  a_call_of_the_wrong_arity_names_both_counts();
  a_call_of_the_right_arity_says_nothing();
  a_foldable_body_carries_its_constant();
  only_the_widest_foldable_span_is_offered();
  a_body_that_is_already_a_number_says_nothing();
  a_misplaced_colon_names_every_operator();
  return cursiv::test::report("arithmetics_grammar_test");
}
