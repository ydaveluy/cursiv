#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace lsp {
class Connection;
}

namespace pegium {

/// Initializes shared services with the pieces required by an LSP server process.
void initializeSharedServicesForLanguageServer(
    SharedServices &sharedServices, ::lsp::Connection &connection);
/// Installs the default LSP feature services on one language service container.
///
/// `installDefaultCoreServices(...)` must already have been called.
void installDefaultLspServices(Services &services);
/// Installs the default shared LSP services.
///
/// `installDefaultSharedCoreServices(...)` must already have been called.
void installDefaultSharedLspServices(SharedServices &sharedServices);

/// Builds one language service container with the default core and LSP services
/// installed. `TShared` is deduced from `sharedServices`, so passing a custom
/// `SharedServices` subclass constructs the container from that exact type — its
/// constructor can take the custom shared reference directly.
template <typename TServices = pegium::Services, typename TShared>
[[nodiscard]] std::unique_ptr<TServices>
makeDefaultServices(const TShared &sharedServices, std::string languageId) {
  static_assert(std::is_base_of_v<pegium::Services, TServices>,
                "TServices must derive from pegium::Services.");
  static_assert(std::is_base_of_v<pegium::SharedServices, TShared>,
                "TShared must derive from pegium::SharedServices.");
  static_assert(std::is_constructible_v<TServices, const TShared &>,
                "TServices must be constructible from const TShared&.");

  auto services = std::make_unique<TServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  installDefaultCoreServices(*services);
  pegium::installDefaultLspServices(*services);
  return services;
}

} // namespace pegium
