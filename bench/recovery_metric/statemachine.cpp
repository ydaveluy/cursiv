// statemachine: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/CursivActions.hpp"
#include "recovery_metric/MetricLanguage.hpp"
#include "statemachine/StateMachineGrammar.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = sm::generated::SM;
const cursiv::fuzzer::CursivActions<sm::SM> kActions{};

const metric::Registered kLanguage{{
    .rank = 4,
    .name = "statemachine",
    .parser = &g::createParser,
    .reflection = &g::createAstReflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              metric::source_file("examples/statemachine/tests/recovery/valid.statemachine"),
              metric::source_file("examples/statemachine/example/doors.statemachine"),
              metric::source_file("examples/statemachine/example/trafficlight.statemachine"),
              bench::generateStatemachineSource(3072)};
        },
}};

}  // namespace
