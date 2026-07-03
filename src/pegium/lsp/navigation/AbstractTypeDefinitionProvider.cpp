#include <pegium/lsp/navigation/AbstractTypeDefinitionProvider.hpp>
#include <pegium/lsp/navigation/NavigationProviderSupport.hpp>

namespace pegium {

std::optional<std::vector<::lsp::LocationLink>>
AbstractTypeDefinitionProvider::getTypeDefinition(
    const workspace::Document &document,
    const ::lsp::TypeDefinitionParams &params,
    const utils::CancellationToken &cancelToken) const {
  return navigation_detail::collect_links_per_declaration(
      services, document, params.position,
      [this](const AstNode &declaration,
             const utils::CancellationToken &token) {
        return collectGoToTypeLocationLinks(declaration, token);
      },
      cancelToken);
}

} // namespace pegium
