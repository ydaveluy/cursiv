#include "cursiv/lsp/symbols/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFU;

}  // namespace

NamedNodes collect_named_nodes(const Document& document,
                               const NameProvider& names,
                               const CancellationToken& token) {
  const auto& all = document.parseResult.nodes;
  NamedNodes named;
  // A record is not a key: `wrap` builds its wrapper at the record its content
  // already sits on. The id opens a run of nodes, and the node is picked out
  // of that run by identity.
  std::vector<std::uint32_t> opens(document.parseResult.cst().size(), kNone);
  std::vector<std::uint32_t> rankAt(all.size(), kNone);
  for (std::uint32_t at = 0; at < all.size(); ++at) {
    throw_if_cancelled(token);
    const AstNode* const node = all[at];
    const NodeId record = node->cstNodeId;
    if (record >= opens.size()) continue;
    if (opens[record] == kNone) opens[record] = at;
    const std::optional<CstNodeView> nameNode = names.getNameNode(*node, &document);
    if (!nameNode.has_value()) continue;
    rankAt[at] = static_cast<std::uint32_t>(named.nodes.size());
    named.nodes.push_back(node);
    named.nameNodes.push_back(*nameNode);
  }

  named.owners.reserve(named.nodes.size());
  for (const AstNode* const node : named.nodes) {
    std::size_t owner = NamedNodes::kNoOwner;
    for (const AstNode* p = node->parent; p != nullptr; p = p->parent) {
      const NodeId record = p->cstNodeId;
      if (record >= opens.size() || opens[record] == kNone) continue;
      for (std::uint32_t at = opens[record];
           at < all.size() && all[at]->cstNodeId == record; ++at) {
        if (all[at] != p) continue;
        if (rankAt[at] != kNone) owner = rankAt[at];
        break;
      }
      if (owner != NamedNodes::kNoOwner) break;
    }
    named.owners.push_back(owner);
  }
  return named;
}

}  // namespace cursiv
