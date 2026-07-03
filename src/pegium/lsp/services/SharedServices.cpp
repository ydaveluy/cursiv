#include <pegium/lsp/services/SharedServices.hpp>

#include <pegium/lsp/runtime/LanguageClient.hpp>

namespace pegium {

SharedServices::SharedServices() = default;
SharedServices::~SharedServices() noexcept = default;

bool SharedServices::isComplete() const noexcept {
  return SharedCoreServices::isComplete() && lsp.textDocuments &&
         lsp.documentUpdateHandler && lsp.fuzzyMatcher && lsp.languageServer &&
         lsp.nodeKindProvider && lsp.workspaceSymbolProvider;
}

} // namespace pegium
