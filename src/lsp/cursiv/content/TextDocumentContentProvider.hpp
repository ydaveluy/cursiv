#pragma once

#include <lsp/messages.h>

#include <optional>
#include <span>
#include <string>

// `workspace/textDocumentContent`: the text behind a URI the client cannot
// read itself. Announced at initialize only when installed.

namespace cursiv {

class TextDocumentContentProvider {
 public:
  virtual ~TextDocumentContentProvider() noexcept = default;

  // The URI schemes this provider answers for; advertised at initialize.
  [[nodiscard]] virtual std::span<const std::string> schemes()
      const noexcept = 0;

  // The text behind the URI, or nothing when the scheme or the document is
  // unknown. Called under the workspace read lock by the server.
  [[nodiscard]] virtual std::optional<lsp::TextDocumentContentResult>
  provideTextDocumentContent(const lsp::TextDocumentContentParams&) const = 0;
};

}  // namespace cursiv
