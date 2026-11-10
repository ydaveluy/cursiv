#pragma once

#include <lsp/messages.h>

#include <optional>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// Optional, with no default: a language with no formatter does not format.
// Edits change only the whitespace between tokens; a shaped document answers
// an empty list.

namespace cursiv {

class Formatter {
 public:
  virtual ~Formatter() noexcept = default;

  // Precondition: called under the workspace read lock. Returns edits for the
  // client; the workspace is not written.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> formatDocument(
      const Document& document, const lsp::DocumentFormattingParams& params,
      const CancellationToken& = {}) const = 0;

  // The same rules for the lines of a range. An edit is kept when its lines lie
  // within the range's lines or cover its last line; columns do not narrow the
  // range, so an edit may reach past it.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> formatDocumentRange(
      const Document& document,
      const lsp::DocumentRangeFormattingParams& params,
      const CancellationToken& = {}) const = 0;

  // Formatting in reaction to a keystroke. The defaults advertise no trigger
  // character, so no client sends the request unless a language opts in.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> formatDocumentOnType(
      const Document& document,
      const lsp::DocumentOnTypeFormattingParams& params,
      const CancellationToken& = {}) const {
    (void)document;
    (void)params;
    return {};
  }

  [[nodiscard]] virtual std::optional<lsp::DocumentOnTypeFormattingOptions>
  formatOnTypeOptions() const noexcept {
    return std::nullopt;
  }
};

}  // namespace cursiv
