#include <pegium/lsp/navigation/AbstractGoToImplementationProvider.hpp>
#include <pegium/lsp/navigation/NavigationProviderSupport.hpp>

namespace pegium {

std::optional<std::vector<::lsp::LocationLink>>
AbstractGoToImplementationProvider::getImplementation(
    const workspace::Document &document,
    const ::lsp::ImplementationParams &params,
    const utils::CancellationToken &cancelToken) const {
  return navigation_detail::collect_links_per_declaration(
      services, document, params.position,
      [this](const AstNode &declaration,
             const utils::CancellationToken &token) {
        return collectGoToImplementationLocationLinks(declaration, token);
      },
      cancelToken);
}

} // namespace pegium
