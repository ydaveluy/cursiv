#include "cursiv/ExpectedHighlight.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
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
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

Disposable expectHighlight(const LspServices& services,
                           const ExpectedHighlight& input) {
  const ReplacedIndices marked = replaceIndices(detail::baseOf(input));
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, input.parseOptions);
  Disposable disposable = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(disposable);
  std::vector<lsp::Location> expected;
  const auto uri = lsp::Uri::parse(document->uri);
  const auto add = [&](std::size_t i) {
    if (const auto* range = detail::markerAt(marked.ranges, i))
      expected.push_back({uri, detail::markedRange(*document, *range)});
  };
  if (const auto* many =
          std::get_if<std::vector<std::size_t>>(&input.rangeIndex))
    for (auto i : *many) add(i);
  else if (const auto* one = std::get_if<std::size_t>(&input.rangeIndex))
    add(*one);
  else
    for (const auto& span : marked.ranges)
      expected.push_back({uri, detail::markedRange(*document, span)});
  if (const auto* caret = detail::markerAt(marked.indices, input.index))
    detail::check_navigation(services, *document, *caret,
                             detail::NavigationQuery::Highlights, expected,
                             kNoCancel, true);

  if (input.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
