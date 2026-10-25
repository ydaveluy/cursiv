// The signature under the caret while an argument list is typed. No
// default: which construct takes arguments, and how they are counted, is
// the grammar's to decide.
#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class SignatureHelpProvider {
 public:
  virtual ~SignatureHelpProvider() noexcept = default;

  /**
   * The characters that reopen the popup as the caret moves between
   * arguments. A language that forgets one gets help once and never again.
   */
  [[nodiscard]] virtual lsp::SignatureHelpOptions signatureHelpOptions() const {
    return {};
  }

  /**
   * Computes the signature help at `params.position`; `params.context` tells
   * a retrigger from a first request. Precondition: called under the
   * workspace read lock.
   */
  [[nodiscard]] virtual std::optional<lsp::SignatureHelp> provideSignatureHelp(
      const Document& document, const lsp::SignatureHelpParams& params,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
