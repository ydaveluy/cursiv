#pragma once

#include <optional>
#include <string>

#include "cursiv/syntax-tree/AstNode.hpp"

// The comment written above a declaration, as written. `DocumentationProvider`
// renders it for a reader.

namespace cursiv {

struct Document;

class CommentProvider {
 public:
  virtual ~CommentProvider() noexcept = default;
  // The comment as written, delimiters included, or nullopt when the node has
  // none; never empty. A null `document` means the node's own document; a node
  // outside any document gives nullopt.
  [[nodiscard]] virtual std::optional<std::string> getComment(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
