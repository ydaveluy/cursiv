#include "cursiv/test/ExpectedSymbols.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/DocumentDisposable.hpp"
#include "cursiv/test/ExpectFunction.hpp"
#include "cursiv/test/ExpectedBase.hpp"
#include "cursiv/test/ParseHelper.hpp"

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

ExpectedBase baseOf(const ExpectedSymbols& input) {
  return std::visit(
      [](const auto& arm) {
        return ExpectedBase{arm.text,           arm.parseOptions,
                            arm.indexMarker,    arm.rangeStartMarker,
                            arm.rangeEndMarker, arm.disposeAfterCheck};
      },
      input);
}

}  // namespace

Disposable expectSymbols(const LspServices& services,
                         const ExpectedSymbols& input) {
  const ExpectedBase base = baseOf(input);
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

    if (actual.size() == expected.expectedSymbols.size())
      for (std::size_t at = 0; at < actual.size(); ++at)
        expectedFunction(actual[at], expected.expectedSymbols[at]);
    else
      expectedFunction(joined(actual), joined(expected.expectedSymbols),
                       "expected " +
                           std::to_string(expected.expectedSymbols.size()) +
                           " but found " + std::to_string(symbols.size()) +
                           " symbols in document");
  }

  Disposable disposable = documentDisposable(services, document);
  if (base.disposeAfterCheck) disposable.dispose();
  return disposable;
}

}  // namespace cursiv::test
