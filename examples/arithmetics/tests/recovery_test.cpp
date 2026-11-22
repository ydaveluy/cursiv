#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

#include "arithmetics/generated/Arith/Module.hpp"
#include "cursiv/MutationOracle.hpp"
#include "cursiv/RecoveryLane.hpp"

namespace {
using namespace cursiv::test;

void division_at_the_caret(const cursiv::Parser& parser) {
  const std::string before = "module M 1 ";
  for (bool closed : {false, true}) {
    const auto source = before + "*" + std::string(400, 'a') + (closed ? "*/2;" : "2;");
    const auto result = parser.parse(source);
    std::vector<cursiv::NextFeature> next;
    parser.nextFeaturesAt(result.cst(), before.size(), before.size(), next);
    const bool division = std::ranges::any_of(next, [&](const cursiv::NextFeature& feature) {
      const auto words = parser.getGrammar().terminalSpellings(feature.feature);
      return std::ranges::find(words, "/") != words.end();
    });
    CHECK(division == !closed);
  }
}

void run(Lane& lane) {
  lane.lang = "arithmetics";
  const std::unique_ptr<cursiv::Parser> engine =
      arith::generated::Arith::createParser();
  division_at_the_caret(*engine);
  language(lane, *engine, "valid.calc",
           {"01_typo1.calc", "02_typo2.calc", "03_closer_block.calc",
            "04_closer_eof.calc", "05_extra_token.calc", "06_truncated.calc",
            "07_two_faults.calc", "08_head_fault.calc", "09_list_delim.calc",
            "10_noise.calc", "11_head_typo.calc", "12_open_region_eof.calc"});
  mutationOracle(*engine, "arithmetics", document("valid.calc"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("arithmetics_recovery_test");
}
