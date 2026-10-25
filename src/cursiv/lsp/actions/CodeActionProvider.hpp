#pragma once

#include <lsp/messages.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// What an editor may do about a finding. `textDocument/codeAction` asks over a
// range and a set of diagnostics; each action carries the edit that performs
// it, and the client applies it and sends the `didChange` back like any other
// keystroke.

namespace cursiv {

class CodeActionProvider {
 public:
  virtual ~CodeActionProvider() noexcept = default;
  // Empty is "nothing to offer", which the server publishes as null.
  [[nodiscard]] virtual std::vector<lsp::CodeAction> getCodeActions(
      const Document&, const lsp::CodeActionParams&,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
