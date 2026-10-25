// Find-references restricted to one open document. `includeDeclaration` is
// computed, not asked -- the protocol's request carries no such flag.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/lsp/navigation/DocumentHighlightProvider.hpp"

namespace cursiv {

class DefaultDocumentHighlightProvider : public DocumentHighlightProvider {
 public:
  explicit DefaultDocumentHighlightProvider(const CoreServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] std::vector<lsp::DocumentHighlight> getDocumentHighlight(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;

 protected:
  // Override this method to determine the highlight kind of the given
  // reference; the default stamps none.
  [[nodiscard]] virtual lsp::DocumentHighlight createDocumentHighlight(
      const Document& document, const ReferenceDescription& reference) const;

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
