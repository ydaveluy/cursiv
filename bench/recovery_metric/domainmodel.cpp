// domainmodel: the example documents and generated ones.

#include <string>
#include <vector>

#include "bench.hpp"
#include "cursiv/fuzz/CursivActions.hpp"
#include "domainmodel/core/DomainModelGrammar.hpp"
#include "domainmodel/generated/DomainModel/AstReflection.hpp"
#include "domainmodel/generated/DomainModel/Grammar.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "domainmodel/generated/DomainModel/Parser.hpp"
#include "domainmodel/generated/DomainModel/Table.hpp"
#include "recovery_metric/Searching.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace {

namespace g = dm::generated::DomainModel;
const cursiv::fuzzer::CursivActions<dm::DomainModel> kActions{};

const recovery_metric::Registered kLanguage{{
    .rank = 5,
    .name = "domainmodel",
    .parser = &g::create_parser,
    .reflection = &g::create_ast_reflection,
    .actions = &kActions,
    .documents =
        [] {
          return std::vector<std::string>{
              recovery_metric::source_file("examples/domainmodel/tests/recovery/valid.dmodel"),
              recovery_metric::source_file("examples/domainmodel/example/blog.dmodel"),
              recovery_metric::source_file("examples/domainmodel/example.dmodel"),
              bench::generateDomainModelSource(3072)};
        },
    .ceilings = &recovery_metric::ceilings<g::kTable, g::kDrive,
                                          g::kGrammarMetaData, g::kAstActions>,
    .searching = &recovery_metric::searching<g::kTable, g::kDrive,
                                                g::kGrammarMetaData,
                                                g::kAstActions>,
}};

}  // namespace
