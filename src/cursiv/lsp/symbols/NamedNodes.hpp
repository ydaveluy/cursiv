// The nodes of a document that own a name node, and who owns whom: the
// nearest named ancestor is the one nesting rule the outline and the
// semantic colours must agree on, so it is computed here and read by both.
#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/utils/Cancellation.hpp"

namespace cursiv {

class NameProvider;
struct Document;

struct NamedNodes {
  static constexpr std::size_t kNoOwner =
      std::numeric_limits<std::size_t>::max();

  // In `nodes` order -- ascending CST id, which is postorder: a child is
  // built before its parent, so an owner always sits later than what it owns.
  std::vector<const AstNode*> nodes;
  std::vector<CstNodeView> nameNodes;
  // The index of each node's nearest named ancestor, `kNoOwner` at a root.
  std::vector<std::size_t> owners;
};

[[nodiscard]] NamedNodes collect_named_nodes(const Document& document,
                                             const NameProvider& names,
                                             const CancellationToken& token);

}  // namespace cursiv
