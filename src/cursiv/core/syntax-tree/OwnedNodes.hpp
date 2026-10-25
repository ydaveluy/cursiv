#pragma once

#include <span>


namespace cursiv {

struct AstNode;

// Which records carry an AST node -- `ParseResult::nodes`, sorted by the
// nodes' `cstNodeId`. It is what ends a search: a construction asking for its
// own `}` must not be answered with a nested one's.
using OwnedNodes = std::span<AstNode* const>;

}  // namespace cursiv
