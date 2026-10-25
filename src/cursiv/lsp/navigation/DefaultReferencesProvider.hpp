// A translation and nothing else: converts the UTF-16 position to an
// offset, asks the core `References` service what is under the caret,
// and converts the rows into `Location`s. Three providers share this
// path, so the declaration-versus-reference rule is written once.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/navigation/ReferencesProvider.hpp"

namespace cursiv {

class DefaultReferencesProvider : public ReferencesProvider {
 public:
  explicit DefaultReferencesProvider(const CoreServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] std::vector<lsp::Location> findReferences(
      const Document& document, lsp::Position position, bool includeDeclaration,
      const CancellationToken& token = {}) const override;

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
