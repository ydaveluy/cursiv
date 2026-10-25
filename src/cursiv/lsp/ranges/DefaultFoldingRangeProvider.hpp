// The folds are the AST's: every node under the root at its record's bounds,
// and every comment record. The hooks say which nodes fold, whether their
// content is entered, and whether a fold keeps its last line.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/ranges/FoldingRangeProvider.hpp"
#include "cursiv/lsp/services/LspServices.hpp"

namespace cursiv {

struct AstNode;
struct CstNodeView;

class DefaultFoldingRangeProvider : public FoldingRangeProvider {
 public:
  explicit DefaultFoldingRangeProvider(const LspServices& services) noexcept
      : services(services) {}

  [[nodiscard]] std::vector<lsp::FoldingRange> getFoldingRanges(
      const Document& document,
      const CancellationToken& token = {}) const override;

 protected:
  virtual void collectFolding(const Document& document,
                              FoldingRangeAcceptor acceptor) const;

  // Whether the node itself folds; its content is `shouldProcessContent`'s.
  [[nodiscard]] virtual bool shouldProcess(const AstNode& node) const;
  // Whether the node's content is entered at all; false skips it whole.
  [[nodiscard]] virtual bool shouldProcessContent(const AstNode& node) const;

  virtual void collectObjectFolding(const Document& document,
                                    const AstNode& node,
                                    FoldingRangeAcceptor acceptor) const;
  virtual void collectCommentFolding(const Document& document,
                                     const AstNode& node,
                                     FoldingRangeAcceptor acceptor) const;

  [[nodiscard]] virtual std::optional<lsp::FoldingRange> toFoldingRange(
      const Document& document, const CstNodeView& node,
      std::optional<lsp::FoldingRangeKindEnum> kind = std::nullopt) const;

  // False hides the last line: every comment, and a record ending in `}`,
  // `)` or `]`.
  [[nodiscard]] virtual bool includeLastFoldingLine(
      const CstNodeView& node,
      std::optional<lsp::FoldingRangeKindEnum> kind) const;

  const LspServices& services;
};

}  // namespace cursiv
