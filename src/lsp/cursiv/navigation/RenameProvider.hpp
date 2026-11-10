// Rename, and the request that asks permission first: `prepareRename` decides
// whether an edit box opens, `rename` returns every edit to make.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string_view>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class RenameProvider {
 public:
  virtual ~RenameProvider() noexcept = default;

  // Called under the workspace read lock. The span of the name under the caret,
  // a reference or the declaration; `nullopt` on neither.
  [[nodiscard]] virtual std::optional<lsp::Range> prepareRename(
      const Document& document, lsp::Position position,
      const CancellationToken& = {}) const = 0;

  // Called under the workspace read lock. The declaration and every reference
  // rewritten to `newName`, by URI; `nullopt` when nothing renames or `newName`
  // is not a name of this language.
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> rename(
      const Document& document, lsp::Position position,
      std::string_view newName, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
