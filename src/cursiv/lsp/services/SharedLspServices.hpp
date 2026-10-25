// The LSP-wide container: what every language of a session shares, from
// the server the client drives to the client the server answers. The
// server is always installed; the client is null outside a session.
#pragma once

#include <memory>

#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

namespace cursiv {

class ConfigurationProvider;
class ExecuteCommandHandler;
class FileOperationHandler;
class FuzzyMatcher;
class LanguageClient;
class LanguageServer;
class McpService;
class NodeKindProvider;
class TextDocumentContentProvider;
class WorkspaceSymbolProvider;

// Virtual inheritance, so a workspace that is both a language's own
// shared container and an LSP one holds one `SharedCoreServices` through
// the diamond.
struct SharedLspServices : virtual SharedCoreServices {
  // The one navigation door that is not a document's: it reads the index, so a
  // symbol of a file nobody opened is reachable. Shared, because a query
  // crosses every language of the workspace.
  std::unique_ptr<const WorkspaceSymbolProvider> workspaceSymbolProvider;
  std::unique_ptr<const NodeKindProvider> nodeKindProvider;
  std::unique_ptr<const FuzzyMatcher> fuzzyMatcher;
  // Shared, and null by default: a command belongs to the workspace, not to a
  // language, and a server that runs none is complete.
  std::unique_ptr<const ExecuteCommandHandler> executeCommandHandler;
  // Shared and null by default, for the same reason: what a file operation
  // means crosses the languages, and a server that watches none is complete.
  // Not const -- the three `did` arms are told about a world that moved.
  std::unique_ptr<FileOperationHandler> fileOperationHandler;
  // Shared and null by default: MCP is a second door onto the same workspace,
  // and a session that opens none is complete.
  std::unique_ptr<const McpService> mcpService;
  SharedLspServices() noexcept;
  ~SharedLspServices() noexcept override;

  std::unique_ptr<LanguageServer> languageServer;
  // Null outside a session.
  std::unique_ptr<LanguageClient> languageClient;
  // Null unless a workspace serves virtual documents; the capability follows.
  std::unique_ptr<TextDocumentContentProvider> textDocumentContentProvider;
  // Always installed, like the server: a workspace whose client announces
  // nothing gets a provider that answers nothing, which is a state rather than
  // an absence. Keyed here and not in the core container because it speaks
  // `lsp::InitializeParams` and `LSPAny`, which the core may not include.
  std::unique_ptr<ConfigurationProvider> configurationProvider;

  void visitServices(
      cursiv::function_ref<void(const ServiceSlot&)> yield) const override;
};

/**
 * Installs the core-wide defaults, then this storey's.
 */
void install_default_shared_lsp_services(SharedLspServices& shared);

}  // namespace cursiv
