#include "cursiv/ExpectedSymbols.hpp"

#include <lsp/types.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectFunction.hpp"
#include "cursiv/ExpectedBase.hpp"
#include "cursiv/ParseHelper.hpp"

namespace cursiv::test {

Disposable expectSymbols(const LspServices& services,
                         const ExpectedSymbols& input) {
  const ExpectedBase base = detail::baseOf(input);
  const std::shared_ptr<Document> document =
      parseDocument(services, base.text, base.parseOptions);
  std::vector<lsp::DocumentSymbol> symbols;
  if (services.documentSymbolProvider != nullptr)
    symbols = services.documentSymbolProvider->getSymbols(*document, kNoCancel);

  if (const auto* const callback =
          std::get_if<ExpectedSymbolsCallback>(&input)) {
    if (callback->assertion) callback->assertion(symbols);
  } else {
    const auto& expected = std::get<ExpectedSymbolsList>(input);
    std::vector<std::string> actual;
    actual.reserve(symbols.size());
    for (const lsp::DocumentSymbol& symbol : symbols)
      actual.push_back(expected.symbolToString ? expected.symbolToString(symbol)
                                               : symbol.name);

    detail::expectedItems(actual, expected.expectedSymbols,
                          "expected " +
                              std::to_string(expected.expectedSymbols.size()) +
                              " but found " + std::to_string(symbols.size()) +
                              " symbols in document");
  }

  Disposable disposable = detail::documentDisposable(services, document);
  if (base.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
