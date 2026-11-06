#include "cursiv/navigation/ReferenceLocation.hpp"

#include <lsp/types.h>

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv::detail {

std::vector<lsp::Location> reference_locations(
    const Documents& store, std::span<const ReferenceDescription> references) {
  std::vector<lsp::Location> locations;
  locations.reserve(references.size());
  DocumentId current = kNoDocument;
  std::shared_ptr<Document> source;
  lsp::Uri uri;
  for (const ReferenceDescription& reference : references) {
    if (reference.sourceDocumentId != current) {
      current = reference.sourceDocumentId;
      // The URI survives its document, which is what `getDocumentUri` is
      // for: an index row may name a file that has since been closed.
      const std::string name = store.getDocumentUri(current);
      source = name.empty() ? nullptr : store.getDocument(current);
      if (source != nullptr) uri = lsp::Uri::parse(name);
    }
    if (source == nullptr) continue;
    locations.push_back(
        lsp::Location{.uri = uri,
                      .range = to_lsp(source->textDocument().rangeAt(
                          reference.sourceBegin, reference.sourceEnd))});
  }
  return locations;
}

}  // namespace cursiv::detail
