#include "cursiv/lsp/symbols/DefaultWorkspaceSymbolProvider.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/text/lower_case.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/lsp/completion/FuzzyMatcher.hpp"
#include "cursiv/lsp/symbols/NodeKindProvider.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "lsp/types.h"
#include "lsp/uri.h"

namespace cursiv {

std::vector<lsp::WorkspaceSymbol> DefaultWorkspaceSymbolProvider::getSymbols(
    std::string_view query, const CancellationToken& cancelToken) const {
  std::vector<lsp::WorkspaceSymbol> found;
  const std::string normalized = lower_case(query);
  (void)_shared.indexManager->visitElements(
      nullptr, {}, [&](const AstNodeDescription& description) {
        throw_if_cancelled(cancelToken);
        if (!_shared.fuzzyMatcher->match(normalized, description.name))
          return true;
        if (std::optional<lsp::WorkspaceSymbol> symbol =
                getWorkspaceSymbol(description))
          found.push_back(std::move(*symbol));
        return true;
      });
  return found;
}

std::optional<lsp::WorkspaceSymbol>
DefaultWorkspaceSymbolProvider::getWorkspaceSymbol(
    const AstNodeDescription& astDescription) const {
  const std::shared_ptr<Document> document =
      _shared.documents->getDocument(astDescription.documentId);
  if (document == nullptr) return std::nullopt;
  // The name's own record is asked for here, per hit, never per description.
  const CoreServices* const language = document->language;
  const std::optional<CstNodeView> nameNode =
      language == nullptr || astDescription.node == nullptr
          ? std::nullopt
          : language->nameProvider->getNameNode(*astDescription.node,
                                                document.get());
  if (!nameNode.has_value()) return std::nullopt;
  lsp::WorkspaceSymbol symbol;
  symbol.name = std::string(astDescription.name);
  symbol.kind = _shared.nodeKindProvider->getSymbolKind(astDescription);
  lsp::Location location;
  location.uri = lsp::Uri::parse(document->uri);
  location.range = to_lsp(
      document->textDocument().rangeAt(nameNode->begin(), nameNode->end()));
  symbol.location = std::move(location);
  return symbol;
}

}  // namespace cursiv
