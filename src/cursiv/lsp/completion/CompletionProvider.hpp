#pragma once

#include <lsp/types.h>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/completion/CompletionProviderOptions.hpp"

// Content assist: a position in a document, and everything the language
// allows to be written there, computed entirely inside that document -- even
// names offered from other files reach it through the scope provider.

namespace cursiv {

class CompletionProvider {
 public:
  virtual ~CompletionProvider() noexcept = default;

  // What makes the client ask without a keystroke, announced once per
  // session. Empty by default: which bytes those are is a fact about one
  // language.
  [[nodiscard]] virtual CompletionProviderOptions completionOptions() const {
    return {};
  }

  /**
   * Precondition: called under the workspace read lock. An empty list is an
   * answer, not an error; `isIncomplete` is always true, since the next
   * keystroke may shift which grammar position the caret is in.
   */
  [[nodiscard]] virtual lsp::CompletionList getCompletion(
      const Document& document, lsp::Position position,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
