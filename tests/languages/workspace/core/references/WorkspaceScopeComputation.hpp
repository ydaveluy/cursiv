#pragma once

#include <span>
#include <vector>

#include "cursiv/references/DefaultScopeComputation.hpp"
#include "workspace/core/ast.hpp"

// A declaration inside a package is exported under its qualified name,
// `foo.bar.E2` rather than `E2`, which is how another document reaches it.

namespace ws {

struct WorkspaceCoreServices;
class QualifiedNameProvider;

class WorkspaceScopeComputation final
    : public cursiv::DefaultScopeComputation {
 public:
  explicit WorkspaceScopeComputation(
      const WorkspaceCoreServices& services) noexcept;

  [[nodiscard]] std::vector<cursiv::AstNodeDescription> collectExportedSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& cancelToken =
          cursiv::kNoCancel) const override;

  [[nodiscard]] cursiv::LocalSymbols collectLocalSymbols(
      const cursiv::Document& document,
      const cursiv::CancellationToken& cancelToken =
          cursiv::kNoCancel) const override;

 protected:
  // Qualified by the enclosing package, bare anywhere else.
  void addExportedSymbol(cursiv::AstNode& node,
                         std::vector<cursiv::AstNodeDescription>& exported,
                         const cursiv::Document& document) const override;

 private:
  // Each container's declarations, then qualified again for each enclosing
  // package.
  void processContainer(const cursiv::AstNode& root,
                        std::span<ast::AbstractElement* const> elements,
                        const cursiv::Document& document,
                        cursiv::LocalSymbols& symbols,
                        const cursiv::CancellationToken& cancelToken) const;

  const WorkspaceCoreServices& _dm;
};

}  // namespace ws
