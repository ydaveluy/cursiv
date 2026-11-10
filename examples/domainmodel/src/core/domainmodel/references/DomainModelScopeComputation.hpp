#pragma once

#include <span>
#include <vector>

#include "cursiv/references/DefaultScopeComputation.hpp"
#include "domainmodel/ast.hpp"

// A declaration inside a package is known by its qualifier -- `foo.bar.E2`,
// not `E2` -- which is what makes it reachable from another document. What a
// qualifier is belongs to the grammar, so both halves are overridden here.

namespace dm {

struct DomainModelCoreServices;
class QualifiedNameProvider;

class DomainModelScopeComputation final
    : public cursiv::DefaultScopeComputation {
 public:
  explicit DomainModelScopeComputation(
      const DomainModelCoreServices& services) noexcept;

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
  // Each container's declarations, then requalified for the enclosing
  // package: a name gains one qualifier per enclosing package.
  void processContainer(const cursiv::AstNode& root,
                        std::span<ast::AbstractElement* const> elements,
                        const cursiv::Document& document,
                        cursiv::LocalSymbols& symbols,
                        const cursiv::CancellationToken& cancelToken) const;

  const DomainModelCoreServices& _dm;
};

}  // namespace dm
