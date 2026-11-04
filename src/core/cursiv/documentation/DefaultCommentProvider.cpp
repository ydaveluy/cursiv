#include "cursiv/documentation/DefaultCommentProvider.hpp"

#include <algorithm>
#include <numeric>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/documentation/CommentNode.hpp"
#include "cursiv/documentation/DocComment.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

NodeId find_comment_node(const Document& document, const AstNode& node,
                         std::span<const std::string> ruleNames) {
  const Cst& cst = document.parseResult.cst();
  if (node.cstNodeId == kNoNode || node.cstNodeId >= cst.size() ||
      cst.grammar() == nullptr)
    return kNoNode;

  const Grammar& grammar = *cst.grammar();
  const NodeId nodeId = node.cstNodeId;
  const TextOffset nodeStart = cst[nodeId].begin;
  // The root's neighbourhood is the whole leading trivia; any other node owns
  // exactly one step past its subtree.
  const bool root = &node == document.parseResult.value;
  // A root with no visible content owns nothing: there is no first token for
  // the comment to precede.
  if (root && nodeStart == cst[nodeId].end) return kNoNode;

  // Record ends never decrease. Find the last record before the node's text
  // without scanning its entire subtree; leading trivia inside the subtree
  // remains eligible. This needs no auxiliary CST index.
  NodeId lo = 0, hi = nodeId;
  while (lo < hi) {
    const NodeId mid = std::midpoint(lo, hi);
    if (cst[mid].end <= nodeStart)
      lo = mid + 1;
    else
      hi = mid;
  }
  for (NodeId id = lo; id > 0;) {
    --id;
    const CstNode& record = cst[id];
    // A zero-width record separates nothing.
    if (record.begin == record.end) continue;
    if (record.hidden == 0) return kNoNode;
    if (std::ranges::find(ruleNames, grammar.recordRule(record)) !=
        ruleNames.end())
      return id;
    if (!root) return kNoNode;
  }
  return kNoNode;
}

DocComment parse_doc_comment(const Document& document, NodeId comment,
                             const DocCommentParseOptions& options) {
  const Cst& cst = document.parseResult.cst();
  const TextOffset begin = cst[comment].begin;
  // The document's index says the line; its columns count code units, where a
  // comment's count bytes, so the column is read back to the line's start.
  const std::size_t lastBreak = cst.text().substr(0, begin).find_last_of("\r\n");
  const std::size_t lineStart =
      lastBreak == std::string_view::npos ? 0 : lastBreak + 1;
  const DocCommentPosition start{
      document.textDocument().positionAt(begin).line,
      static_cast<std::uint32_t>(begin - lineStart), begin};
  return parse_doc_comment(cst.text(comment), start, options);
}

std::optional<std::string> DefaultCommentProvider::getComment(
    const AstNode& node, const Document* document) const {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return std::nullopt;
  const NodeId comment = find_comment_node(*document, node, _mlCommentRuleNames);
  if (comment == kNoNode) return std::nullopt;
  return std::string(document->parseResult.cst().text(comment));
}

}  // namespace cursiv
