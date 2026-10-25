#include "cursiv/lsp/navigation/DefaultDocumentHighlightProvider.hpp"

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/lsp/navigation/NavigationProviderSupport.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::vector<lsp::DocumentHighlight>
DefaultDocumentHighlightProvider::getDocumentHighlight(
    const Document& document, lsp::Position position,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentHighlight> highlights;
  throw_if_cancelled(token);
  const Declaration found = _core.references->findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)), token);

  // The server routes a request to the container of the asking document, so
  // `_core`'s name provider is the target's language exactly when the two
  // documents are the same -- the only case a name is measured here.
  const NameProvider& names = *_core.nameProvider;
  const auto options = [&](const AstNode& target) {
    // The declaration joins the answer only if it is here to be drawn: a caret
    // on a reference whose declaration lives in another file paints the
    // occurrences here and stops.
    const bool declared = target.getDocument() == &document &&
                          names.getNameNode(target, &document).has_value();
    return FindReferencesOptions{declared, document.id};
  };
  for (const ReferenceDescription& reference :
       collect_references_per_declaration(*_core.references, found.targets,
                                          options, token))
    highlights.push_back(createDocumentHighlight(document, reference));
  return highlights;
}

lsp::DocumentHighlight
DefaultDocumentHighlightProvider::createDocumentHighlight(
    const Document& document, const ReferenceDescription& reference) const {
  lsp::DocumentHighlight highlight;
  highlight.range = to_lsp(document.textDocument().rangeAt(
      reference.sourceBegin, reference.sourceEnd));
  return highlight;
}

}  // namespace cursiv
