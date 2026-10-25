#pragma once

#include <vector>

#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

// Not templated by the grammar: what a reference is and what it resolved to
// are erased already, so the one typed question left -- which member is a
// name -- goes through a name provider, routed by the target's own document
// and not the asker's.

namespace cursiv {

// Not `final`: a language derives it; the protected doors are below.
class DefaultReferences : public References, protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] Declaration findDeclaration(
      const Document& document, TextOffset offset,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] std::vector<ReferenceDescription> findReferences(
      const AstNode& target, const FindReferencesOptions& options = {},
      const CancellationToken& cancelToken = kNoCancel) const override;

  ~DefaultReferences() noexcept override = default;

 protected:
  // The logical siblings of a declaration: when `node` is a target of a multi
  // reference, that reference names its whole overload set at once, so a
  // question asked on any member is about the group. `{node}` when it belongs
  // to none -- or when no member of the grammar could hold one.
  [[nodiscard]] virtual std::vector<const AstNode*> getSelfNodes(
      const AstNode& node, const CancellationToken& cancelToken) const;

  // The declaration rows of `node`'s whole group, one per member: the
  // declaration is nobody's reference, so the inverse table cannot hold it and
  // these are the rows the service computes rather than reads. Described as
  // local references to themselves, so consumers see one row type and not two.
  [[nodiscard]] virtual std::vector<ReferenceDescription> getSelfReferences(
      const AstNode& node, const CancellationToken& cancelToken) const;

 private:
  // Both arms of `findDeclaration` at one offset: the retry a byte back applies
  // to the pair, not inside either, so a name cannot lose to the neighbour it
  // happens to touch.
  [[nodiscard]] Declaration at(const Document& document, TextOffset offset,
                               const CancellationToken& cancelToken) const;
};

}  // namespace cursiv
