// statemachine: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"
#include "statemachine/core/StateMachineGrammar.hpp"
#include "statemachine/generated/SM/AstReflection.hpp"
#include "statemachine/generated/SM/Grammar.hpp"
#include "statemachine/generated/SM/Module.hpp"
#include "statemachine/generated/SM/Parser.hpp"
#include "statemachine/generated/SM/Table.hpp"

namespace {

namespace g = sm::generated::SM;
const cursiv::fuzzer::CursivActions<sm::SM> kActions{};

const recovery_metric::Registered kLanguage{{
    .rank = 4,
    .name = "statemachine",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              recovery_metric::source_file("examples/statemachine/tests/recovery/valid.statemachine"),
              recovery_metric::source_file("examples/statemachine/example/doors.statemachine"),
              recovery_metric::source_file("examples/statemachine/example/trafficlight.statemachine"),
              bench::generateStatemachineSource(3072)};
        },
    .ceilings = &recovery_metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &recovery_metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
