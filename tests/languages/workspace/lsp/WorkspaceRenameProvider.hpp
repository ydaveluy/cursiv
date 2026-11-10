#pragma once

#include <optional>
#include <string_view>

#include "cursiv/navigation/DefaultRenameProvider.hpp"

// Renaming a package also rewrites the qualified references to what it holds:
// `foo.bar.E2` becomes `foo.baz.E2`, so the qualified name of every
// declaration under it is computed again.

namespace ws {

struct WorkspaceCoreServices;

class WorkspaceRenameProvider final : public cursiv::DefaultRenameProvider {
 public:
  explicit WorkspaceRenameProvider(
      const WorkspaceCoreServices& services) noexcept;

  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const cursiv::Document& document, lsp::Position position,
      std::string_view newName,
      const cursiv::CancellationToken& token) const override;

 private:
  const WorkspaceCoreServices& _dm;
};

}  // namespace ws
