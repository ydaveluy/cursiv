#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/references/References.hpp"
#include "cursiv/core/utils/Cancellation.hpp"

// What the two hierarchies share: both ask in three steps -- prepare a root,
// then walk one way or the other. An item carries no pointer, only a URI and
// a range, so it resolves through the document store; gone from the
// workspace resolves to nothing, never a stale node.

namespace cursiv {

struct Document;
class Documents;

// The declarations a position denotes -- all of them: a multi-reference
// names its whole set, and prepare owes one item per target, never just the
// first.
[[nodiscard]] std::vector<const AstNode*> hierarchy_nodes_at(
    const References& references, const Document& document,
    lsp::Position position, const CancellationToken& cancelToken);

// The node a returned item named, reached back through the store. One node:
// an item's selection range is a declaration's own name, which denotes only
// itself.
[[nodiscard]] const AstNode* hierarchy_node_of_item(
    const Documents& store, const References& references,
    const lsp::DocumentUri& uri, lsp::Position selection,
    const CancellationToken& cancelToken);

}  // namespace cursiv
