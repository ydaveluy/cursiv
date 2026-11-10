#pragma once

#include <vector>

#include "cursiv/references/ScopeComputation.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The default answer: a document exports its root and the root's direct
// children, and every named node is visible from its own container.

namespace cursiv {

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
  // Describes `parent`, then every node `children` hands to its callback. The
  // default runs it over the root's members; a language may export another
  // part.
  [[nodiscard]] std::vector<AstNodeDescription> collectExportedSymbolsForNode(
      AstNode& parent, const Document& document,
      function_ref<void(function_ref<void(AstNode&)>)> children,
      const CancellationToken& cancelToken) const;

  // Adds one node to the exports when it has a name. A language overrides it to
  // change what it exports.
  virtual void addExportedSymbol(AstNode& node,
                                 std::vector<AstNodeDescription>& exported,
                                 const Document& document) const;

  // Adds one node to its container's symbols when it has a name. A language
  // overrides it to widen visibility.
  virtual void addLocalSymbol(AstNode& node, const Document& document,
                              LocalSymbols& symbols) const;
};

}  // namespace cursiv
