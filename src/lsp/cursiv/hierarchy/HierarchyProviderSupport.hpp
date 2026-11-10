#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/references/References.hpp"
#include "cursiv/utils/Cancellation.hpp"

// Shared by the call and type hierarchies. An item holds only a URI and a
// range, resolved through the store: a document gone resolves to nothing.

namespace cursiv {

struct Document;
class Documents;

// The declarations a position denotes, all of them: a multi-reference names
// its whole set, and prepare gives one item per target.
[[nodiscard]] std::vector<const AstNode*> hierarchy_nodes_at(
    const References& references, const Document& document,
    lsp::Position position, const CancellationToken& cancelToken);

// The node an item names, read in the document's current text: after an
// edit, an older item names the declaration that stands there.
[[nodiscard]] const AstNode* hierarchy_node_of_item(
    const Documents& store, const References& references,
    const lsp::DocumentUri& uri, lsp::Position selection,
    const CancellationToken& cancelToken);

}  // namespace cursiv
