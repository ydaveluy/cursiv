#include <algorithm>
#include <memory>
#include <string_view>

#include "cursiv/test/MutationOracle.hpp"
#include "cursiv/test/RecoveryLane.hpp"
#include "requirements/core/CoreModule.hpp"

namespace {
using namespace cursiv::test;

void run(Lane& lane) {
  lane.lang = "requirements";
  const std::unique_ptr<cursiv::Parser> engine =
      req::generated::Requirements::create_parser();
  {
    const auto seed = document("valid.req");
    const auto before = project(*engine, seed);
    auto broken = seed;
    const auto at = broken.find("A test identifier");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
      broken.insert(at + 8, 1, '"');
      const auto after = project(*engine, broken);
      CHECK(after.accepted && after.recovered);
      for (const std::string token : {"req", "applicable", "for"})
        CHECK(std::count(after.tokens.begin(), after.tokens.end(), token)
              == std::count(before.tokens.begin(), before.tokens.end(), token));
    }
  }
  language(lane, *engine, "valid.req",
           {"01_typo1.req", "02_typo2.req", "03_closer_block.req",
            "04_closer_eof.req", "05_intruder.req", "06_truncated.req",
            "07_two_faults.req", "08_head_fault.req", "09_list_delim.req",
            "10_noise.req", "11_head_typo.req"});
  mutation_oracle(*engine, "requirements", document("valid.req"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("requirements_recovery_test");
}
