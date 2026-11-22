// The domainmodel command line: the usual check, plus `generate FILE`: one
// Java class per entity, in the directory a package spells.

#include "cursiv/cli.hpp"

#include <iostream>
#include <optional>
#include <string_view>

#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/workspace/Document.hpp"
#include "domainmodel/CoreServices.hpp"
#include "domainmodel/Generator.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::registerLanguage<dm::DomainModelCoreServices,
                               dm::DomainModelSharedCoreServices,
                               dm::install_core_module>;

int generate(const cursiv::Document& document, std::string_view path,
             std::optional<std::string_view> destination) {
  if (cursiv::hasErrorDiagnostics(document)) {
    std::cerr << "There are validation errors:\n";
    cursiv::printDiagnostics(document, path, std::cerr);
    return cursiv::kDiagnostics;
  }
  const auto* const model =
      cursiv::ast_cast<const dm::ast::DomainModel>(document.parseResult.value);
  if (model == nullptr) {
    std::cerr << "Unable to generate Java for an invalid domain model.\n";
    return cursiv::kDiagnostics;
  }

  std::cout << dm::generate_java(*model, path, destination) << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  return cursiv::runCli<dm::DomainModelSharedCoreServices>(
      argc, argv, kRegister,
      {{.name = "generate",
        .run = generate,
        .acceptsDestination = true,
        .loadSiblings = true}});
}
