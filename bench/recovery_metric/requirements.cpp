// requirements: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"
#include "requirements/core/RequirementsGrammar.hpp"
#include "requirements/generated/Requirements/AstReflection.hpp"
#include "requirements/generated/Requirements/Grammar.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Requirements/Parser.hpp"
#include "requirements/generated/Requirements/Table.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = req::generated::Requirements;
const cursiv::fuzzer::CursivActions<req::Requirements> kActions{};

const metric::Registered kLanguage{{
    .rank = 3,
    .name = "requirements",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              metric::source_file("examples/requirements/tests/recovery/valid.req"),
              metric::source_file("examples/requirements/example/requirements.req"),
              metric::source_file("examples/requirements/example.req"),
              bench::generateRequirementsSource(3072)};
        },
    .ceilings = &metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
