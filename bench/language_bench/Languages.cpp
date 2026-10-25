#include "language_bench/Languages.hpp"

#include "arithmetics/lsp/LspModule.hpp"
#include "bench.hpp"
#include "domainmodel/lsp/LspModule.hpp"
#include "requirements/lsp/LspModule.hpp"
#include "statemachine/lsp/LspModule.hpp"

namespace cursiv::bench::examples {

std::span<const Language> languages() {
  static const Language languages[] = {
      make_language<arith::ArithmeticsSharedServices,
                    arith::register_lsp_services>(
          "arithmetics", "arithmetics", ".calc",
          ::bench::generateArithmeticsSource),
      make_language<dm::DomainModelSharedServices, dm::register_lsp_services>(
          "domainmodel", "domain-model", ".dmodel",
          ::bench::generateDomainModelSource, ::bench::multidocDomainModelFile),
      make_language<req::RequirementsSharedServices,
                    req::register_lsp_services>(
          "requirements", "requirements-lang", ".req",
          ::bench::generateRequirementsSource,
          ::bench::multidocRequirementsFile, Refs::Cross),
      make_language<sm::StateMachineSharedServices, sm::register_lsp_services>(
          "statemachine", "statemachine", ".statemachine",
          ::bench::generateStatemachineSource,
          ::bench::multidocStatemachineFile),
  };
  return languages;
}

}  // namespace cursiv::bench::examples
