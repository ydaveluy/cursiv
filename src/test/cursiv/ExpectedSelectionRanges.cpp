#include "cursiv/ExpectedSelectionRanges.hpp"

#include <utility>

#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/RangeUtils.hpp"
#include "cursiv/ReplacedIndices.hpp"
#include "cursiv/ranges/SelectionRangeProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv::test {

Disposable expectSelectionRanges(const LspServices& services,
                                   const ExpectedBase& input,
                                   const CancellationToken& cancel) {
  if (!services.selectionRangeProvider)
    throw ExpectationError("missing selection range provider");
  throwIfCancelled(cancel);
  auto marked = detail::scan_indices(input);
  const auto offset = marked.singleCaret();
  if (marked.ranges.empty())
    throw ExpectationError("expected a marked selection range");
  const auto document =
      parseHelper(services, std::move(marked.output), input.parseOptions);
  auto disposable = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(disposable);
  const auto caret = toLsp(document->textDocument().positionAt(offset));
  const auto selections = services.selectionRangeProvider->getSelectionRanges(
      *document, {&caret, 1}, cancel);
  expectedFunction(std::to_string(selections.size()), "1",
                   "selection chains for one caret");
  if (selections.size() == 1) {
    std::size_t depth = 0;
    for (const auto* step = &selections.front(); step;
         step = step->parent.get(), ++depth)
      if (depth < marked.ranges.size())
        detail::expectSameRange(
            step->range, detail::markedRange(*document, marked.ranges[depth]));
    expectedFunction(std::to_string(depth),
                     std::to_string(marked.ranges.size()),
                     "selection chain length");
  }
  if (input.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
