#include "cursiv/navigation/DefaultDocumentHighlightProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv {

std::vector<lsp::DocumentHighlight>
DefaultDocumentHighlightProvider::getDocumentHighlight(
    const Document& document, lsp::Position position,
    const CancellationToken& token) const {
  std::vector<lsp::DocumentHighlight> highlights;
  throwIfCancelled(token);
  const Declaration found = services.references->findDeclarations(
      document, document.textDocument().offsetAt(fromLsp(position)), token);

  // The server routes a request to the container of the asking document, so
  // `services`'s name provider is the target's language exactly when the two
  // documents are the same -- the only case a name is measured here.
  const NameProvider& names = *services.nameProvider;
  // The declarations join when one target is declared here, with a name to
  // paint: a caret on a reference whose declaration lives in another file
  // paints the occurrences here and stops.
  const bool declared =
      std::ranges::any_of(found.targets, [&](const AstNode* target) {
        return target->getDocument() == &document &&
               names.getNameNode(*target, &document);
      });
  for (const ReferenceDescription& reference :
       services.references->findReferences(found.targets,
                                           {declared, document.id}, token))
    highlights.push_back(createDocumentHighlight(document, reference));
  return highlights;
}

lsp::DocumentHighlight
DefaultDocumentHighlightProvider::createDocumentHighlight(
    const Document& document, const ReferenceDescription& reference) const {
  lsp::DocumentHighlight highlight;
  highlight.range = toLsp(document.textDocument().rangeAt(
      reference.sourceBegin, reference.sourceEnd));
  return highlight;
}

}  // namespace cursiv
