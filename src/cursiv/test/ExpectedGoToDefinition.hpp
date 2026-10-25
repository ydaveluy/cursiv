// `textDocument/definition`: the caret at one marked index, and the marked
// range the declaration it links to is selected at.
#pragma once

#include <cstddef>
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

struct ExpectedGoToDefinition {
  std::string text{};
  std::size_t index = 0;
  std::variant<std::size_t, std::vector<std::size_t>> rangeIndex{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectGoToDefinition(
    const LspServices& services,
    const ExpectedGoToDefinition& expectedGoToDefinition);

}  // namespace cursiv::test
