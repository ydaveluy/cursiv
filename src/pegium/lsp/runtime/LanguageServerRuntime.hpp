#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <string_view>

#include <pegium/lsp/services/ServiceRequirements.hpp>
#include <pegium/core/utils/Disposable.hpp>

namespace lsp {
class Connection;
class MessageHandler;
}

namespace pegium {
struct SharedServices;
}

namespace pegium {

class FileOperationHandler;

/// Registers the workspace-configuration change handler.
void addConfigurationChangeHandler(
    ::lsp::MessageHandler &messageHandler,
    const pegium::SharedServices &sharedServices,
    std::function<void()> ensureInitialized = {});

/// Bridges document diagnostics to the LSP message handler.
void addDiagnosticsHandler(::lsp::MessageHandler &messageHandler,
                           pegium::SharedServices &sharedServices,
                           utils::DisposableStore &disposables);

/// Bridges text-document notifications to the configured update handler.
void addDocumentUpdateHandler(
    ::lsp::MessageHandler &messageHandler,
    pegium::SharedServices &sharedServices,
    std::function<void()> ensureInitialized,
    utils::DisposableStore &disposables);

/// Registers file operation request and notification handlers.
void addFileOperationHandler(::lsp::MessageHandler &messageHandler,
                             FileOperationHandler &fileOperationHandler,
                             const std::function<void()> &ensureInitialized);

namespace detail {
/// Runs the full server lifecycle (arg parsing, stream/socket selection, init,
/// registration, start) on a caller-constructed shared container. The templated
/// `runLanguageServerMain` is a thin wrapper that only chooses the container's
/// concrete type; all the real work lives here, non-templated, in the .cpp.
[[nodiscard]] int runLanguageServerMainOn(
    int argc, char **argv, std::string_view serverName,
    const std::function<bool(pegium::SharedServices &)> &registerLanguageServices,
    const ServiceRequirements &serviceRequirements,
    pegium::SharedServices &sharedServices);
} // namespace detail

/// Runs a complete language-server process from command-line entry parameters.
///
/// `TShared` is the shared-service container constructed for the session
/// (default `pegium::SharedServices`). A language may pass its own
/// `SharedServices` subclass to add workspace-wide shared services; it must be
/// default-constructible. Existing calls that omit the template argument keep
/// the default container unchanged.
template <typename TShared = SharedServices>
  requires std::derived_from<TShared, SharedServices>
[[nodiscard]] int runLanguageServerMain(
    int argc, char **argv, std::string_view serverName,
    const std::function<bool(pegium::SharedServices &)> &registerLanguageServices,
    const ServiceRequirements &serviceRequirements = {}) {
  auto sharedServices = std::make_unique<TShared>();
  return detail::runLanguageServerMainOn(argc, argv, serverName,
                                         registerLanguageServices,
                                         serviceRequirements, *sharedServices);
}

/// Starts the language server on an existing connection.
[[nodiscard]] int startLanguageServer(
    pegium::SharedServices &sharedServices, ::lsp::Connection &connection,
    const ServiceRequirements &serviceRequirements = {});

} // namespace pegium
