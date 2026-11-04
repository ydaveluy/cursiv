#include "cursiv/ExpectedFoldings.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "cursiv/text/Position.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/ranges/FoldingRangeProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/ReplacedIndices.hpp"

namespace cursiv::test {

Disposable expectFoldings(const LspServices& services,
                          const ExpectedFoldings& input) {
  const ReplacedIndices marked =
      replaceIndices({.text = input.text,
                      .indexMarker = input.indexMarker,
                      .rangeStartMarker = input.rangeStartMarker,
                      .rangeEndMarker = input.rangeEndMarker});
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, input.parseOptions);
  std::vector<lsp::FoldingRange> foldings;
  if (services.foldingRangeProvider != nullptr)
    foldings =
        services.foldingRangeProvider->getFoldingRanges(*document, kNoCancel);
  std::ranges::sort(foldings, {}, &lsp::FoldingRange::startLine);

  if (input.assertion) {
    input.assertion(foldings, marked.ranges);
  } else {
    expectedFunction(
        std::to_string(foldings.size()), std::to_string(marked.ranges.size()),
        "expected " + std::to_string(marked.ranges.size()) + " but received " +
            std::to_string(foldings.size()) + " foldings");
    for (std::size_t at = 0; at < marked.ranges.size() && at < foldings.size();
         ++at) {
      const Position start =
          document->textDocument().positionAt(marked.ranges[at].first);
      const Position end =
          document->textDocument().positionAt(marked.ranges[at].second);
      expectedFunction(
          std::to_string(foldings[at].startLine), std::to_string(start.line),
          "expected folding start at line " + std::to_string(start.line) +
              " but received folding start at line " +
              std::to_string(foldings[at].startLine) + " instead.");
      expectedFunction(
          std::to_string(foldings[at].endLine), std::to_string(end.line),
          "expected folding end at line " + std::to_string(end.line) +
              " but received folding end at line " +
              std::to_string(foldings[at].endLine) + " instead.");
    }
  }

  Disposable disposable = documentDisposable(services, document);
  if (input.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
