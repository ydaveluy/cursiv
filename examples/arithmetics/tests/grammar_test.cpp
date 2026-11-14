// The calculator's six checks, each reporting and each silent. A name standing
// for zero is a division by zero, and a call cycle is reported at each of its
// call sites with its path.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "arithmetics/CoreServices.hpp"
#include "arithmetics/Evaluator.hpp"
#include "arithmetics/Interpreter.hpp"
#include "arithmetics/LspModule.hpp"
#include "arithmetics/validation/ArithmeticsValidator.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/ValidationResult.hpp"
#include "cursiv/actions/CodeActionProvider.hpp"
#include "cursiv/cli.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"

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

// How many findings carry that message, whatever else they carry.
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

// A cycle that two sibling calls reach, `b` and `c` from `a`, is reported
// whatever the order of the definitions.
void a_cycle_reached_through_two_siblings_is_said() {
  for (const std::string_view order :
       {"def s: a;\ndef a: b + c;\ndef b: c;\ndef c: b;\n",
        "def b: c;\ndef c: b;\ndef s: a;\ndef a: b + c;\n"}) {
    const World world;
    const ct::ValidationResult said =
        world.validate("module m\n" + std::string(order));
    CHECK(count(said, "Recursion is not allowed [c()->b()]") == 2);
    CHECK(said.diagnostics.size() == 2);
  }
}

void a_call_that_returns_says_nothing() {
  const World world;
  ct::expectNoIssues(world.validate(
      "module m\ndef add(a, b): a + b;\ndef calc: add(2, 3);\n"));
}

