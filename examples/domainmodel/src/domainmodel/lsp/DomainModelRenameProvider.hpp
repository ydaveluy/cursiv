#pragma once

#include <optional>
#include <string_view>

#include "cursiv/lsp/navigation/DefaultRenameProvider.hpp"

// Renaming a package renames what it contains, as far as a reference site is
// concerned: `foo.bar.E2` written elsewhere must become `foo.baz.E2`, and
// that is not a substitution the base can make -- it has to recompute the
// qualified name of every declaration under the renamed one.

namespace dm {

struct DomainModelCoreServices;

class DomainModelRenameProvider final : public cursiv::DefaultRenameProvider {
 public:
  explicit DomainModelRenameProvider(
      const DomainModelCoreServices& services) noexcept;

  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const cursiv::Document& document, lsp::Position position,
      std::string_view newName,
      const cursiv::CancellationToken& token) const override;

 private:
  const DomainModelCoreServices& _dm;
};

}  // namespace dm
