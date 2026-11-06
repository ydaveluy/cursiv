#include "cursiv/ExpectedHighlight.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/ReplacedIndices.hpp"

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
  const auto* const caret = detail::markerAt(marked.indices, input.index);
  if (caret != nullptr && services.documentHighlightProvider != nullptr)
    highlights = services.documentHighlightProvider->getDocumentHighlight(
        *document, to_lsp(document->textDocument().positionAt(*caret)),
        kNoCancel);

  if (const auto* const many =
          std::get_if<std::vector<std::size_t>>(&input.rangeIndex)) {
    expectedFunction(
        std::to_string(highlights.size()), std::to_string(many->size()),
        "expected " + std::to_string(many->size()) +
            " highlights but received " + std::to_string(highlights.size()));
    for (std::size_t at = 0; at < many->size() && at < highlights.size(); ++at)
      if (const auto* const range =
              detail::markerAt(marked.ranges, (*many)[at]))
        expectSameRange(highlights[at].range, markedRange(*document, *range));
  } else if (const auto* const one =
                 std::get_if<std::size_t>(&input.rangeIndex)) {
    expectedFunction(std::to_string(highlights.size()), "1",
                     "expected a single highlight but received " +
                         std::to_string(highlights.size()));
    if (!highlights.empty())
      if (const auto* const range = detail::markerAt(marked.ranges, *one))
        expectSameRange(highlights.front().range,
                        markedRange(*document, *range));
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
