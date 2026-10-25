#include "cursiv/test/ExpectedFindReferences.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/navigation/ReferencesProvider.hpp"
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

Disposable expectFindReferences(
    const LspServices& services,
    const ExpectedFindReferences& expectedFindReferences) {
  const ReplacedIndices marked = replaceIndices(
      {.text = expectedFindReferences.text,
       .indexMarker = expectedFindReferences.indexMarker,
       .rangeStartMarker = expectedFindReferences.rangeStartMarker,
       .rangeEndMarker = expectedFindReferences.rangeEndMarker});
  const std::shared_ptr<Document> document = parseDocument(
      services, marked.output, expectedFindReferences.parseOptions);

  std::vector<lsp::Range> expectedRanges;
  expectedRanges.reserve(marked.ranges.size());
  for (const auto& span : marked.ranges)
    expectedRanges.push_back(markedRange(*document, span));

  for (const TextOffset index : marked.indices) {
    std::vector<lsp::Location> references;
    if (services.referencesProvider != nullptr)
      references = services.referencesProvider->findReferences(
          *document, to_lsp(document->textDocument().positionAt(index)),
          expectedFindReferences.includeDeclaration, kNoCancel);

    expectedFunction(std::to_string(references.size()),
                     std::to_string(expectedRanges.size()),
                     "found references do not match amount of expected "
                     "references");

    // Both sides in reading order, then one to one: a membership test lets an
    // answer that repeats one marked site stand for all of them.
    std::vector<lsp::Location> found = references;
    std::ranges::sort(found, {}, [](const lsp::Location& location) {
      return std::pair{location.range.start.line,
                       location.range.start.character};
    });
    std::vector<lsp::Range> wanted = expectedRanges;
    std::ranges::sort(wanted, {}, [](const lsp::Range& range) {
      return std::pair{range.start.line, range.start.character};
    });
    for (std::size_t at = 0; at < wanted.size() && at < found.size(); ++at) {
      expectSameRange(found[at].range, wanted[at]);
      expectedFunction(found[at].uri.toString(), document->uri,
                       "reference at range " + rangeToString(found[at].range) +
                           " is in another document");
    }
  }

  Disposable disposable = documentDisposable(services, document);
  if (expectedFindReferences.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
