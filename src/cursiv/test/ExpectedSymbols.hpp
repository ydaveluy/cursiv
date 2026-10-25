// `textDocument/documentSymbol`: the outline a document answers, checked
// against a list of names or handed whole to the caller's own assertion.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedSymbolsList {
  std::string text{};
  std::vector<std::string> expectedSymbols{};
  // Empty prints a symbol's `name`.
  std::function<std::string(const lsp::DocumentSymbol&)> symbolToString{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

struct ExpectedSymbolsCallback {
  std::string text{};
  std::function<void(std::span<const lsp::DocumentSymbol>)> assertion{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

using ExpectedSymbols =
    std::variant<ExpectedSymbolsList, ExpectedSymbolsCallback>;

[[nodiscard]] Disposable expectSymbols(const LspServices& services,
                                       const ExpectedSymbols& input);

}  // namespace cursiv::test
