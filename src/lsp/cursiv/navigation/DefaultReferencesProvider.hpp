// Translates a UTF-16 position to an offset, asks the core `References`
// service, and turns the rows into `Location`s.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/navigation/ReferencesProvider.hpp"

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
