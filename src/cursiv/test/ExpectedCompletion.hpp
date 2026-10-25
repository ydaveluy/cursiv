// `textDocument/completion`: what may be written at one marked index, checked
// against a list of labels or handed whole to the caller's own assertion.
#pragma once

#include <lsp/types.h>

#include <cstddef>
#include <functional>
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

struct ExpectedCompletionItems {
  std::string text{};
  std::size_t index = 0;
  std::vector<std::string> expectedItems{};
  // Empty prints an item's `label`.
  std::function<std::string(const lsp::CompletionItem&)> itemToString{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

struct ExpectedCompletionCallback {
  std::string text{};
  std::size_t index = 0;
  std::function<void(const lsp::CompletionList&)> assertion{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

using ExpectedCompletion =
    std::variant<ExpectedCompletionItems, ExpectedCompletionCallback>;

[[nodiscard]] Disposable expectCompletion(
    const LspServices& services, const ExpectedCompletion& expectedCompletion);

}  // namespace cursiv::test
