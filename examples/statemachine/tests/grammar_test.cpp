// The statemachine language's two checks, each said and each silent. The one
// worth a lane of its own is the second: states and events share one namespace,
// so a state and an event wearing the same name collide -- a table per family
// would let that pair through.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "cursiv/test/ValidationHelper.hpp"
#include "cursiv/test/ValidationResult.hpp"
#include "statemachine/core/CoreServices.hpp"
#include "cursiv/test/Check.hpp"

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
  return cursiv::test::report("statemachine_grammar_test");
}
