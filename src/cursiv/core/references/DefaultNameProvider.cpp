#include "cursiv/core/references/DefaultNameProvider.hpp"

#include <optional>
#include <string_view>

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

std::optional<std::string_view> DefaultNameProvider::getName(
    const AstNode& node) const {
  // Absent is "the class declares no name"; a declared name that matched
  // nothing is the empty name, and it still denotes -- resolvable, renamable.
  if (!is_named(node)) return std::nullopt;
  return node.type->readName(node);
}

// Which record of this node's own subtree wrote the `name` member. Asked of
// the grammar, so a name a converter rewrote -- unquoted, unescaped, and
// living in the pool rather than in the text -- still has the range a rename
// and a highlight need.
std::optional<CstNodeView> DefaultNameProvider::getNameNode(
    const AstNode& node, const Document* document) const {
  // The class answers first. A record may open two nodes over one span, so
  // the span alone cannot say whose name a member is: a class that declares
  // none has no name node, whatever the record below it wrote.
  if (!is_named(node)) return std::nullopt;
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr || node.cstNodeId == kNoNode) return std::nullopt;
  return find_node_for_feature(
      CstNodeView{&document->parseResult.cst(), node.cstNodeId}, kNameFeature);
}

}  // namespace cursiv
