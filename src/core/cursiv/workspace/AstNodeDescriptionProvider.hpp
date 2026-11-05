#pragma once

#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

// One description for one node. Which nodes get described is the scope
// computation's question -- exported and local are two answers over one tree --
// so a walk here could only ever serve one of them.

namespace cursiv {

struct Document;

class AstNodeDescriptionProvider {
 public:
  virtual ~AstNodeDescriptionProvider() noexcept = default;

  // Nothing for an empty name: a description that names nothing is
  // unresolvable, so it is refused where it would be made. Both defaults are
  // asked of the node itself -- its name of the `NameProvider`, its document of
  // the climb.
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
