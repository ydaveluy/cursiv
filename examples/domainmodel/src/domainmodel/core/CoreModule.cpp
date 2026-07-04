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

void installDomainModelCoreModule(DomainModelCoreServices &services) {
  services.parser = std::make_unique<const parser::DomainModelParser>(services);
  services.languageMetaData.fileExtensions = {".dmodel"};
  services.qualifiedNameProvider =
      std::make_shared<const references::QualifiedNameProvider>();
  services.references.scopeComputation =
      std::make_unique<references::DomainModelScopeComputation>(services);
  services.validator =
      std::make_unique<validation::DomainModelValidator>(services);
  validation::registerValidationChecks(services, *services.validator);
}

std::unique_ptr<DomainModelCoreServices>
createDomainModelCoreServices(
    const DomainModelSharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<DomainModelCoreServices>(
      sharedServices, std::move(languageId));
  installDomainModelCoreModule(*services);
  return services;
}

bool registerDomainModelCoreServices(pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(createDomainModelCoreServices(
      dynamic_cast<DomainModelSharedCoreServices &>(sharedServices)));
  return true;
}

} // namespace domainmodel
