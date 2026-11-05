#include "cursiv/symbols/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFU;

}  // namespace

NamedNodes collect_named_nodes(const Document& document,
                               const NameProvider& names,
                               const CancellationToken& token) {
  const auto& all = document.parseResult.nodes();
  NamedNodes named;
  // A record is not a key: `wrap` builds its wrapper at the record its content
  // already sits on. The id opens a run of nodes, and the node is picked out
  // of that run by identity.
  std::vector opens(document.parseResult.cst().size(), kNone);
  std::vector rankAt(all.size(), kNone);
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

  // The nearest named ancestor of each node on the way, found once: a long
  // chain of unnamed containers is climbed by the first symbol under it.
  constexpr std::size_t kUnknown = NamedNodes::kNoOwner - 1;
  std::vector ownerAt(all.size(), kUnknown);
  std::vector<std::uint32_t> climbed;
  named.owners.reserve(named.nodes.size());
  for (const AstNode* const node : named.nodes) {
    throw_if_cancelled(token);
    std::size_t owner = NamedNodes::kNoOwner;
    climbed.clear();
    for (const AstNode* p = node->parent; p != nullptr; p = p->parent) {
      const NodeId record = p->cstNodeId;
      if (record >= opens.size() || opens[record] == kNone) continue;
      std::uint32_t at = opens[record];
      while (at < all.size() && all[at]->cstNodeId == record && all[at] != p)
        ++at;
      if (at == all.size() || all[at] != p) continue;
      if (rankAt[at] != kNone) {
        owner = rankAt[at];
        break;
      }
      if (ownerAt[at] != kUnknown) {
        owner = ownerAt[at];
        break;
      }
      climbed.push_back(at);
    }
    for (const std::uint32_t at : climbed) ownerAt[at] = owner;
    named.owners.push_back(owner);
  }
  return named;
}

}  // namespace cursiv
