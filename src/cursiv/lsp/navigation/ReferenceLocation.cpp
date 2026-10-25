#include "cursiv/lsp/navigation/ReferenceLocation.hpp"

#include <lsp/types.h>

#include <memory>
#include <optional>
#include <string>

#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::optional<lsp::Location> reference_location(
    const Documents& store, const ReferenceDescription& reference) {
  // The URI survives its document, which is what `getDocumentUri` is for: an
  // index row may name a file that has since been closed and must still be
  // nameable.
  const std::string uri = store.getDocumentUri(reference.sourceDocumentId);
  if (uri.empty()) return std::nullopt;
  const std::shared_ptr<Document> source =
      store.getDocument(reference.sourceDocumentId);
  if (source == nullptr) return std::nullopt;
  lsp::Location location;
  location.uri = lsp::Uri::parse(uri);
  location.range = to_lsp(source->textDocument().rangeAt(reference.sourceBegin,
                                                         reference.sourceEnd));
  return location;
}

}  // namespace cursiv
