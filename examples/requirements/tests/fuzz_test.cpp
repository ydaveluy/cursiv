// Two entries over one rule set: each closure is a lane of its own.

#include <memory>

#include "cursiv/fuzz/FuzzLane.hpp"
#include "requirements/core/RequirementsGrammar.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Tests/Module.hpp"

namespace {
void fuzz_requirements() {
  namespace g = req::generated::Requirements;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<req::Requirements>(
      "requirements", *parser, *reflection, 400, 0x5eed0c);
}
void fuzz_reqtests() {
  namespace g = req::generated::Tests;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<req::Tests>("reqtests", *parser, *reflection, 400, 0x5eed0d);
}
}  // namespace

int main() {
  fuzz_requirements();
  fuzz_reqtests();
  return cursiv::test::report("requirements_fuzz_test");
}
