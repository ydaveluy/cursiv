#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp>

#include <utility>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

void install@PEGIUM_NEW_CLASS@LspModule(@PEGIUM_NEW_CLASS@Services &services) {
  // Register your LSP overrides here, e.g.:
  //   services.lsp.formatter = std::make_unique<@PEGIUM_NEW_CLASS@Formatter>(services);
  (void)services;
}

std::unique_ptr<@PEGIUM_NEW_CLASS@Services>
create@PEGIUM_NEW_CLASS@LspServices(
    const @PEGIUM_NEW_CLASS@SharedServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultServices<@PEGIUM_NEW_CLASS@Services>(
      sharedServices, std::move(languageId));
  install@PEGIUM_NEW_CLASS@CoreModule(*services);
  install@PEGIUM_NEW_CLASS@LspModule(*services);
  return services;
}

bool register@PEGIUM_NEW_CLASS@LspServices(pegium::SharedServices &sharedServices) {
  // The session container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      create@PEGIUM_NEW_CLASS@LspServices(
          dynamic_cast<@PEGIUM_NEW_CLASS@SharedServices &>(sharedServices)));
  return true;
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
