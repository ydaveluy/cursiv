#include "cursiv/references/DefaultNameProvider.hpp"

#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

std::optional<std::string_view> DefaultNameProvider::getName(
    const AstNode& node) const {
  // Absent is "the class declares no name"; a declared name that matched
  // nothing is the empty name, which no default scope offers.
  if (!is_named(node)) return std::nullopt;
  return node.type->readName(node);
}

// Which record of the node's own subtree wrote its `name`, asked of the
// grammar: a name a converter rewrote still has its source range.
std::optional<CstNodeView> DefaultNameProvider::getNameNode(
    const AstNode& node, const Document* document) const {
  // Ask the class first: a record may create two nodes over one span, so a
  // class that declares no name has no name node, whatever its records wrote.
  if (!is_named(node)) return std::nullopt;
  return find_node_for_feature(node, kNameFeature, 0, document);
}

}  // namespace cursiv
