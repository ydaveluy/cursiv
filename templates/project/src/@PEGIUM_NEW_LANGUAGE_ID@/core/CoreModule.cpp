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

void install@PEGIUM_NEW_CLASS@CoreModule(pegium::CoreServices &core) {
  core.parser = std::make_unique<const parser::@PEGIUM_NEW_CLASS@Parser>(core);
  core.languageMetaData.fileExtensions = {"@PEGIUM_NEW_EXT@"};
}

std::unique_ptr<@PEGIUM_NEW_CLASS@CoreServices>
create@PEGIUM_NEW_CLASS@CoreServices(const pegium::SharedCoreServices &sharedServices,
                     std::string languageId) {
  auto services = std::make_unique<@PEGIUM_NEW_CLASS@CoreServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  pegium::installDefaultCoreServices(*services);
  install@PEGIUM_NEW_CLASS@CoreModule(*services);
  return services;
}

bool register@PEGIUM_NEW_CLASS@CoreServices(pegium::SharedCoreServices &sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      create@PEGIUM_NEW_CLASS@CoreServices(sharedServices));
  return true;
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
