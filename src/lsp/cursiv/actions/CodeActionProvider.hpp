#pragma once

#include <lsp/messages.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// What an editor may do about a finding: each action carries its edit, which
// the client applies and sends back as a `didChange`.

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
