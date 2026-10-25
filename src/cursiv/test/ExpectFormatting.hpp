// `textDocument/formatting`: the edits applied to `before` must give `after`,
// the line terminators of both normalized first.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string>

#include "cursiv/test/Disposable.hpp"
#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct LspServices;
}  // namespace cursiv

namespace cursiv::test {

struct ExpectFormatting {
  std::string before{};
  std::string after{};
  ParseHelperOptions parseOptions{};
  // Present restricts the formatter to that range.
  std::optional<lsp::Range> range{};
  // Absent formats with four spaces per level.
  std::optional<lsp::FormattingOptions> options{};
  bool disposeAfterCheck = false;
};

[[nodiscard]] Disposable expectFormatting(
    const LspServices& services, const ExpectFormatting& expectedFormatting);

}  // namespace cursiv::test
