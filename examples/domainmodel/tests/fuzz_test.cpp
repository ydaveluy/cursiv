// Cross references and entity inheritance.

#include <memory>

#include "cursiv/fuzz/FuzzLane.hpp"
#include "domainmodel/core/DomainModelGrammar.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

namespace {
void fuzz_domainmodel() {
  namespace g = dm::generated::DomainModel;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<dm::DomainModel>(
      "domainmodel", *parser, *reflection, 400, 0x5eed06);
}
}  // namespace

int main() {
  fuzz_domainmodel();
  return cursiv::test::report("domainmodel_fuzz_test");
}
