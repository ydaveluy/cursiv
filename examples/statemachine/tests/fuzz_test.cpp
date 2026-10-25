// Commands, events, transitions: three lists that reference each other.

#include <memory>

#include "cursiv/fuzz/FuzzLane.hpp"
#include "statemachine/core/StateMachineGrammar.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace {
void fuzz_statemachine() {
  namespace g = sm::generated::SM;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<sm::SM>("statemachine", *parser, *reflection, 400, 0x5eed07);
}
}  // namespace

int main() {
  fuzz_statemachine();
  return cursiv::test::report("statemachine_fuzz_test");
}
