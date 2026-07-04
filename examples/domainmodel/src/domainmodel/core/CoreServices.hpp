#pragma once

#include <memory>

#include <domainmodel/core/references/QualifiedNameProvider.hpp>
#include <domainmodel/core/validation/DomainModelValidator.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace domainmodel {

/// Workspace-wide shared services.
struct DomainModelSharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) language services, built from the language's shared-core type.
struct DomainModelCoreServices
    : pegium::CoreServicesFor<DomainModelSharedCoreServices> {
  std::shared_ptr<const references::QualifiedNameProvider> qualifiedNameProvider;
  std::unique_ptr<validation::DomainModelValidator> validator;

  explicit DomainModelCoreServices(
      const DomainModelSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace domainmodel
