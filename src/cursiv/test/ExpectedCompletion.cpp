#include "cursiv/test/ExpectedCompletion.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "cursiv/test/ReplacedIndices.hpp"

namespace cursiv::test {

namespace {

std::string joined(std::span<const std::string> names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

ExpectedBase baseOf(const ExpectedCompletion& input) {
  return std::visit(
      [](const auto& arm) {
        return ExpectedBase{arm.text,           arm.parseOptions,
                            arm.indexMarker,    arm.rangeStartMarker,
                            arm.rangeEndMarker, arm.disposeAfterCheck};
      },
      input);
}

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
  const ExpectedBase base = baseOf(expectedCompletion);
  const ReplacedIndices marked = replaceIndices(base);
  const std::shared_ptr<Document> document =
      parseDocument(services, marked.output, base.parseOptions);

  lsp::CompletionList completions;
  if (services.completionProvider != nullptr)
    completions = services.completionProvider->getCompletion(
        *document,
        to_lsp(document->textDocument().positionAt(
            marked.indices[indexOf(expectedCompletion)])),
        kNoCancel);

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

    if (actual.size() == expected.expectedItems.size())
      for (std::size_t at = 0; at < actual.size(); ++at)
        expectedFunction(actual[at], expected.expectedItems[at]);
    else
      expectedFunction(joined(actual), joined(expected.expectedItems),
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
