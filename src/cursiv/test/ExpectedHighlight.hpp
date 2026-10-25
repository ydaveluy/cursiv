// `textDocument/documentHighlight`: the caret at one marked index, and the
// marked ranges the symbol under it must be painted at.
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

struct ExpectedHighlight {
  std::string text{};
  std::size_t index = 0;
  // Absent checks every marked range in order; one index checks a single
  // highlight, a list checks that many in order.
  std::variant<std::monostate, std::size_t, std::vector<std::size_t>>
      rangeIndex{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectHighlight(const LspServices& services,
                                         const ExpectedHighlight& input);

}  // namespace cursiv::test
