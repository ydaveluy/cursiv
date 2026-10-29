#include "cursiv/lsp/formatting/AbstractFormatter.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/formatting/FormattingEngine.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

std::vector<NodeId> keyword_nodes(const Cst& cst, NodeId root,
                                  std::string_view keyword) {
  std::vector<NodeId> matches;
  for (const CstNodeView& at :
       find_nodes_for_keyword(CstNodeView{&cst, root}, keyword))
    matches.push_back(at.id);
  return matches;
}

std::vector<NodeId> hidden_nodes(const Cst& cst, NodeId root,
                                 std::string_view ruleName) {
  std::vector<NodeId> matches;
  for (const CstNodeView& at :
       find_hidden_nodes(CstNodeView{&cst, root}, ruleName))
    matches.push_back(at.id);
  return matches;
}

std::vector<lsp::TextEdit> AbstractFormatter::formatDocument(
    const Document& document, const lsp::DocumentFormattingParams& params,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  // The tape of a repaired parse describes text the author did not write.
  if (document.parseRecovered()) return {};
  return doDocumentFormat(document, params.options, std::nullopt, token);
}

std::vector<lsp::TextEdit> AbstractFormatter::formatDocumentRange(
    const Document& document, const lsp::DocumentRangeFormattingParams& params,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!isFormatRangeErrorFree(document, params.range)) return {};
  return doDocumentFormat(document, params.options, params.range, token);
}

std::vector<lsp::TextEdit> AbstractFormatter::formatDocumentOnType(
    const Document& document, const lsp::DocumentOnTypeFormattingParams& params,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  // From the start of the caret's line to the caret. Edits are kept by line,
  // as for a selection, so the whole line is formatted, text past the caret
  // included.
  lsp::Range range{};
  range.start.line = params.position.line;
  range.start.character = 0;
  range.end = params.position;
  if (!isFormatRangeErrorFree(document, range)) return {};
  return doDocumentFormat(document, params.options, range, token);
}

bool AbstractFormatter::isFormatRangeErrorFree(const Document& document,
                                               const lsp::Range& range) const {
  std::uint32_t earliest = std::numeric_limits<std::uint32_t>::max();
  for (const cursiv::ParseDiagnostic& d : document.parseResult.diagnostics)
    if (d.isSyntax())
      earliest =
          std::min(earliest, document.textDocument().positionAt(d.begin).line);
  return earliest > range.end.line;
}

void AbstractFormatter::format(FormattingBuilder& builder,
                               const AstNode* node) const {
  if (node == nullptr || node->type == nullptr) return;
  const auto it = _formatters.find(node->type);
  if (it == _formatters.end()) return;
  it->second(*this, builder, node);
}

void AbstractFormatter::formatHidden(HiddenNodeFormatter& hidden) const {
  const auto it = _hiddenFormatters.find(hidden.ruleName());
  if (it == _hiddenFormatters.end()) return;
  it->second(*this, hidden);
}

std::vector<lsp::TextEdit> AbstractFormatter::doDocumentFormat(
    const Document& document, const lsp::FormattingOptions& options,
    const std::optional<lsp::Range>& range,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return {};
  const ParseResult& parsed = document.parseResult;
  const Cst& cst = parsed.cst();
  if (!cst.has_entry()) return {};

  FormattingActions actions(cst.size());
  const AstReflection& reflection = *_core.astReflection;
  if (cst.grammar() == nullptr) return {};
  FormattingBuilder builder(cst, actions);

  // Root first, each node before its descendants: on a boundary two
  // constructions both claim, the inner one's rule is registered last and
  // wins the tie. A subtree whose lines miss the range's lines is not entered.
  std::vector<const AstNode*> pending{parsed.value};
  std::vector<const AstNode*> children;
  while (!pending.empty()) {
    throw_if_cancelled(token);
    const AstNode* const node = pending.back();
    pending.pop_back();
    if (node == nullptr) continue;
    const NodeId id = node->cstNodeId;
    const bool placed = id != kNoNode && id < cst.size();
    if (placed && range.has_value()) {
      const lsp::Range span =
          to_lsp(document.textDocument().rangeAt(cst[id].begin, cst[id].end));
      if (span.start.line > range->end.line ||
          range->start.line > span.end.line)
        continue;
    }
    if (placed) format(builder, node);
    children.clear();
    reflection.visitMembers(*node, [&children](std::string_view, std::uint32_t,
                                               const AstNode& child) {
      children.push_back(&child);
    });
    pending.insert(pending.end(), children.rbegin(), children.rend());
  }

  const auto visitHidden = [&](NodeId id, std::int32_t indentation,
                               std::string_view newlineStyle) {
    HiddenNodeFormatter hidden(cst, id, indentation, options, newlineStyle,
                               actions);
    formatHidden(hidden);
  };
  return run_formatting(document, cst, options, range, actions, visitHidden,
                        token);
}

}  // namespace cursiv
