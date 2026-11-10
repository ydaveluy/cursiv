#pragma once

#include <lsp/types.h>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/completion/CompletionProviderOptions.hpp"

// Content assist: everything the language allows at a position of a document.
// Names from other files come through the scope provider.

namespace cursiv {

class CompletionProvider {
 public:
  virtual ~CompletionProvider() noexcept = default;

  // The trigger and commit characters, sent once at initialize. Empty by
  // default.
  [[nodiscard]] virtual CompletionProviderOptions completionOptions() const {
    return {};
  }

  // Called under the workspace read lock. An empty list is an answer, and
  // `isIncomplete` is always true: the next keystroke may move the caret.
  [[nodiscard]] virtual lsp::CompletionList getCompletion(
      const Document& document, const lsp::CompletionParams& params,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
