#pragma once

#include <lsp/messages.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/actions/CodeActionProvider.hpp"

// The default quick fixes: the edit rows of a diagnostic's `data` and the
// spelling fixes of a linking error, as client edits. Reads the document's own
// diagnostics, not the request's echo, and drops a row with a stale version.

namespace cursiv {

class DefaultCodeActionProvider : public CodeActionProvider {
 public:
  ~DefaultCodeActionProvider() noexcept override = default;

  [[nodiscard]] std::vector<lsp::CodeAction> getCodeActions(
      const Document&, const lsp::CodeActionParams&,
      const CancellationToken& = {}) const override;

 protected:
  // Called after the runtime's own, whatever kinds the request asks for: a
  // language appends and never rewrites, and decides its own kinds.
  virtual void appendCodeActions(const Document& document,
                                 const lsp::CodeActionParams& params,
                                 std::vector<lsp::CodeAction>& actions,
                                 const CancellationToken& cancelToken) const;
};

}  // namespace cursiv
