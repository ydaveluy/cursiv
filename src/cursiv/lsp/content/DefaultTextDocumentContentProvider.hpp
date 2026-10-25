#pragma once

#include <lsp/messages.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/lsp/content/TextDocumentContentProvider.hpp"

// The default answers from the documents store: a virtual document someone
// already registered (a builtin library added at workspace setup) is served
// back by URI, gated on the declared schemes. No file system involved -- what
// the store does not hold does not exist.

namespace cursiv {

class DefaultTextDocumentContentProvider : public TextDocumentContentProvider {
 public:
  DefaultTextDocumentContentProvider(const SharedCoreServices& shared,
                                     std::vector<std::string> schemes);

  [[nodiscard]] std::span<const std::string> schemes() const noexcept override;

  [[nodiscard]] std::optional<lsp::TextDocumentContentResult>
  provideTextDocumentContent(
      const lsp::TextDocumentContentParams&) const override;

 private:
  const SharedCoreServices& _shared;
  std::vector<std::string> _schemes;
};

}  // namespace cursiv
