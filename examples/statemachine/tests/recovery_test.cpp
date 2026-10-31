#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/test/MutationOracle.hpp"
#include "cursiv/test/RecoveryLane.hpp"
#include "statemachine/core/ast.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace {
using namespace cursiv::test;

void run(Lane& lane) {
  lane.lang = "statemachine";
  const std::unique_ptr<cursiv::Parser> engine =
      sm::generated::SM::create_parser();
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
      // A letter written before the head, or in place of one of its
      // letters, misspells the keyword: it is read over the word, and the
      // machine keeps its name and its events.
      CHECK(model->name == expected->name);
      CHECK(model->events.size() == expected->events.size());
      CHECK(model->states.size() == expected->states.size());
      if (model->events.size() == expected->events.size())
        for (std::size_t i = 0; i < expected->events.size(); ++i)
          CHECK(model->events[i]->name == expected->events[i]->name);
      if (model->states.size() == expected->states.size())
        for (std::size_t i = 0; i < model->states.size(); ++i) {
          CHECK(model->states[i]->name == expected->states[i]->name);
          CHECK(model->states[i]->transitions.size()
                == expected->states[i]->transitions.size());
        }
    }
  }
  // A word two letters off the keyword it stands for is read as that keyword,
  // and the name after it stays a name. A word unlike the keyword is the
  // next item: the keyword is inserted before it. A word unlike the keyword
  // and followed by a name, where only that keyword may stand, is replaced by
  // it -- one gesture where the alternatives take two.
  for (const auto& [text, want] :
       {std::pair<std::string_view, std::vector<std::string>>{
            "statemacne Lamp\n\nevents toggle",
            {"rep:statemachine@0<Expecting 'statemachine' but found "
             "'statemacne'.>",
             "ins:initialState@30<Expecting 'initialState'.>"}},
        {"Lamp\n\nevents toggle",
         {"ins:statemachine@0<Expecting 'statemachine'.>",
          "ins:initialState@19<Expecting 'initialState'.>"}},
        {"xyzzy Lamp\n\nevents toggle",
         {"rep:statemachine@0<Expecting 'statemachine' but found 'xyzzy'.>",
          "ins:initialState@25<Expecting 'initialState'.>"}}}) {
    const Proj p = project(*engine, std::string(text));
    CHECK(p.accepted && p.value);
    CHECK(p.diags == want);
    if (p.diags != want)
      for (const std::string& d : p.diags)
        std::printf("  said %s\n", d.c_str());
  }
  language(lane, *engine, "valid.statemachine",
           {"01_typo1.statemachine", "02_typo2.statemachine",
            "03_closer_block.statemachine", "04_closer_eof.statemachine",
            "05_intruder.statemachine", "06_truncated.statemachine",
            "07_two_faults.statemachine", "08_head_fault.statemachine",
            "09_list_delim.statemachine", "10_noise.statemachine",
            "11_head_typo.statemachine"});
  mutation_oracle(*engine, "statemachine", document("valid.statemachine"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("statemachine_recovery_test");
}
