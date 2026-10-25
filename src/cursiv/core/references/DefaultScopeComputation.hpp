#pragma once

#include <vector>

#include "cursiv/core/references/ScopeComputation.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// The default answer: a document exports its root and the root's direct
// children, and every named node is visible from its own container.

namespace cursiv {

// Not `final`: a language whose visibility is not "the root and its children"
// derives this and overrides the half it disagrees with -- a qualified-name
// language, say.
class DefaultScopeComputation : public ScopeComputation,
                                protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  ~DefaultScopeComputation() noexcept override = default;

  [[nodiscard]] std::vector<AstNodeDescription> collectExportedSymbols(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] LocalSymbols collectLocalSymbols(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const override;

 protected:
  // The exporting walk with the child set as a parameter: describes `parent`,
  // then every node `children` hands to its callback. What the public default
  // runs over the root's direct members, reusable by a language that exports
  // another slice of the tree.
  [[nodiscard]] std::vector<AstNodeDescription> collectExportedSymbolsForNode(
      AstNode& parent, const Document& document,
      function_ref<void(function_ref<void(AstNode&)>)> children,
      const CancellationToken& cancelToken) const;

  // One node into the exports, when it has a name. The override point of a
  // language that renames what it publishes.
  virtual void addExportedSymbol(AstNode& node,
                                 std::vector<AstNodeDescription>& exported,
                                 const Document& document) const;

  // One node into its container's symbols, when it has a name. The override
  // point of a language that widens visibility.
  virtual void addLocalSymbol(AstNode& node, const Document& document,
                              LocalSymbols& symbols) const;
};

}  // namespace cursiv