void every_definition_of_a_shared_name_is_an_error() {
  const World world;
  const std::string text =
      "module m\ndef x: 1;\ndef y: 2;\ndef x: 3;\ndef x: 4;\n";
  const ct::ValidationResult said = world.validate(text);
  constexpr std::string_view kMessage = "Duplicate definition name: x";
  CHECK(count(said, kMessage) == 3);
  CHECK(count(said, "Duplicate definition name: y") == 0);
  ct::expectError(said, kMessage, at(text.find("x: 1"), 1));
  ct::expectError(said, kMessage, at(text.find("x: 3"), 1));
  ct::expectError(said, kMessage, at(text.find("x: 4"), 1));
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

// An argument is evaluated before the body it is passed to, so a call written
// there is a call its host makes.
void a_call_in_an_argument_closes_a_cycle() {
  const World world;
  const std::string text = "module m\ndef id(x): x;\ndef a: id(a);\n";
  const ct::ValidationResult said = world.validate(text);
  constexpr std::string_view kMessage = "Recursion is not allowed [a()]";
  CHECK(count(said, kMessage) == 1);
  ct::expectError(said, kMessage, at(text.rfind("a)"), 1));
}

// A parameter stands for a number: arguments written after it would take no
// part in the value.
void a_parameter_called_with_arguments_is_an_error() {
  const World world;
  const std::string text = "module m\ndef f(x): x(2);\nf(3);\n";
  const ct::ValidationResult said = world.validate(text);
  CHECK(said.diagnostics.size() == 1);
  ct::expectError(said, "Parameter x takes no arguments.",
                  at(text.find("x(2)"), 4));
}

// A constant is offered when it is short -- eight characters at most -- and
// written as JavaScript writes a number; zero is a constant too.
void a_short_constant_is_offered_as_written() {
  const World world;
  const auto offered = [&world](std::string_view body, std::string_view as) {
    return count(
        world.validate("module m\ndef t: " + std::string(body) + ";\n"),
        "Expression could be normalized to constant " + std::string(as));
  };
  CHECK(offered("1 - 2", "-1") == 1);
  CHECK(offered("50000 * 2", "100000") == 1);
  CHECK(offered("1 - 1", "0") == 1);
  ct::expectNoIssues(world.validate("module m\ndef t: 1 / 3;\n"));

  // The whole expression, not its first operand.
  const std::string text = "module m\ndef t: 2 * 3 - 10;\n";
  const ct::ValidationResult said = world.validate(text);
  CHECK(said.diagnostics.size() == 1);
  ct::expectIssue(
      said, {.message = "Expression could be normalized to constant -4",
             .offset = static_cast<cursiv::TextOffset>(text.find("2 * 3")),
             .length = 10});
}

// The rewrite writes only a number that reads back as the same value: none for
// `1 - 2`, which has a sign, or for `0 * (1 - 2)`, a negative zero; a plain
// zero is offered.
void the_quick_fix_writes_only_a_number_the_grammar_reads() {
  const arith::ArithmeticsLanguage editor = arith::create_lsp_services();
  const ct::ValidationResult said =
      ct::validationHelper(editor.language,
                           "module m\ndef a: 50000 * 2;\ndef b: 1 - 2;\n"
                           "def c: (0 * (1 - 2)) ^ (0 - 1);\ndef d: 1 - 1;\n");
  CHECK(said.diagnostics.size() == 5);
  lsp::CodeActionParams params;
  params.range.end.line = 5;
  const std::vector<lsp::CodeAction> actions =
      editor.language.codeActionProvider->getCodeActions(*said.document,
                                                         params);
  std::vector<std::string> titles;
  for (const lsp::CodeAction& action : actions) titles.push_back(action.title);
  CHECK((titles
         == std::vector<std::string>{"Replace with constant 100000",
                                     "Replace with constant 0"}));
  if (actions.empty() || !actions[0].edit.has_value()
      || !actions[0].edit->changes.has_value())
    return;
  const auto& edits = actions[0].edit->changes->begin()->second;
  CHECK(edits.size() == 1 && edits[0].newText == "100000");
}

// What a value prints as: the digits it needs and no more, as JavaScript
// writes them.
void a_value_is_written_as_javascript_writes_it() {
  CHECK(arith::number_text(123456789.0 * 1000000000000.0)
        == "123456789000000000000");
  CHECK(arith::number_text(1e21) == "1e+21");
  CHECK(arith::number_text(1e-7) == "1e-7");
  CHECK(arith::number_text(0.000001) == "0.000001");
  CHECK(arith::number_text(1.0 / 3.0) == "0.3333333333333333");
  CHECK(arith::number_text(-2.5) == "-2.5");
  CHECK(arith::number_text(1.5e300) == "1.5e+300");
  CHECK(arith::number_text(-0.0) == "0");
  CHECK(arith::number_text(std::numeric_limits<double>::infinity())
        == "Infinity");
}

// The definitions without parameters are values: each is computed once, so a
// chain that doubles at every level costs one step per level.
void a_shared_constant_is_computed_once() {
  const World world;
  std::string text = "module m\ndef d0: 1;\n";
  constexpr int kLevels = 64;
  for (int i = 1; i <= kLevels; ++i)
    text += "def d" + std::to_string(i) + ": d" + std::to_string(i - 1) + " + d"
            + std::to_string(i - 1) + ";\n";
  text += "d" + std::to_string(kLevels) + ";\n";
  const ct::ValidationResult said = world.validate(text);
  ct::expectNoIssues(said);
  const auto& module =
      static_cast<const arith::ast::Module&>(*said.document->parseResult.value);
  const arith::EvaluationResult results = arith::interpret_evaluations(module);
  CHECK(results.size() == 1);
  CHECK(results.begin()->second == 18446744073709551616.0);
}

#if defined(__linux__)
// A worker's stack can be small -- half a mebibyte on macOS, one on WASI -- so
// no walk holds a native frame per term or per call.
#ifdef NDEBUG
constexpr std::size_t kSmallStack = 64 * 1024;
#else
constexpr std::size_t kSmallStack = 256 * 1024;
#endif

struct Walk {
  const arith::ArithmeticsValidator* validator = nullptr;
  const cursiv::Document* document = nullptr;
  std::size_t said = 0;
  arith::EvaluationResult results;
};

void* walk(void* arg) {
  auto* job = static_cast<Walk*>(arg);
  std::vector<cursiv::Diagnostic> found;
  const auto collect = [&found](cursiv::Diagnostic&& diagnostic) {
    found.push_back(std::move(diagnostic));
  };
  cursiv::ValidationAcceptor accept(cursiv::ValidationAcceptor::Sink(collect),
                                    *job->document, "arithmetics");
  const auto& module =
      static_cast<const arith::ast::Module&>(*job->document->parseResult.value);
  job->validator->checkDivisionsByZero(*job->document, accept, {});
  job->validator->checkFunctionRecursion(module, accept);
  for (const arith::ast::Statement* const statement : module.statements)
    if (statement->type == &cursiv::ast_type_tag<arith::ast::Definition>)
      job->validator->checkNormalizable(
          static_cast<const arith::ast::Definition&>(*statement), accept);
  job->said = found.size();
  job->results = arith::interpret_evaluations(module);
  return nullptr;
}

// A sum of many terms is as deep a tree as it has terms, and a chain of
// definitions is as deep a call stack as it has definitions.
void a_deep_module_fits_a_small_stack() {
  constexpr std::size_t kTerms = 20000;
  constexpr std::size_t kLevels = 5000;
  std::string text = "module m\ndef sum: x";
  for (std::size_t i = 0; i < kTerms; ++i) text += " + 1";
  text += ";\ndef x: 0;\ndef d0: 0;\n";
  for (std::size_t i = 1; i <= kLevels; ++i)
    text +=
        "def d" + std::to_string(i) + ": d" + std::to_string(i - 1) + " + 1;\n";
  text += "sum;\nd" + std::to_string(kLevels) + ";\n";

  const World world;
  const ct::ValidationResult said = world.validate(text);
  ct::expectNoIssues(said);

  const arith::ArithmeticsValidator validator(world.services.language);
  Walk job;
  job.validator = &validator;
  job.document = said.document.get();
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, kSmallStack);
  pthread_t th;
  const bool started = pthread_create(&th, &attr, &walk, &job) == 0;
  pthread_attr_destroy(&attr);
  CHECK(started);
  if (!started) return;
  pthread_join(th, nullptr);
  CHECK(job.said == 0);
  std::vector<double> values;
  for (const auto& [evaluation, value] : job.results)
    values.push_back(value.value_or(-1.0));
  std::ranges::sort(values);
  CHECK(values
        == std::vector<double>{static_cast<double>(kLevels),
                               static_cast<double>(kTerms)});
}
#endif

