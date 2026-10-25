#pragma once

#include <optional>
#include <string>

#include "cursiv/core/syntax-tree/AstNode.hpp"

// The comment an author wrote above a declaration, as written -- the raw
// source `DocumentationProvider` renders into a reader's prose.

namespace cursiv {

struct Document;

class CommentProvider {
 public:
  virtual ~CommentProvider() noexcept = default;
  // The comment as its author wrote it -- the source image, delimiters
  // included, so a reader can still recognize a documentation comment -- or
  // nullopt when the node carries none. Never an empty string. `document` is
  // the node's home; left out, the node is asked for its own -- passing it
  // saves the climb when the caller already has it. A node no document hosts
  // answers nullopt.
  [[nodiscard]] virtual std::optional<std::string> getComment(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
