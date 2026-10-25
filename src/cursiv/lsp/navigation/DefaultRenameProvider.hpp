// Validates a replacement by reparsing one affected document per language
// before producing the edits. Language-specific providers can refine the
// edits for qualified names.
#pragma once

#include <lsp/types.h>

#include <optional>
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
   * Checks the substitution against the existing parse verdict and requires
   * a CST record spanning the replacement. This checks a representative site,
   * not every lexical context touched by the edits.
   */
  [[nodiscard]] bool isNameOfLanguage(const Document& home, TextRange span,
                                      std::string_view newName,
                                      const CancellationToken& token) const;

  const CoreServices& _core;
};

}  // namespace cursiv
