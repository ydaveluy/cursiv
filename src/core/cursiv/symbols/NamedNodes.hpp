// The nodes of a document that own a name node, and the nearest named
// ancestor of each: the nesting the outlines and semantic colors share.
#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {

class NameProvider;
struct Document;

namespace detail {

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

}  // namespace detail

}  // namespace cursiv
