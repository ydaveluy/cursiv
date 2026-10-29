// Validates a replacement by reparsing every affected document, with all its
// substitutions made, before producing the edits. Language-specific providers
// can refine the edits for qualified names.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <span>
#include <string_view>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/navigation/RenameProvider.hpp"

namespace cursiv {

/**
 * Not `final`: a reference site may be written under a qualifier, and
 * rewriting one is not generic -- renaming a package changes the
 * qualified name of everything it contains, which only that language's
 * naming rule can compute. `DomainModelRenameProvider` is the example.
 */
class DefaultRenameProvider : public RenameProvider {
 public:
  explicit DefaultRenameProvider(const CoreServices& core) noexcept
      : _core(core) {}
  ~DefaultRenameProvider() noexcept override = default;

  [[nodiscard]] std::optional<lsp::Range> prepareRename(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;

  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const Document& document, lsp::Position position,
      std::string_view newName,
      const CancellationToken& token = {}) const override;

 protected:
  [[nodiscard]] const CoreServices& core() const noexcept { return _core; }

 private:
  /**
   * Substitutes `newName` at every span of `home`, given in document order
   * and disjoint, and reads the document back. The parse verdict must hold,
   * and each replacement must be read as the name it replaces: a record of
   * its own, with no repaired part, writing the same member of a node of the
   * same class.
   */
  [[nodiscard]] bool isNameOfLanguage(const Document& home,
                                      std::span<const TextRange> spans,
                                      std::string_view newName,
                                      const CancellationToken& token) const;

  const CoreServices& _core;
};

}  // namespace cursiv
