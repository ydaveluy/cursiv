#pragma once

#include <memory>
#include <string>

#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Tests/Module.hpp"
#include "requirements/lsp/LspServices.hpp"

namespace req {

void install_lsp_module(RequirementsServices& services);

std::unique_ptr<RequirementsServices> create_requirements_lsp_services(
    RequirementsSharedServices& sharedServices,
    std::string languageId =
        std::string(req::generated::Requirements::kLanguageId));
std::unique_ptr<RequirementsServices> create_tests_lsp_services(
    RequirementsSharedServices& sharedServices,
    std::string languageId = std::string(req::generated::Tests::kLanguageId));

// The same one call, one storey up: both languages of the example.
struct RequirementsLanguage {
  std::unique_ptr<RequirementsSharedServices> shared;
  RequirementsServices& requirements;
  RequirementsServices& tests;
};

RequirementsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace req
