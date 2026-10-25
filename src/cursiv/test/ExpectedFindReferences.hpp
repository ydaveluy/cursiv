// `textDocument/references`: asked at every marked index in turn, and each
// answer must be exactly the set of marked ranges.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedFindReferences {
  std::string text{};
  bool includeDeclaration = false;
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectFindReferences(
    const LspServices& services,
    const ExpectedFindReferences& expectedFindReferences);

}  // namespace cursiv::test
