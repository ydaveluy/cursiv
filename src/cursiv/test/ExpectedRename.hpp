// `textDocument/prepareRename` then `textDocument/rename`: the caret at one
// marked index, and every span the workspace edit must rewrite marked as a
// range. Both refusals of the protocol are states of the same check.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedRename {
  std::string text{};
  std::size_t index = 0;
  std::string newName{};
  // Nothing renamable under the caret: no edit box, no edit. The text marks no
  // range then.
  bool nothingUnderCaret = false;
  // The box opens and the edit is refused, the new name not being a name of
  // this language.
  bool nameRefused = false;
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectRename(const LspServices& services,
                                      const ExpectedRename& input);

}  // namespace cursiv::test
