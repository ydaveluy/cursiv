#include "cursiv/lsp/navigation/DefaultReferencesProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <utility>
#include <vector>

#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/lsp/navigation/NavigationProviderSupport.hpp"
#include "cursiv/lsp/navigation/ReferenceLocation.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::vector<lsp::Location> DefaultReferencesProvider::findReferences(
    const Document& document, lsp::Position position, bool includeDeclaration,
    const CancellationToken& token) const {
  std::vector<lsp::Location> locations;
  throw_if_cancelled(token);
  const References& references = *_core.references;
  const Documents& store = *_core.shared.documents;

  const Declaration found = references.findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)), token);
  const FindReferencesOptions options{includeDeclaration, kNoDocument};
  for (const ReferenceDescription& reference :
       collect_references_per_declaration(
           references, found.targets,
           [&options](const AstNode&) { return options; }, token))
    if (std::optional<lsp::Location> location =
            reference_location(store, reference))
      locations.push_back(std::move(*location));
  return locations;
}

}  // namespace cursiv
