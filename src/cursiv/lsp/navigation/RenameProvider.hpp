// Rename, and the request that asks permission first. The split is the
// protocol's: `prepareRename` decides whether an edit box opens at all,
// `rename` returns every edit the workspace owes.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string_view>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class RenameProvider {
 public:
  virtual ~RenameProvider() noexcept = default;

  /**
   * Called under the workspace read lock. The span the client puts its
   * edit box on: the name as written under the caret, whether a
   * reference or the declaration itself. `nullopt` when the caret is on
   * neither.
   */
  [[nodiscard]] virtual std::optional<lsp::Range> prepareRename(
      const Document& document, lsp::Position position,
      const CancellationToken& = {}) const = 0;

  /**
   * Called under the workspace read lock. Returns a `WorkspaceEdit`
   * grouped by URI: the declaration and every reference, rewritten to
   * `newName`. `nullopt` when there is nothing to rename or `newName` is
   * not a name of this language.
   */
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> rename(
      const Document& document, lsp::Position position,
      std::string_view newName, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
