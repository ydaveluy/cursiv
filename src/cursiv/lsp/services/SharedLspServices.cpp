// Every slot's type must be complete here, even the ones this file never
// names: a `ServiceSlot` holds a `unique_ptr`, whose destructor needs the
// whole type. An include-cleaner sweep would flag these as unused;
// removing one fails the build on an incomplete type.

#include "cursiv/lsp/services/SharedLspServices.hpp"  // NOLINT(misc-include-cleaner)

#include <memory>

#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/lsp/completion/DefaultFuzzyMatcher.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/content/TextDocumentContentProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/runtime/DefaultLanguageServer.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/services/ExecuteCommandHandler.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/symbols/DefaultNodeKindProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/symbols/DefaultWorkspaceSymbolProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/workspace/DefaultConfigurationProvider.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/workspace/FileOperationHandler.hpp"  // NOLINT(misc-include-cleaner)
#include "cursiv/lsp/workspace/LspDocumentBuilder.hpp"
#include "cursiv/mcp/McpService.hpp"  // NOLINT(misc-include-cleaner)

namespace cursiv {

SharedLspServices::SharedLspServices() noexcept = default;
SharedLspServices::~SharedLspServices() noexcept {
  // The server unsubscribes while its builder and configuration still live.
  languageServer.reset();
}

void SharedLspServices::visitServices(
    cursiv::function_ref<void(const ServiceSlot&)> yield) const {
  SharedCoreServices::visitServices(yield);
  yield({"workspaceSymbolProvider", true, workspaceSymbolProvider != nullptr});
  yield({"nodeKindProvider", true, nodeKindProvider != nullptr});
  yield({"fuzzyMatcher", true, fuzzyMatcher != nullptr});
  // A language installs one; the server answers "no such command" without.
  yield({"executeCommandHandler", false, executeCommandHandler != nullptr});
  // A host installs one; without it the six file-operation routes are neither
  // announced nor registered.
  yield({"fileOperationHandler", false, fileOperationHandler != nullptr});
  yield({"mcpService", false, mcpService != nullptr});
  yield({"languageServer", true, languageServer != nullptr});
  // Owed only inside a session, so never a hole a completeness check reports.
  yield({"languageClient", false, languageClient != nullptr});
  yield({"textDocumentContentProvider", false,
         textDocumentContentProvider != nullptr});
  yield({"configurationProvider", true, configurationProvider != nullptr});
}

void install_default_shared_lsp_services(SharedLspServices& shared) {
  if (shared.documentBuilder == nullptr)
    shared.documentBuilder = std::make_unique<LspDocumentBuilder>(shared);
  install_default_shared_core_services(shared);
  if (shared.workspaceSymbolProvider == nullptr)
    shared.workspaceSymbolProvider =
        std::make_unique<DefaultWorkspaceSymbolProvider>(shared);
  if (shared.nodeKindProvider == nullptr)
    shared.nodeKindProvider = std::make_unique<DefaultNodeKindProvider>();
  if (shared.fuzzyMatcher == nullptr)
    shared.fuzzyMatcher = std::make_unique<DefaultFuzzyMatcher>();
  if (shared.languageServer == nullptr)
    shared.languageServer = std::make_unique<DefaultLanguageServer>(shared);
  if (shared.configurationProvider == nullptr)
    shared.configurationProvider =
        std::make_unique<DefaultConfigurationProvider>(shared);
}

}  // namespace cursiv
