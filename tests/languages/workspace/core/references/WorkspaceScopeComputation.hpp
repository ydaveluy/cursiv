#pragma once

#include <span>
#include <vector>

#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "workspace/core/ast.hpp"

// A declaration inside a package is known by its qualifier -- `foo.bar.E2`,
// not `E2` -- which is what makes it reachable from another document. What a
// qualifier is belongs to the grammar, so both halves are overridden here.

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
  // Exported under the name the outside world spells: qualified by the
  // enclosing package, bare anywhere else.
  void addExportedSymbol(cursiv::AstNode& node,
                         std::vector<cursiv::AstNodeDescription>& exported,
                         const cursiv::Document& document) const override;

 private:
  // One container's declarations, returned so the enclosing package can
  // requalify them: a name climbs one dot per storey on the way out.
  [[nodiscard]] std::vector<cursiv::AstNodeDescription> processContainer(
      const cursiv::AstNode& container,
      std::span<ast::AbstractElement* const> elements,
      const cursiv::Document& document, cursiv::LocalSymbols& symbols,
      const cursiv::CancellationToken& cancelToken) const;

  const WorkspaceCoreServices& _dm;
};

}  // namespace ws