// Calls keep their arguments below the body's operands, including sibling
// calls made while an outer binary expression already has a value waiting.
void calls_keep_parameters_and_pending_operands() {
  const World world;
  const ct::ValidationResult said = world.validate(
      "module m\n"
      "def id(x): x;\n"
      "def pack(a, b): a * 10 + b;\n"
      "def body(x, y): pack(y, id(x)) + x + pack(id(y), x);\n"
      "def z: 5;\n"
      "def wrap(x): z + pack(x, id(z));\n"
      "def bad(x): 1 / (x - x);\n"
      "body(2, 3);\n"
      "pack(id(2), id(3));\n"
      "id(id(4));\n"
      "10 + body(2, 3) * 2;\n"
      "wrap(4);\n"
      "body(2, 3) / (1 - 1);\n"
      "pack(bad(1), id(9));\n"
      "1 + z * 2;\n"
      "body(7, 8);\n");
  const auto* const module = cursiv::ast_cast<const arith::ast::Module>(
      said.document->parseResult.value);
  CHECK(module != nullptr);
  if (module == nullptr) return;
  arith::Interpreter interpreter;
  std::vector<std::optional<double>> values;
  const arith::ast::Expression* first = nullptr;
  for (const arith::ast::Statement* statement : module->statements) {
    const auto* const evaluation =
        cursiv::ast_cast<const arith::ast::Evaluation>(statement);
    if (evaluation == nullptr) continue;
    CHECK(evaluation->expression != nullptr);
    if (evaluation->expression == nullptr) return;
    if (first == nullptr) first = evaluation->expression;
    values.push_back(interpreter.evaluate(*evaluation->expression));
  }
  CHECK((values == std::vector<std::optional<double>>{
                       66.0, 23.0, 4.0, 142.0, 50.0, std::nullopt,
                       std::nullopt, 11.0, 181.0}));
  CHECK(first != nullptr);
  if (first != nullptr) {
    CHECK(!interpreter.evaluate(*first, 1).has_value());
    CHECK(interpreter.evaluate(*first, 512) == 66.0);
    // A question stopped at any step leaves no state behind for the next.
    for (std::size_t budget = 1; budget < 64; ++budget) {
      const std::optional<double> bounded =
          interpreter.evaluate(*first, budget);
      CHECK(!bounded.has_value() || *bounded == 66.0);
      CHECK(interpreter.evaluate(*first) == 66.0);
    }
  }
}

// The evaluations a valid module asks for, each one reported: a zero that a
// parameter brings has no value, and that is an answer too.
void every_evaluation_is_said() {
  const std::filesystem::path file =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "evaluations.calc";
  std::ofstream(file) << "module m\n"
                         "def f(x): 1 / x;\n"
                         "f(0);\n"
                         "f(2);\n"
                         "3.14159 * 5 ^ 2;\n"
                         "50000 * 2;\n";
  arith::ArithmeticsSharedCoreServices shared;
  shared.fileSystemProvider =
      std::make_unique<cursiv::LocalFileSystemProvider>();
  cursiv::install_default_shared_core_services(shared);
  cursiv::register_language<Services, Shared, arith::install_core_module>(
      shared);
  std::ostringstream out;
  std::ostringstream err;
  const int status = arith::eval_file(file.string(), shared, out, err);
  CHECK(status == cursiv::kDiagnostics);
  CHECK(out.str()
        == "line 4: f(2) ===> 0.5\n"
           "line 5: 3.14159 * 5 ^ 2 ===> 78.53975\n"
           "line 6: 50000 * 2 ===> 100000\n");
  CHECK(err.str() == "line 3: f(0) cannot be evaluated\n");
  std::filesystem::remove(file);
}

// A statement whose end is written as a colon: every operator could stand
// there at the same cost, whatever level it binds at, and either engine names
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
  a_cycle_reached_through_two_siblings_is_said();
  a_call_that_returns_says_nothing();
  every_definition_of_a_shared_name_is_an_error();
  two_definitions_of_two_names_say_nothing();
  two_parameters_of_one_name_are_two_errors();
  two_parameters_of_two_names_say_nothing();
  a_call_of_the_wrong_arity_names_both_counts();
  a_call_of_the_right_arity_says_nothing();
  a_foldable_body_carries_its_constant();
  only_the_widest_foldable_span_is_offered();
  a_body_that_is_already_a_number_says_nothing();
  a_misplaced_colon_names_every_operator();
  a_call_in_an_argument_closes_a_cycle();
  a_parameter_called_with_arguments_is_an_error();
  a_short_constant_is_offered_as_written();
  the_quick_fix_writes_only_a_number_the_grammar_reads();
  a_value_is_written_as_javascript_writes_it();
  a_shared_constant_is_computed_once();
#if defined(__linux__)
  a_deep_module_fits_a_small_stack();
#endif
  every_evaluation_is_said();
  calls_keep_parameters_and_pending_operands();
  return cursiv::test::report("arithmetics_grammar_test");
}
