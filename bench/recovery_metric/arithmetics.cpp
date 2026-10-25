// arithmetics: the example documents and generated ones.

#include <string>
#include <vector>

#include "arithmetics/core/ArithmeticsGrammar.hpp"
#include "arithmetics/generated/Arith/AstReflection.hpp"
#include "arithmetics/generated/Arith/Grammar.hpp"
#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "bench.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace {

namespace g = arith::generated::Arith;
const cursiv::fuzzer::CursivActions<arith::Arith> kActions{};

const recovery_metric::Registered kLanguage{{
    .rank = 6,
    .name = "arithmetics",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              recovery_metric::source_file("examples/arithmetics/tests/recovery/valid.calc"),
              recovery_metric::source_file("examples/arithmetics/example/demo.calc"),
              recovery_metric::source_file("examples/arithmetics/example.calc"),
              bench::generateArithmeticsSource(2048)};
        },
    .ceilings = &recovery_metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &recovery_metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
