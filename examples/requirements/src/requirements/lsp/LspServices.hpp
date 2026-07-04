#pragma once

#include <requirements/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace requirements {

/// Workspace-wide shared services in an LSP session, shared by both languages.
struct RequirementsSharedServices : RequirementsSharedCoreServices,
                                    pegium::SharedServices {};

/// LSP-enabled requirements language services.
struct RequirementsServices final
    : pegium::ServicesFor<RequirementsCoreServices, RequirementsSharedServices> {
  explicit RequirementsServices(const RequirementsSharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

/// LSP-enabled tests language services.
struct TestsServices final
    : pegium::ServicesFor<TestsCoreServices, RequirementsSharedServices> {
  explicit TestsServices(const RequirementsSharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace requirements
