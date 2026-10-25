// `textDocument/hover`: the prose answered at one marked index, whole or
// through a predicate. Both absent expects no hover at all.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedHover {
  std::string text{};
  std::size_t index = 0;
  std::optional<std::string> hover{};
  // The predicate arm: what a regular expression would have decided.
  std::function<bool(std::string_view)> hoverMatches{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  // Whether the document is dropped as soon as the check has run.
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectHover(const LspServices& services,
                                     const ExpectedHover& expectedHover);

}  // namespace cursiv::test
