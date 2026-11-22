#include "language_bench/Languages.hpp"

#include "arithmetics/LspModule.hpp"
#include "bench.hpp"
#include "domainmodel/LspModule.hpp"
#include "requirements/LspModule.hpp"
#include "statemachine/LspModule.hpp"

namespace cursiv::bench::examples {

std::span<const Language> languages() {
  static const Language languages[] = {
      makeLanguage<arith::ArithmeticsSharedServices,
                    arith::register_lsp_services>(
          "arithmetics", "arithmetics", ".calc",
          ::bench::generateArithmeticsSource),
      makeLanguage<dm::DomainModelSharedServices, dm::register_lsp_services>(
          "domainmodel", "domain-model", ".dmodel",
          ::bench::generateDomainModelSource, ::bench::multidocDomainModelFile),
      makeLanguage<req::RequirementsSharedServices,
                    req::register_lsp_services>(
          "requirements", "requirements-lang", ".req",
          ::bench::generateRequirementsSource,
          ::bench::multidocRequirementsFile, Refs::Cross),
      makeLanguage<sm::StateMachineSharedServices, sm::register_lsp_services>(
          "statemachine", "statemachine", ".statemachine",
          ::bench::generateStatemachineSource,
          ::bench::multidocStatemachineFile),
  };
  return languages;
}

}  // namespace cursiv::bench::examples
