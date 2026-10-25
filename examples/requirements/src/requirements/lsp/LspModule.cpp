#include "requirements/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "requirements/core/CoreModule.hpp"
#include "requirements/lsp/RequirementsFormatter.hpp"

namespace req {

namespace {

// The formatter is the one of the language being built, not of the container:
// one container type serves both grammars, and a `.tst` laid out by the `.req`
// rules would be laid out wrong.
using MakeFormatter =
    std::unique_ptr<const cursiv::Formatter> (*)(const cursiv::CoreServices&);

std::unique_ptr<RequirementsServices> build(
    RequirementsSharedServices& sharedServices, std::string languageId,
    void (*installCore)(RequirementsCoreServices&),
    MakeFormatter makeFormatter) {
  auto services = std::make_unique<RequirementsServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  cursiv::install_default_core_services(*services);
  installCore(*services);
  install_lsp_module(*services);
  // Built on the container it will read, and after it: a formatter holds the
  // services by reference.
  services->formatter = makeFormatter(*services);
  return services;
}

template <class F>
std::unique_ptr<const cursiv::Formatter> make(
    const cursiv::CoreServices& services) {
  return std::make_unique<const F>(services);
}

}  // namespace

void install_lsp_module(RequirementsServices& services) {
  cursiv::install_default_lsp_services(services);
}

std::unique_ptr<RequirementsServices> create_requirements_lsp_services(
    RequirementsSharedServices& sharedServices, std::string languageId) {
  return build(sharedServices, std::move(languageId),
               &install_requirements_core_module, &make<RequirementsFormatter>);
}

std::unique_ptr<RequirementsServices> create_tests_lsp_services(
    RequirementsSharedServices& sharedServices, std::string languageId) {
  return build(sharedServices, std::move(languageId),
               &install_tests_core_module, &make<TestsFormatter>);
}

RequirementsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  auto shared = std::make_unique<RequirementsSharedServices>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<cursiv::EmptyFileSystemProvider>();
  cursiv::install_default_shared_lsp_services(*shared);
  auto requirements = create_requirements_lsp_services(*shared);
  auto tests = create_tests_lsp_services(*shared);
  RequirementsServices& one = *requirements;
  RequirementsServices& other = *tests;
  shared->serviceRegistry->registerServices(std::move(requirements));
  shared->serviceRegistry->registerServices(std::move(tests));
  return {std::move(shared), one, other};
}

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  auto& shared = dynamic_cast<RequirementsSharedServices&>(sharedServices);
  sharedServices.serviceRegistry->registerServices(
      create_requirements_lsp_services(shared));
  sharedServices.serviceRegistry->registerServices(
      create_tests_lsp_services(shared));
  return true;
}

}  // namespace req
