#include "cursiv/test/ExpectedHighlight.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/RangeUtils.hpp"
#include "cursiv/test/ReplacedIndices.hpp"

namespace cursiv::test {

Disposable expectHighlight(const LspServices& services,
                           const ExpectedHighlight& input) {
  const ReplacedIndices marked =
      replaceIndices({.text = input.text,
                      .indexMarker = input.indexMarker,
                      .rangeStartMarker = input.rangeStartMarker,
                      .rangeEndMarker = input.rangeEndMarker});
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, input.parseOptions);
  std::vector<lsp::DocumentHighlight> highlights;
  if (services.documentHighlightProvider != nullptr)
    highlights = services.documentHighlightProvider->getDocumentHighlight(
        *document,
        to_lsp(
            document->textDocument().positionAt(marked.indices[input.index])),
        kNoCancel);

  if (const auto* const many =
          std::get_if<std::vector<std::size_t>>(&input.rangeIndex)) {
    expectedFunction(
        std::to_string(highlights.size()), std::to_string(many->size()),
        "expected " + std::to_string(many->size()) +
            " highlights but received " + std::to_string(highlights.size()));
    for (std::size_t at = 0; at < many->size() && at < highlights.size(); ++at)
      expectSameRange(highlights[at].range,
                      markedRange(*document, marked.ranges[(*many)[at]]));
  } else if (const auto* const one =
                 std::get_if<std::size_t>(&input.rangeIndex)) {
    expectedFunction(std::to_string(highlights.size()), "1",
                     "expected a single highlight but received " +
                         std::to_string(highlights.size()));
    if (!highlights.empty())
      expectSameRange(highlights.front().range,
                      markedRange(*document, marked.ranges[*one]));
  } else {
    expectedFunction(
        std::to_string(highlights.size()), std::to_string(marked.ranges.size()),
        "expected " + std::to_string(marked.ranges.size()) +
            " highlights but received " + std::to_string(highlights.size()));
    for (std::size_t at = 0;
         at < marked.ranges.size() && at < highlights.size(); ++at)
      expectSameRange(highlights[at].range,
                      markedRange(*document, marked.ranges[at]));
  }

  Disposable disposable = documentDisposable(services, document);
  if (input.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
