#include "cursiv/ExpectedGoToDefinition.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
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

Disposable expectGoToDefinition(
    const LspServices& services,
    const ExpectedGoToDefinition& expectedGoToDefinition) {
  const ReplacedIndices marked =
      replaceIndices(detail::baseOf(expectedGoToDefinition));
  const std::shared_ptr<Document> document = parseDocument(
      services, marked.output, expectedGoToDefinition.parseOptions);

  std::vector<lsp::LocationLink> links;
  const auto* const caret =
      detail::markerAt(marked.indices, expectedGoToDefinition.index);
  if (caret != nullptr && services.definitionProvider != nullptr)
    if (std::optional<std::vector<lsp::LocationLink>> found =
            services.definitionProvider->getDefinition(
                *document, to_lsp(document->textDocument().positionAt(*caret)),
                kNoCancel))
      links = *std::move(found);

  if (const auto* const many = std::get_if<std::vector<std::size_t>>(
          &expectedGoToDefinition.rangeIndex)) {
    expectedFunction(std::to_string(links.size()), std::to_string(many->size()),
                     "expected " + std::to_string(many->size()) +
                         " definitions but received " +
                         std::to_string(links.size()));
    for (std::size_t at = 0; at < many->size() && at < links.size(); ++at)
      if (const auto* const range =
              detail::markerAt(marked.ranges, (*many)[at]))
        detail::expectSameRange(links[at].targetSelectionRange,
                                detail::markedRange(*document, *range));
  } else {
    const std::size_t index =
        std::get<std::size_t>(expectedGoToDefinition.rangeIndex);
    expectedFunction(std::to_string(links.size()), "1",
                     "expected a single definition but received " +
                         std::to_string(links.size()));
    if (!links.empty())
      if (const auto* const range = detail::markerAt(marked.ranges, index))
        detail::expectSameRange(links.front().targetSelectionRange,
                                detail::markedRange(*document, *range));
  }

  Disposable disposable = detail::documentDisposable(services, document);
  if (expectedGoToDefinition.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
