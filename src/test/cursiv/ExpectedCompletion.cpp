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

Disposable expectCompletion(const LspServices& services,
                            const ExpectedCompletion& expectedCompletion) {
  const ExpectedBase base = detail::baseOf(expectedCompletion);
  const ReplacedIndices marked = replaceIndices(base);
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, base.parseOptions);
  Disposable disposable = detail::documentDisposable(services, document);
  const detail::DisposeOnThrow cleanup(disposable);

  const auto index =
      std::visit([](const auto& arm) { return arm.index; }, expectedCompletion);
  lsp::CompletionList completions;
  if (const auto* const caret = detail::markerAt(marked.indices, index);
      caret != nullptr && services.completionProvider != nullptr) {
    lsp::CompletionParams params;
    params.textDocument.uri = lsp::Uri::parse(document->uri);
    params.position = toLsp(document->textDocument().positionAt(*caret));
    completions = services.completionProvider->getCompletion(*document, params,
                                                             kNoCancel);
  }

  if (const auto* const callback =
          std::get_if<ExpectedCompletionCallback>(&expectedCompletion)) {
    if (callback->assertion) callback->assertion(completions);
  } else {
    const auto& expected =
        std::get<ExpectedCompletionItems>(expectedCompletion);
    std::ranges::stable_sort(
        completions.items, {},
        [](const lsp::CompletionItem& item) -> const std::string& {
          return item.sortText ? *item.sortText : item.label;
        });

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

  if (base.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
