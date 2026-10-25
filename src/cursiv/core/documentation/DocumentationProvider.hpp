#pragma once

#include <optional>
#include <string>

#include "cursiv/core/syntax-tree/AstNode.hpp"

// The prose an author wrote over a node, rendered for a reader. Nothing is
// an answer, not a failure.

namespace cursiv {

struct Document;

class DocumentationProvider {
 public:
  virtual ~DocumentationProvider() noexcept = default;
  // The documentation of a node: the comment its author wrote above it,
  // rendered to markdown -- or nothing, when no documentation comment
  // precedes it or the one that does says nothing. The raw image of the
  // comment is `CommentProvider`'s answer. `document` is the node's home;
  // left out, the node is asked for its own -- and a node no document hosts
  // answers nothing.
  [[nodiscard]] virtual std::optional<std::string> getDocumentation(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
