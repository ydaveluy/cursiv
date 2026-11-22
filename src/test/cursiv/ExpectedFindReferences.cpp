#include "cursiv/ExpectedFindReferences.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/navigation/ReferencesProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

Disposable expectFindReferences(
    const LspServices& services,
    const ExpectedFindReferences& expectedFindReferences) {
  const ReplacedIndices marked =
      replaceIndices(detail::baseOf(expectedFindReferences));
  const std::shared_ptr<Document> document = parseDocument(
      services, marked.output, expectedFindReferences.parseOptions);
  Disposable disposable = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(disposable);

  (void)detail::markerAt(marked.indices, 0);
  const auto uri = lsp::Uri::parse(document->uri);
  std::vector<lsp::Location> targets;
  for (const auto& span : marked.ranges)
    targets.push_back({uri, detail::markedRange(*document, span)});
  detail::sort_locations(targets);
  const auto query = expectedFindReferences.includeDeclaration
                         ? detail::NavigationQuery::ReferencesWithDeclaration
                         : detail::NavigationQuery::References;
  for (auto caret : marked.indices)
    detail::check_navigation(services, *document, caret, query, targets);

  if (expectedFindReferences.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
