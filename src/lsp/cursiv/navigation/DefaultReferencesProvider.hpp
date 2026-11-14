// Translates a UTF-16 position to an offset, asks the core `References`
// service, and turns the rows into `Location`s.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/navigation/ReferencesProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DefaultReferencesProvider : public ReferencesProvider,
                                  protected DefaultCoreService {
 public:
  explicit DefaultReferencesProvider(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

  [[nodiscard]] std::vector<lsp::Location> findReferences(
      const Document& document, lsp::Position position, bool includeDeclaration,
      const CancellationToken& token = {}) const override;
};

}  // namespace cursiv
