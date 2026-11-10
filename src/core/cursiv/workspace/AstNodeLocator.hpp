#pragma once

#include <string>
#include <string_view>

// A node's path inside its document, as text: `/elements@2/features@0`. The
// path of a node, and the node at a path: unlike a `NodeId`, a path still
// means something outside the process.

namespace cursiv {

struct AstNode;

class AstNodeLocator {
 public:
  virtual ~AstNodeLocator() noexcept = default;

  // Empty for the root of a document: a path is relative to what contains it.
  [[nodiscard]] virtual std::string getAstNodePath(
      const AstNode& node) const = 0;

  // Null when the path names nothing under `node`.
  // Takes a mutable node because it returns one.
  [[nodiscard]] virtual AstNode* getAstNode(AstNode& node,
                                            std::string_view path) const = 0;
};

}  // namespace cursiv
