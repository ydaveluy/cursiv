// domainmodel: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/CursivActions.hpp"
#include "domainmodel/DomainModelGrammar.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace metric = cursiv::recovery_metric;

namespace {

namespace g = dm::generated::DomainModel;
const cursiv::fuzzer::CursivActions<dm::DomainModel> kActions{};

const metric::Registered kLanguage{{
    .rank = 5,
    .name = "domainmodel",
    .parser = &g::createParser,
    .reflection = &g::createAstReflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              metric::source_file("examples/domainmodel/tests/recovery/valid.dmodel"),
              metric::source_file("examples/domainmodel/example/blog.dmodel"),
              metric::source_file("examples/domainmodel/example.dmodel"),
              bench::generateDomainModelSource(3072)};
        },
}};

}  // namespace
