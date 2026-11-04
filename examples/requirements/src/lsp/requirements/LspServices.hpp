#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "requirements/CoreServices.hpp"

namespace req {

struct RequirementsSharedServices : RequirementsSharedCoreServices,
                                    cursiv::SharedLspServices {};

struct RequirementsServices final
    : cursiv::LspServicesFor<RequirementsCoreServices, RequirementsSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace req
