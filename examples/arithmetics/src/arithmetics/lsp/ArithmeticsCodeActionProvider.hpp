#pragma once

#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"

// The quick fix for `checkNormalizable`: replace a foldable expression with the
// constant the check already computed. What to write is read off the
// diagnostic's data, never parsed back out of its message.

namespace arith {

class ArithmeticsCodeActionProvider final
    : public cursiv::DefaultCodeActionProvider {
 protected:
  void appendCodeActions(
      const cursiv::Document& document, const ::lsp::CodeActionParams& params,
      std::vector<::lsp::CodeAction>& actions,
      const cursiv::CancellationToken& cancelToken) const override;
};

}  // namespace arith
