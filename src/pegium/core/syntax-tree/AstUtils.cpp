#include <pegium/core/syntax-tree/AstUtils.hpp>

#include <memory>

#include <pegium/core/syntax-tree/AstArena.hpp>
#include <pegium/core/utils/Errors.hpp>

namespace pegium {

const workspace::Document *tryGetDocument(const AstNode &node) noexcept {
  if (const auto *arena = node.arena(); arena != nullptr) {
    return arena->document();
  }
  return nullptr;
}

const workspace::Document &getDocument(const AstNode &node) {
  auto *document = tryGetDocument(node);
  if (document == nullptr) {
    throw utils::MissingAstDocumentError("AST node has no document.");
  }
  return *document;
}

const AstNode *find_ast_node_at_offset(const AstNode &node, TextOffset offset) {
  if (!node.hasCstNode()) {
    return nullptr;
  }

  if (const auto cstNode = node.getCstNode();
      offset < cstNode.getBegin() || offset > cstNode.getEnd()) {
    return nullptr;
  }

  for (const auto *child : node.getContent()) {
    if (const auto *match = find_ast_node_at_offset(*child, offset);
        match != nullptr) {
      return match;
    }
  }

  return &node;
}

} // namespace pegium
