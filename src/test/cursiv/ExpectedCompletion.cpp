#include "cursiv/ExpectedCompletion.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/completion/CompletionProvider.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"
#include "cursiv/ReplacedIndices.hpp"

namespace cursiv::test {

namespace {

std::size_t indexOf(const ExpectedCompletion& input) {
  if (const auto* const items = std::get_if<ExpectedCompletionItems>(&input))
    return items->index;
  return std::get<ExpectedCompletionCallback>(input).index;
}

// The order the client would show, and the rank a candidate carries: an item
// with none ranks where `"0"` would.
std::string rankOf(const lsp::CompletionItem& item) {
  return item.sortText.has_value() ? *item.sortText : std::string("0");
}

}  // namespace

Disposable expectCompletion(const LspServices& services,
                            const ExpectedCompletion& expectedCompletion) {
  const ExpectedBase base = detail::baseOf(expectedCompletion);
  const ReplacedIndices marked = replaceIndices(base);
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, base.parseOptions);

  lsp::CompletionList completions;
  const auto* const caret =
      detail::markerAt(marked.indices, indexOf(expectedCompletion));
  if (caret != nullptr && services.completionProvider != nullptr) {
    lsp::CompletionParams params;
    params.textDocument.uri = lsp::Uri::parse(document->uri);
    params.position = to_lsp(document->textDocument().positionAt(*caret));
    completions = services.completionProvider->getCompletion(*document, params,
                                                             kNoCancel);
  }

  if (const auto* const callback =
          std::get_if<ExpectedCompletionCallback>(&expectedCompletion)) {
    if (callback->assertion) callback->assertion(completions);
  } else {
    const auto& expected =
        std::get<ExpectedCompletionItems>(expectedCompletion);
    std::ranges::stable_sort(completions.items, {}, rankOf);

    std::vector<std::string> actual;
    actual.reserve(completions.items.size());
    for (const lsp::CompletionItem& item : completions.items)
      actual.push_back(expected.itemToString ? expected.itemToString(item)
                                             : item.label);

    detail::expectedItems(actual, expected.expectedItems,
                          "expected " +
                              std::to_string(expected.expectedItems.size()) +
                              " but received " + std::to_string(actual.size()) +
                              " completion items");
  }

  Disposable disposable = documentDisposable(services, document);
  if (base.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
