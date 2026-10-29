#include "cursiv/core/documentation/DefaultCommentProvider.hpp"

#include <algorithm>
#include <numeric>
#include <optional>
#include <string>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

std::optional<std::string> DefaultCommentProvider::getComment(
    const AstNode& node, const Document* document) const {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return std::nullopt;
  const Cst& cst = document->parseResult.cst();
  if (node.cstNodeId == kNoNode || node.cstNodeId >= cst.size() ||
      cst.grammar() == nullptr)
    return std::nullopt;

  const Grammar& grammar = *cst.grammar();
  const NodeId nodeId = node.cstNodeId;
  const TextOffset nodeStart = cst[nodeId].begin;
  // The root's neighbourhood is the whole leading trivia; any other node owns
  // exactly one step past its subtree.
  const bool root = &node == document->parseResult.value;
  // A root with no visible content owns nothing: there is no first token for
  // the comment to precede.
  if (root && nodeStart == cst[nodeId].end) return std::nullopt;

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
    if (record.hidden == 0) return std::nullopt;
    if (std::ranges::find(_mlCommentRuleNames, grammar.recordRule(record)) !=
        _mlCommentRuleNames.end())
      return std::string(cst.text(id));
    if (!root) return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace cursiv
