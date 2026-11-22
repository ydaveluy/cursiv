// Document, range or on-type formatting edits must turn `before` into `after`.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string>

#include "cursiv/Disposable.hpp"
#include "cursiv/ParseHelperOptions.hpp"

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
  // The character just typed, with <|> marking the resulting caret in before.
  std::optional<std::string> onType{};
};

[[nodiscard]] Disposable expectFormatting(
    const LspServices& services, const ExpectFormatting& expectedFormatting);

}  // namespace cursiv::test
