#pragma once

#include <lsp/messages.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/actions/CodeActionProvider.hpp"

// The default quick fixes: the edit rows the document validator wrote into a
// diagnostic's `data` (`core/validation/DefaultCodeActionProtocol.hpp`) and
// those a linking error is owed on demand (`spelling_actions`), turned into
// client edits. Reads the document's own published block, not the request's
// echo, and drops any row whose stamped version is stale.

namespace cursiv {

// Not `final`: a language with fixes of its own derives this and overrides
// the hook below, so the runtime's repairs and the language's are offered
// together.
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
