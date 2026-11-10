#pragma once

#include <optional>
#include <string_view>

#include "cursiv/navigation/DefaultRenameProvider.hpp"

// Renaming a package also renames the qualified names under it: `foo.bar.E2`
// written elsewhere becomes `foo.baz.E2`.

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
