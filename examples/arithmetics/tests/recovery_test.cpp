#include <memory>
#include <string_view>

#include "arithmetics/generated/Arith/Module.hpp"
#include "cursiv/test/RecoveryLane.hpp"
#include "cursiv/test/MutationOracle.hpp"

namespace {
using namespace cursiv::test;

void run(Lane& lane) {
  lane.lang = "arithmetics";
  const std::unique_ptr<cursiv::Parser> engine =
      arith::generated::Arith::create_parser();
  language(lane, *engine, "valid.calc",
           {"01_typo1.calc", "02_typo2.calc", "03_closer_block.calc",
            "04_closer_eof.calc", "05_intruder.calc", "06_truncated.calc",
            "07_two_faults.calc", "08_head_fault.calc", "09_list_delim.calc",
            "10_noise.calc", "11_head_typo.calc",
            "12_open_region_eof.calc"});
  mutation_oracle(*engine, "arithmetics", document("valid.calc"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("arithmetics_recovery_test");
}
