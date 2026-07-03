#include <pegium/lsp/symbols/DefaultWorkspaceSymbolProvider.hpp>

#include <memory>
#include <optional>
#include <utility>

#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/ServiceRegistry.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

namespace pegium {

namespace {

std::optional<::lsp::Location>
symbol_location(const pegium::SharedServices &sharedServices,
                const workspace::AstNodeDescription &entry) {
  // An index snapshot can outlive the document in the store: allElements()
  // hands back a value copy, so a reader (especially one calling this read API
  // outside the workspace lock) can hold an entry whose document was deleted
  // since. Skip it rather than dereferencing a null document.
  const auto document =
      sharedServices.workspace.documents->getDocument(entry.documentId);
  if (document == nullptr) {
    return std::nullopt;
  }

  // Range from the declaration site (name node, else the node's own range).
  const auto *node = document->findAstNode(entry.symbolId);
  if (node == nullptr) {
    return std::nullopt;
  }
  const auto *services =
      sharedServices.serviceRegistry->findServices(document->uri);
  if (services == nullptr) {
    return std::nullopt;
  }
  const auto site = references::declaration_site_node(
      *node, *services->references.nameProvider);
  if (!site.has_value()) {
    return std::nullopt;
  }

  ::lsp::Location location{};
  location.uri = ::lsp::Uri::parse(document->uri);
  location.range = provider_detail::offset_to_range(*document, site->getBegin(),
                                                    site->getEnd());
  return location;
}

} // namespace

std::vector<::lsp::WorkspaceSymbol>
DefaultWorkspaceSymbolProvider::getSymbols(
    const ::lsp::WorkspaceSymbolParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto &indexManager = *shared.workspace.indexManager;
  const auto &nodeKindProvider = *shared.lsp.nodeKindProvider;
  const auto &fuzzyMatcher = *shared.lsp.fuzzyMatcher;

  std::vector<::lsp::WorkspaceSymbol> symbols;
  auto allElements = indexManager.allElements();
  for (const auto &entry : allElements) {
    utils::throw_if_cancelled(cancelToken);
    if (!fuzzyMatcher.match(params.query, entry.name)) {
      continue;
    }

    auto location = symbol_location(shared, entry);
    if (!location.has_value()) {
      continue;
    }

    ::lsp::WorkspaceSymbol workspaceSymbol{};
    workspaceSymbol.name = entry.name;
    workspaceSymbol.kind = nodeKindProvider.getSymbolKind(entry);
    workspaceSymbol.location = std::move(*location);
    symbols.push_back(std::move(workspaceSymbol));
  }
  return symbols;
}

} // namespace pegium
