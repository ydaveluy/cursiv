#include "cursiv/lsp/ranges/DefaultFoldingRangeProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/ranges/FoldingRangeProvider.hpp"

namespace cursiv {

std::vector<lsp::FoldingRange> DefaultFoldingRangeProvider::getFoldingRanges(
    const Document& document, const CancellationToken& token) const {
  throw_if_cancelled(token);
  std::vector<lsp::FoldingRange> foldings;
  collectFolding(document, [&](lsp::FoldingRange range) {
    throw_if_cancelled(token);
    foldings.push_back(std::move(range));
  });
  return foldings;
}

void DefaultFoldingRangeProvider::collectFolding(
    const Document& document, FoldingRangeAcceptor acceptor) const {
  if (!document.hasAst()) return;
  const AstNode& root = *document.parseResult.value;
  const AstReflection& reflection = *services.astReflection;
  if (shouldProcessContent(root)) {
    // Preorder over what the root contains, siblings in document order.
    std::vector<const AstNode*> pending;
    const auto enter = [&](const AstNode& node) {
      const std::size_t first = pending.size();
      reflection.visitMembers(
          node, [&](std::string_view, std::uint32_t, AstNode& child) {
            pending.push_back(&child);
          });
      std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(first),
                   pending.end());
    };
    enter(root);
    while (!pending.empty()) {
      const AstNode& node = *pending.back();
      pending.pop_back();
      if (shouldProcess(node)) collectObjectFolding(document, node, acceptor);
      if (shouldProcessContent(node)) enter(node);
    }
  }
  collectCommentFolding(document, root, acceptor);
}

bool DefaultFoldingRangeProvider::shouldProcess(const AstNode&) const {
  return true;
}

bool DefaultFoldingRangeProvider::shouldProcessContent(const AstNode&) const {
  return true;
}

void DefaultFoldingRangeProvider::collectObjectFolding(
    const Document& document, const AstNode& node,
    FoldingRangeAcceptor acceptor) const {
  if (node.cstNodeId == kNoNode) return;
  const CstNodeView record{&document.parseResult.cst(), node.cstNodeId};
  if (std::optional<lsp::FoldingRange> range = toFoldingRange(document, record))
    acceptor(std::move(*range));
}

void DefaultFoldingRangeProvider::collectCommentFolding(
    const Document& document, const AstNode& node,
    FoldingRangeAcceptor acceptor) const {
  const Cst& cst = document.parseResult.cst();
  // Trivia outside the entry are roots of the forest, so the document's root
  // answers for the whole tape.
  NodeId first = 0;
  NodeId past = cst.size();
  if (&node != document.parseResult.value) {
    if (node.cstNodeId == kNoNode) return;
    first = cst.subtree_begin(node.cstNodeId);
    past = node.cstNodeId + 1;
  }
  const auto kComment =
      lsp::FoldingRangeKindEnum(lsp::FoldingRangeKind::Comment);
  for (NodeId id = first; id < past; ++id) {
    if (cst[id].hidden == 0) continue;
    if (std::optional<lsp::FoldingRange> range =
            toFoldingRange(document, CstNodeView{&cst, id}, kComment))
      acceptor(std::move(*range));
  }
}

std::optional<lsp::FoldingRange> DefaultFoldingRangeProvider::toFoldingRange(
    const Document& document, const CstNodeView& node,
    std::optional<lsp::FoldingRangeKindEnum> kind) const {
  const TextDocument& text = document.textDocument();
  const Position start = text.positionAt(node.begin());
  Position end = text.positionAt(node.end());
  if (end.line < start.line + 2) return std::nullopt;
  // The closer stays visible: the fold stops at the end of the line before.
  if (!includeLastFoldingLine(node, kind))
    end = text.positionAt(text.offsetAt(Position{end.line, 0}) - 1);
  lsp::FoldingRange range;
  range.startLine = start.line;
  range.endLine = end.line;
  range.startCharacter = start.character;
  range.endCharacter = end.character;
  range.kind = std::move(kind);
  return range;
}

bool DefaultFoldingRangeProvider::includeLastFoldingLine(
    const CstNodeView& node,
    std::optional<lsp::FoldingRangeKindEnum> kind) const {
  if (kind.has_value() && *kind == lsp::FoldingRangeKind::Comment) return false;
  const std::string_view text = node.text();
  if (text.empty()) return true;
  const char last = text.back();
  return last != '}' && last != ')' && last != ']';
}

}  // namespace cursiv
