// Validates a replacement by reparsing every affected document, with all its
// substitutions made, before producing the edits. Language-specific providers
// can refine the edits for qualified names.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <span>
#include <string_view>

#include "cursiv/navigation/RenameProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DefaultRenameProvider : public RenameProvider,
                              protected DefaultCoreService {
 public:
  explicit DefaultRenameProvider(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}
  ~DefaultRenameProvider() noexcept override = default;

  [[nodiscard]] std::optional<lsp::Range> prepareRename(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;

  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const Document& document, lsp::Position position,
      std::string_view newName,
      const CancellationToken& token = {}) const override;

 protected:

 private:
  // Substitutes `newName` at the spans of `declaringDocument` and reads the
  // document back: the parse verdict holds, and each replacement is an
  // unrepaired record of its own writing the same member of a node of the same
  // class.
  [[nodiscard]] bool isNameOfLanguage(const Document& declaringDocument,
                                      std::span<const TextRange> spans,
                                      std::string_view newName,
                                      const CancellationToken& token) const;
};

}  // namespace cursiv
