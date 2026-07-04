#pragma once

#include <memory>

#include <pegium/lsp/workspace/TextDocuments.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/lsp/workspace/DocumentUpdateHandler.hpp>
#include <pegium/lsp/services/ExecuteCommandHandler.hpp>
#include <pegium/lsp/workspace/FileOperationHandler.hpp>
#include <pegium/lsp/support/FuzzyMatcher.hpp>
#include <pegium/lsp/runtime/LanguageClient.hpp>
#include <pegium/lsp/runtime/LanguageServer.hpp>
#include <pegium/lsp/symbols/NodeKindProvider.hpp>
#include <pegium/lsp/symbols/WorkspaceSymbolProvider.hpp>

namespace pegium {

/// Shared LSP services reused by all registered languages; installed by default.
struct SharedLspServices {
  // null outside an active LSP session
  std::unique_ptr<pegium::LanguageClient> languageClient;
  std::shared_ptr<pegium::TextDocuments> textDocuments;
  std::unique_ptr<pegium::DocumentUpdateHandler> documentUpdateHandler;
  // optional
  std::unique_ptr<pegium::ExecuteCommandHandler> executeCommandHandler;
  // optional
  std::unique_ptr<pegium::FileOperationHandler> fileOperationHandler;
  std::unique_ptr<FuzzyMatcher> fuzzyMatcher;
  std::unique_ptr<pegium::LanguageServer> languageServer;
  std::unique_ptr<NodeKindProvider> nodeKindProvider;
  std::unique_ptr<WorkspaceSymbolProvider> workspaceSymbolProvider;
};

/// Root shared service container for Pegium core plus LSP runtime state.
/// SharedCoreServices is a virtual base so a language can join a custom
/// shared-core and shared-LSP container under one diamond.
struct SharedServices : virtual pegium::SharedCoreServices {
  SharedLspServices lsp;

  SharedServices();
  // Non-movable for the same reason as its SharedCoreServices base: installed
  // LSP services hold a back-reference to this SharedServices.
  SharedServices(SharedServices &&) noexcept = delete;
  SharedServices &operator=(SharedServices &&) noexcept = delete;
  SharedServices(const SharedServices &) = delete;
  SharedServices &operator=(const SharedServices &) = delete;
  ~SharedServices() noexcept override;

  /// True when the shared core services and the required shared LSP runtime
  /// services are all present. Optional LSP pieces (languageClient,
  /// executeCommandHandler, fileOperationHandler) are not required.
  [[nodiscard]] bool isComplete() const noexcept override;
};

} // namespace pegium
