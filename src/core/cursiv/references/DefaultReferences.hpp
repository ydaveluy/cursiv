#pragma once

#include <span>
#include <vector>

#include "cursiv/references/References.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

namespace cursiv {

class DefaultReferences : public References, protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] Declaration findDeclarations(
      const Document& document, TextOffset offset,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] std::vector<ReferenceDescription> findReferences(
      std::span<const AstNode* const> targets,
      const FindReferencesOptions& options,
      const CancellationToken& cancelToken = kNoCancel) const override;

  ~DefaultReferences() noexcept override = default;

 protected:
  // The group a declaration stands for, the same for each of its members: the
  // targets of the first multi reference, in (document, offset) order, that
  // names `node`, or `{node}` otherwise.
  [[nodiscard]] virtual std::vector<const AstNode*> getSelfNodes(
      const AstNode& node, const CancellationToken& cancelToken) const;

  // One row per declaration site of `node`'s group, described as a local
  // reference to itself so callers see one row type.
  [[nodiscard]] virtual std::vector<ReferenceDescription> getSelfReferences(
      const AstNode& node, const CancellationToken& cancelToken) const;

 private:
  // Both lookups of `findDeclarations` at one offset. The retry one byte back
  // covers both together, so a name never loses to a neighbour it touches.
  [[nodiscard]] Declaration at(const Document& document, TextOffset offset,
                               const CancellationToken& cancelToken) const;
};

}  // namespace cursiv
