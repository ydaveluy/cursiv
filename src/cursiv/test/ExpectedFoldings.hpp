// `textDocument/foldingRange`: every marked range must be a fold, compared on
// the lines it opens and closes -- the unit the protocol answers in.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectedFoldings {
  std::string text{};
  // Empty compares the folds against the marked ranges, in order.
  std::function<void(std::span<const lsp::FoldingRange>,
                     std::span<const std::pair<TextOffset, TextOffset>>)>
      assertion{};
  ParseHelperOptions parseOptions{};
  std::string_view indexMarker = "<|>";
  std::string_view rangeStartMarker = "<|";
  std::string_view rangeEndMarker = "|>";
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectFoldings(const LspServices& services,
                                        const ExpectedFoldings& input);

}  // namespace cursiv::test
