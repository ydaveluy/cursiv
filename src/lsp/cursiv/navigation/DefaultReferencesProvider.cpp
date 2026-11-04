#include "cursiv/navigation/DefaultReferencesProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <utility>
#include <vector>

#include "cursiv/references/References.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/navigation/NavigationProviderSupport.hpp"
#include "cursiv/navigation/ReferenceLocation.hpp"
#include "cursiv/text/LspPosition.hpp"

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
