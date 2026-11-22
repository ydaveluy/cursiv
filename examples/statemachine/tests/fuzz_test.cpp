// Commands, events, transitions: three lists that reference each other.

#include <memory>

#include "cursiv/FuzzLane.hpp"
#include "statemachine/StateMachineGrammar.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace {
void fuzz_statemachine() {
  namespace g = sm::generated::SM;
  const std::unique_ptr<cursiv::Parser> parser = g::createParser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::createAstReflection();
  cursiv::fuzzer::lane<sm::SM>("statemachine", *parser, *reflection, 400,
                               0x5eed07);
}
}  // namespace

int main() {
  fuzz_statemachine();
  return cursiv::test::report("statemachine_fuzz_test");
}
