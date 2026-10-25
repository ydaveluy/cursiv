#include <memory>
#include <string_view>

#include "cursiv/test/RecoveryLane.hpp"
#include "cursiv/test/MutationOracle.hpp"
#include "statemachine/core/ast.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace {
using namespace cursiv::test;

void run(Lane& lane) {
  lane.lang = "statemachine";
  const std::unique_ptr<cursiv::Parser> engine =
      sm::generated::SM::create_parser();
#if !defined(CURSIV_ENGINE_LL)
  {
    const auto seed = document("valid.statemachine");
    const auto before = engine->parse(seed);
    const auto* expected = before.value_as<sm::ast::Statemachine>();
    CHECK(expected);
    for (const bool replace : {false, true}) {
      auto text = seed;
      if (replace)
        text[11] = 'a';
      else
        text.insert(0, 1, 'a');
      const auto result = engine->parse(text);
      const auto* model = result.value_as<sm::ast::Statemachine>();
      CHECK(result.accepted && model);
      if (!model || !expected) continue;
      // A word before the head is read as the machine's name; an events
      // section opens before the name, which reads it and the section's own
      // keyword as events. At equal cost, the reading that drops no token of
      // the author's wins.
      const std::size_t shift = replace ? 0 : 2;
      CHECK(model->events.size() == expected->events.size() + shift);
      CHECK(model->states.size() == expected->states.size());
      if (model->events.size() == expected->events.size() + shift)
        for (std::size_t i = 0; i < expected->events.size(); ++i)
          CHECK(model->events[i + shift]->name == expected->events[i]->name);
      if (!replace && model->events.size() >= 2) {
        CHECK(model->events[0]->name == expected->name);
        CHECK(model->events[1]->name == "events");
      }
      if (model->states.size() == expected->states.size())
        for (std::size_t i = 0; i < model->states.size(); ++i) {
          CHECK(model->states[i]->name == expected->states[i]->name);
          CHECK(model->states[i]->transitions.size() ==
                expected->states[i]->transitions.size());
        }
      if (replace) CHECK(model->name == expected->name);
    }
  }
#endif
  language(lane, *engine, "valid.statemachine",
           {"01_typo1.statemachine", "02_typo2.statemachine",
            "03_closer_block.statemachine", "04_closer_eof.statemachine",
            "05_intruder.statemachine", "06_truncated.statemachine",
            "07_two_faults.statemachine", "08_head_fault.statemachine",
            "09_list_delim.statemachine", "10_noise.statemachine",
            "11_head_typo.statemachine"});
  mutation_oracle(*engine, "statemachine",
                  document("valid.statemachine"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("statemachine_recovery_test");
}
