#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>

#include <utility>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/@PEGIUM_NEW_CLASS@Parser.hpp>

// This is the single translation unit that includes the grammar header, so the
// heavy grammar template instantiation happens here once, not in every module
// translation unit; the lsp module reaches the wiring through the declaration.

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

std::unique_ptr<const pegium::parser::Parser> create@PEGIUM_NEW_CLASS@Parser() {
  return std::make_unique<const parser::@PEGIUM_NEW_CLASS@Parser>();
}

void install@PEGIUM_NEW_CLASS@CoreModule(@PEGIUM_NEW_CLASS@CoreServices &services) {
  services.parser =
      std::make_unique<const parser::@PEGIUM_NEW_CLASS@Parser>(services);
  services.languageMetaData.fileExtensions = {"@PEGIUM_NEW_EXT@"};
  services.validator =
      std::make_unique<validation::@PEGIUM_NEW_CLASS@Validator>(services);
  validation::registerValidationChecks(services, *services.validator);
}

std::unique_ptr<@PEGIUM_NEW_CLASS@CoreServices>
create@PEGIUM_NEW_CLASS@CoreServices(
    const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<@PEGIUM_NEW_CLASS@CoreServices>(
      sharedServices, std::move(languageId));
  install@PEGIUM_NEW_CLASS@CoreModule(*services);
  return services;
}

bool register@PEGIUM_NEW_CLASS@CoreServices(pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      create@PEGIUM_NEW_CLASS@CoreServices(
          dynamic_cast<@PEGIUM_NEW_CLASS@SharedCoreServices &>(sharedServices)));
  return true;
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
