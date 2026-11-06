// The statemachine language's two checks, each said and each silent. The one
// worth a lane of its own is the second: states and events share one namespace,
// so a state and an event wearing the same name collide -- a table per family
// would let that pair through.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/ValidationResult.hpp"
#include "statemachine/CoreServices.hpp"
#include "statemachine/Generator.hpp"

namespace {

namespace ct = cursiv::test;

using Services = sm::StateMachineCoreServices;
using Shared = sm::StateMachineSharedCoreServices;

struct World {
  cursiv::CoreLanguage<Services, Shared> services =
      cursiv::create_language<Services, Shared, sm::install_core_module>();

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

// The case a table per family lets through: one name, two families.
void a_state_and_an_event_of_one_name_collide() {
  const World world;
  const std::string text =
      "statemachine M\n"
      "events Foo\n"
      "initialState Foo\n"
      "state Foo end\n";
  const ct::ValidationResult said = world.validate(text);

  constexpr std::string_view kMessage = "Duplicate identifier name: Foo";
  CHECK(count(said, kMessage) == 2);
  ct::expectError(said, kMessage, at(text.find("Foo"), 3));   // the event
  ct::expectError(said, kMessage, at(text.rfind("Foo"), 3));  // the state
}

void two_states_of_one_name_still_collide() {
  const World world;
  const ct::ValidationResult said = world.validate(
      "statemachine M\n"
      "initialState A\n"
      "state A end\n"
      "state A end\n");
  CHECK(count(said, "Duplicate identifier name: A") == 2);
}

void two_events_of_one_name_still_collide() {
  const World world;
  const ct::ValidationResult said = world.validate(
      "statemachine M\n"
      "events E E\n"
      "initialState A\n"
      "state A end\n");
  CHECK(count(said, "Duplicate identifier name: E") == 2);
}

void distinct_names_say_nothing() {
  const World world;
  ct::expectNoIssues(
      world.validate("statemachine M\n"
                     "events Go\n"
                     "initialState Idle\n"
                     "state Idle Go => Idle end\n"));
}

void a_lowercase_state_is_a_warning_on_its_name() {
  const World world;
  const std::string text =
      "statemachine M\n"
      "initialState idle\n"
      "state idle end\n";
  const ct::ValidationResult said = world.validate(text);

  constexpr std::string_view kMessage =
      "State name should start with a capital letter.";
  CHECK(count(said, kMessage) == 1);
  ct::expectWarning(said, kMessage, at(text.rfind("idle"), 4));
}

void a_capitalised_state_says_nothing() {
  const World world;
  ct::expectNoIssues(
      world.validate("statemachine M\n"
                     "initialState Idle\n"
                     "state Idle end\n"));
}

// The keyword worn as a name. The grammar lexes a name as a plain token, so
// `end` parses; the refusal is the validator's, and it names the site.
void a_keyword_is_not_a_name() {
  const World world;
  const std::string text =
      "statemachine M\n"
      "initialState end\n"
      "state end end\n";
  const ct::ValidationResult said = world.validate(text);

  constexpr std::string_view kMessage =
      "`end` is a keyword and cannot be a name.";
  CHECK(count(said, kMessage) == 1);
  ct::expectError(said, kMessage, at(text.rfind("state end end") + 6, 3));
}

// ...and the name that is not one says nothing on that count.
void a_plain_name_says_nothing_about_keywords() {
  const World world;
  ct::expectNoIssues(
      world.validate("statemachine M\n"
                     "initialState Idle\n"
                     "state Idle end\n"));
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.is_open());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const sm::ast::Statemachine& machine_of(const ct::ValidationResult& said) {
  return static_cast<const sm::ast::Statemachine&>(
      *said.document->parseResult.value);
}

// The program each shipped machine becomes, whole and byte for byte: generated
// code rarely changes, and a change anywhere in it is one to read.
void the_shipped_machines_generate_their_golden_programs() {
  const std::filesystem::path root(CURSIV_EXAMPLE_DIR);
  for (const std::string_view name : {"doors", "trafficlight"}) {
    const World world;
    const ct::ValidationResult said = world.validate(
        read_file(root / "example" / (std::string(name) + ".statemachine")));
    CHECK(said.document->parseResult.value != nullptr);
    if (said.document->parseResult.value == nullptr) continue;
    CHECK(
        sm::generate_cpp_content(machine_of(said))
        == read_file(root / "tests" / "golden" / (std::string(name) + ".cpp")));
  }
}

// Two transitions on one event make one handler, the first: a second override
// of one method would not compile. Each state keeps its own handlers.
void one_event_makes_one_handler_per_state() {
  const World world;
  const ct::ValidationResult said = world.validate(
      "statemachine M events go stop initialState A\n"
      "state A go => B go => A stop => A end\n"
      "state B go => A end\n");
  CHECK(said.document->parseResult.value != nullptr);
  if (said.document->parseResult.value == nullptr) return;
  const std::string program = sm::generate_cpp_content(machine_of(said));
  const auto occurrences = [&program](std::string_view text) {
    std::size_t found = 0;
    for (auto at = program.find(text); at != std::string::npos;
         at = program.find(text, at + text.size()))
      ++found;
    return found;
  };
  CHECK(occurrences("void go() override;") == 2);
  CHECK(occurrences("void stop() override;") == 1);
  CHECK(occurrences("void A::go() {\n    statemachine->transition_to(new B);")
        == 1);
  CHECK(occurrences("void A::go() {") == 1);
  CHECK(occurrences("void B::go() {\n    statemachine->transition_to(new A);")
        == 1);
}

// A program that cannot be made, or cannot be written, is an error -- and the
// one written before stays whole.
void a_failed_generation_is_an_error_and_keeps_the_old_output() {
  const std::filesystem::path folder =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "generated";
  std::filesystem::create_directories(folder);
  const std::filesystem::path output = folder / "machine.cpp";
  std::filesystem::remove(output);
  std::ofstream(output, std::ios::binary) << "older\n";

  const World world;
  const ct::ValidationResult said =
      world.validate("statemachine M\ninitialState Nowhere\n");
  bool refused = false;
  try {
    (void)sm::generate_cpp(machine_of(said), "machine.statemachine",
                           folder.string());
  } catch (const std::runtime_error&) {
    refused = true;
  }
  CHECK(refused);
  CHECK(read_file(output) == "older\n");

#if defined(__linux__)
  // A disk that is full: the file opens, and the write fails.
  std::filesystem::remove(output);
  std::error_code linked;
  std::filesystem::create_symlink("/dev/full", output, linked);
  if (!linked) {
    const ct::ValidationResult valid =
        world.validate("statemachine M\ninitialState S\nstate S end\n");
    bool failed = false;
    try {
      (void)sm::generate_cpp(machine_of(valid), "machine.statemachine",
                             folder.string());
    } catch (const std::runtime_error&) {
      failed = true;
    }
    CHECK(failed);
  }
  std::filesystem::remove(output);
#endif
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  a_state_and_an_event_of_one_name_collide();
  two_states_of_one_name_still_collide();
  two_events_of_one_name_still_collide();
  distinct_names_say_nothing();
  a_lowercase_state_is_a_warning_on_its_name();
  a_capitalised_state_says_nothing();
  a_keyword_is_not_a_name();
  a_plain_name_says_nothing_about_keywords();
  the_shipped_machines_generate_their_golden_programs();
  one_event_makes_one_handler_per_state();
  a_failed_generation_is_an_error_and_keeps_the_old_output();
  return cursiv::test::report("statemachine_grammar_test");
}
