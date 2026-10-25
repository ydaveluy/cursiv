#pragma once

#include <memory>
#include <string>

#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "requirements/core/CoreServices.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Tests/Module.hpp"

// The two languages of this example, each with its own module: the rule set is
// shared, the entry rule and the checks are not.

namespace req {

void install_requirements_core_module(RequirementsCoreServices& services);
void install_tests_core_module(RequirementsCoreServices& services);

std::unique_ptr<RequirementsCoreServices> create_requirements_core_services(
    RequirementsSharedCoreServices& sharedServices,
    std::string languageId =
        std::string(req::generated::Requirements::kLanguageId));
std::unique_ptr<RequirementsCoreServices> create_tests_core_services(
    RequirementsSharedCoreServices& sharedServices,
    std::string languageId = std::string(req::generated::Tests::kLanguageId));

// The whole world in one call: the shared container built over `files` -- none
// meaning no disk under it -- with both languages registered in it.
struct RequirementsCoreLanguage {
  std::unique_ptr<RequirementsSharedCoreServices> shared;
  RequirementsCoreServices& requirements;
  RequirementsCoreServices& tests;
};

RequirementsCoreLanguage create_core_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

// Both, in one call: a binary that served one would refuse half its own
// example -- a `.tst` names the requirements of the `.req` beside it.
bool register_core_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace req
