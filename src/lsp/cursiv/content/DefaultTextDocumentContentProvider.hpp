#pragma once

#include <lsp/messages.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/content/TextDocumentContentProvider.hpp"

// Serves a virtual document the store holds, by URI and for the declared
// schemes; what the store does not hold does not exist.

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
