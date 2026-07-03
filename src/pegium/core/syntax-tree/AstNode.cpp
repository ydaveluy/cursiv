#include <pegium/core/syntax-tree/AstNode.hpp>

#include <pegium/core/syntax-tree/AstArena.hpp>

namespace pegium {

CstNodeView AstNode::getCstNode() const noexcept {
  if (_arena == nullptr || _cstNodeId == kNoNode) {
    return CstNodeView{};
  }
  return CstNodeView{_arena->cstRoot(), _cstNodeId};
}

const AstReflection *ast_reflection_of(const AstNode &node) noexcept {
  const auto *arena = node.arena();
  return arena != nullptr ? arena->reflection() : nullptr;
}

} // namespace pegium
