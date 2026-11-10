#pragma once

#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

// One description for one node; the scope computation decides which nodes are
// described.

namespace cursiv {

struct Document;

class AstNodeDescriptionProvider {
 public:
  virtual ~AstNodeDescriptionProvider() noexcept = default;

  // Nothing for an empty name, which nothing could resolve. By default the name
  // comes from the `NameProvider` and the document from the node.
  [[nodiscard]] virtual std::optional<AstNodeDescription> createDescription(
      AstNode& node, std::string_view name = {},
      const Document* document = nullptr) const = 0;
  // A description keeps a view of its name: a temporary is refused where it is
  // written rather than dangling silently. It wins the overload as an exact
  // match.
  template <detail::owning_text_rvalue S>
  std::optional<AstNodeDescription> createDescription(
      AstNode&, S&&, const Document* = nullptr) const {
    detail::refuse_owning_bind<S>();
    return std::nullopt;
  }
};

}  // namespace cursiv
