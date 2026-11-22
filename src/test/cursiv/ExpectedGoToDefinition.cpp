#include "cursiv/ExpectedGoToDefinition.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

Disposable expectGoToDefinition(
    const LspServices& services,
    const ExpectedGoToDefinition& expectedGoToDefinition) {
  const ReplacedIndices marked =
      replaceIndices(detail::baseOf(expectedGoToDefinition));
  const std::shared_ptr<Document> document = parseDocument(
      services, marked.output, expectedGoToDefinition.parseOptions);
  Disposable disposable = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(disposable);

  std::vector<lsp::Location> targets;
  const auto uri = lsp::Uri::parse(document->uri);
  const auto add = [&](std::size_t index) {
    if (const auto* range = detail::markerAt(marked.ranges, index))
      targets.push_back({uri, detail::markedRange(*document, *range)});
  };
  if (const auto* many = std::get_if<std::vector<std::size_t>>(
          &expectedGoToDefinition.rangeIndex))
    for (auto index : *many) add(index);
  else
    add(std::get<std::size_t>(expectedGoToDefinition.rangeIndex));
  if (const auto* caret =
          detail::markerAt(marked.indices, expectedGoToDefinition.index))
    detail::check_navigation(services, *document, *caret,
                             detail::NavigationQuery::Definition,
                             targets, kNoCancel, true);

  if (expectedGoToDefinition.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
