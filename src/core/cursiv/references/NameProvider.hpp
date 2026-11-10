#pragma once

#include <concepts>
#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"

// The name of a node, and where it is written. `getName` does not read the
// CST, so indexing stays cheap; only a caller that needs the range pays for the
// CST lookup.

namespace cursiv {

struct Document;

// A class with a text member called `name`.
template <class T>
concept NamedAstNode =
    std::derived_from<T, AstNode> && detail::has_name_member<T>;

// The same question for a node whose class is not known here, answered by its
// type tag.
[[nodiscard]] constexpr bool is_named(const AstNode& node) noexcept {
  return node.type != nullptr && node.type->readName != nullptr;
}

class NameProvider {
 public:
  virtual ~NameProvider() noexcept = default;

  // Absent when the node declares no name. The view points into the node or
  // the document.
  [[nodiscard]] virtual std::optional<std::string_view> getName(
      const AstNode& node) const = 0;

  // Absent when the name has no source range. `document` holds the tree the
  // node's `cstNodeId` indexes; null means the node's own document.
  [[nodiscard]] virtual std::optional<CstNodeView> getNameNode(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
