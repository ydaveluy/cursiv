// requirements: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/CursivActions.hpp"
#include "recovery_metric/MetricLanguage.hpp"
#include "requirements/RequirementsGrammar.hpp"
#include "requirements/generated/Requirements/Module.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = req::generated::Requirements;
const cursiv::fuzzer::CursivActions<req::Requirements> kActions{};

const metric::Registered kLanguage{{
    .rank = 3,
    .name = "requirements",
    .parser = &g::createParser,
    .reflection = &g::createAstReflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              metric::source_file("examples/requirements/tests/recovery/valid.req"),
              metric::source_file("examples/requirements/example/requirements.req"),
              metric::source_file("examples/requirements/example.req"),
              bench::generateRequirementsSource(3072)};
        },
}};

}  // namespace
