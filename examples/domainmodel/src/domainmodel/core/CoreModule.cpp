#include <domainmodel/core/CoreModule.hpp>

#include <utility>

#include <domainmodel/core/DomainModelParser.hpp>
#include <domainmodel/core/references/DomainModelScopeComputation.hpp>
#include <domainmodel/core/references/QualifiedNameProvider.hpp>
#include <domainmodel/core/validation/DomainModelValidator.hpp>

// This is the single translation unit that includes the grammar header
// (DomainModelParser.hpp), so the heavy grammar template instantiation happens
// here once; the lsp module reaches the wiring through the declaration only.

namespace domainmodel {

std::unique_ptr<const pegium::parser::Parser> createDomainModelParser() {
  return std::make_unique<const parser::DomainModelParser>();
}

void installDomainModelCoreModule(pegium::CoreServices &core,
                                  DomainModelAddedServices &added) {
  core.parser = std::make_unique<const parser::DomainModelParser>(core);
  core.languageMetaData.fileExtensions = {".dmodel"};
  added.qualifiedNameProvider =
      std::make_shared<const references::QualifiedNameProvider>();
  core.references.scopeComputation =
      std::make_unique<references::DomainModelScopeComputation>(core, added);
  added.validator = std::make_unique<validation::DomainModelValidator>();
  validation::registerValidationChecks(core, *added.validator);
}

std::unique_ptr<DomainModelCoreServices>
createDomainModelCoreServices(const pegium::SharedCoreServices &sharedServices,
                              std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<DomainModelCoreServices>(
      sharedServices, std::move(languageId));
  installDomainModelCoreModule(*services, *services);
  return services;
}

bool registerDomainModelCoreServices(pegium::SharedCoreServices &sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      createDomainModelCoreServices(sharedServices));
  return true;
}

} // namespace domainmodel
