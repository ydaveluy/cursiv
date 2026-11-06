#pragma once

#include <lsp/messages.h>

#include <optional>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// The formatter slot: optional, with no default, so a language with no
// formatter simply does not format. The contract is edits over the
// whitespace between tokens, never a rewrite -- an already-shaped document
// answers an empty list, not an error.

namespace cursiv {

class Formatter {
 public:
  virtual ~Formatter() noexcept = default;

  // Precondition: called under the workspace read lock. A format computes edits
  // and hands them to the client, which applies them and sends the `didChange`
  // back; the workspace is not written here.
  [[nodiscard]] virtual std::vector<lsp::TextEdit> formatDocument(
      const Document& document, const lsp::DocumentFormattingParams& params,
      const CancellationToken& = {}) const = 0;

  // The same rules restricted to the lines of a range: an edit is kept when
  // its lines lie within the range's lines or cover the range's last line. An
  // edit that starts before the range and ends before its last line is left
  // out. The range's columns do not narrow it, and a kept gap is rewritten
  // whole, so an edit may reach past the range.
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
