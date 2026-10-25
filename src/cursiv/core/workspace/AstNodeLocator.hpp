#pragma once

#include <string>
#include <string_view>

// A node's address inside its document, as text: `/elements@2/features@0`.
// Two questions answered here: the path of a node, and the node at a path.
// cursiv names a symbol by its `NodeId` everywhere it can -- one integer, no
// walk -- but a path is what survives leaving the process: a message to a
// client, a stored bookmark, anywhere an id means nothing.

namespace cursiv {

struct AstNode;

class AstNodeLocator {
 public:
  virtual ~AstNodeLocator() noexcept = default;

  // Empty for the root of a document: a path is relative to what contains it.
  [[nodiscard]] virtual std::string getAstNodePath(
      const AstNode& node) const = 0;

  // Null when the path names nothing under `node`.
  // Takes a mutable node because it hands one back: a path names a node to be
  // read and written, and a `const_cast` on the way out is banned here.
  [[nodiscard]] virtual AstNode* getAstNode(AstNode& node,
                                            std::string_view path) const = 0;
};

}  // namespace cursiv
