// The pure infix form: the precedence ladder and its parentheses.

#include <memory>

#include "arithmetics/core/ArithmeticsGrammar.hpp"
#include "arithmetics/generated/Arith/Module.hpp"
#include "cursiv/fuzz/FuzzLane.hpp"

namespace {
void fuzz_arithmetics() {
  namespace g = arith::generated::Arith;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<arith::Arith>(
      "arithmetics", *parser, *reflection, 400, 0x5eed09);
}
}  // namespace

int main() {
  fuzz_arithmetics();
  return cursiv::test::report("arithmetics_fuzz_test");
}
