// The LSP-wide container: what every language of a session shares, from
// the server the client drives to the client the server answers. The
// server is always installed; the client is null outside a session.
#pragma once

#include <memory>

#include "cursiv/services/ServiceSlot.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv {

class ConfigurationProvider;
class DocumentSaveHandler;
class ExecuteCommandHandler;
class FileOperationHandler;
class FuzzyMatcher;
class LanguageClient;
class LanguageServer;
class McpService;
class NodeKindProvider;
class NotebookDocuments;
class TextDocumentContentProvider;
class WorkspaceSymbolProvider;

// Virtual inheritance, so a workspace that is both a language's own
// shared container and an LSP one holds one `SharedCoreServices` through
// the diamond.
struct SharedLspServices : virtual SharedCoreServices {
  // Shared: a workspace symbol query reads the index across every language.
  std::unique_ptr<const WorkspaceSymbolProvider> workspaceSymbolProvider;
  std::unique_ptr<const NodeKindProvider> nodeKindProvider;
  std::unique_ptr<const FuzzyMatcher> fuzzyMatcher;
  // Null by default: a command belongs to the workspace, not to a language.
  std::unique_ptr<const ExecuteCommandHandler> executeCommandHandler;
  // Null by default: a file operation crosses the languages.
  std::unique_ptr<FileOperationHandler> fileOperationHandler;
  // Null by default: a save crosses the languages.
  std::unique_ptr<DocumentSaveHandler> documentSaveHandler;
  // Null by default. Installed, a notebook's cells are documents like any
  // other.
  std::unique_ptr<NotebookDocuments> notebookDocuments;
  // Null by default: MCP is a second way into the same workspace.
  std::unique_ptr<const McpService> mcpService;
  SharedLspServices() noexcept;
  ~SharedLspServices() noexcept override;

  std::unique_ptr<LanguageServer> languageServer;
  // Null outside a session.
  std::unique_ptr<LanguageClient> languageClient;
  // Null unless a workspace serves virtual documents; the capability follows.
  std::unique_ptr<TextDocumentContentProvider> textDocumentContentProvider;
  // Always installed: a client that announces nothing gets a provider that
  // answers nothing.
  std::unique_ptr<ConfigurationProvider> configurationProvider;

  void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const override;
};

// Installs the shared core defaults, then the LSP ones.
void install_default_shared_lsp_services(SharedLspServices& shared);

}  // namespace cursiv
