#pragma once

#include <optional>
#include <string>

#include "cursiv/syntax-tree/AstNode.hpp"

// The comment written above a node, rendered for a reader. No documentation is
// a valid answer, not a failure.

namespace cursiv {

struct Document;

class DocumentationProvider {
 public:
  virtual ~DocumentationProvider() noexcept = default;
  // The documentation comment above a node, as markdown, or nullopt. A null
  // `document` means the node's own document.
  [[nodiscard]] virtual std::optional<std::string> getDocumentation(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
