#include "cursiv/test/ExpectedGoToDefinition.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/navigation/DefinitionProvider.hpp"
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

Disposable expectGoToDefinition(
    const LspServices& services,
    const ExpectedGoToDefinition& expectedGoToDefinition) {
  const ReplacedIndices marked = replaceIndices(
      {.text = expectedGoToDefinition.text,
       .indexMarker = expectedGoToDefinition.indexMarker,
       .rangeStartMarker = expectedGoToDefinition.rangeStartMarker,
       .rangeEndMarker = expectedGoToDefinition.rangeEndMarker});
  const std::shared_ptr<Document> document = parseDocument(
      services, marked.output, expectedGoToDefinition.parseOptions);

  std::vector<lsp::LocationLink> links;
  if (services.definitionProvider != nullptr)
    if (std::optional<std::vector<lsp::LocationLink>> found =
            services.definitionProvider->getDefinition(
                *document,
                to_lsp(document->textDocument().positionAt(
                    marked.indices[expectedGoToDefinition.index])),
                kNoCancel))
      links = *std::move(found);

  if (const auto* const many = std::get_if<std::vector<std::size_t>>(
          &expectedGoToDefinition.rangeIndex)) {
    expectedFunction(std::to_string(links.size()), std::to_string(many->size()),
                     "expected " + std::to_string(many->size()) +
                         " definitions but received " +
                         std::to_string(links.size()));
    for (std::size_t at = 0; at < many->size() && at < links.size(); ++at)
      expectSameRange(links[at].targetSelectionRange,
                      markedRange(*document, marked.ranges[(*many)[at]]));
  } else {
    const std::size_t index =
        std::get<std::size_t>(expectedGoToDefinition.rangeIndex);
    expectedFunction(std::to_string(links.size()), "1",
                     "expected a single definition but received " +
                         std::to_string(links.size()));
    if (!links.empty())
      expectSameRange(links.front().targetSelectionRange,
                      markedRange(*document, marked.ranges[index]));
  }

  Disposable disposable = documentDisposable(services, document);
  if (expectedGoToDefinition.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
