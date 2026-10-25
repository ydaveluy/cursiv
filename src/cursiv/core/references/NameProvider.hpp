#pragma once

#include <concepts>
#include <optional>
#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"

// The user-visible name of a node, and where it was written. The split is the
// point: `getName` answers without touching the tree, so indexing stays cheap,
// and the CST lookup is paid only by a caller that needs the range.

namespace cursiv {

struct Document;

// A class that declares a name, as a concept: the class either spells a text
// member called `name` or it does not, and nothing has to be inherited to say
// so.
template <class T>
concept NamedAstNode =
    std::derived_from<T, AstNode> && requires(const T& node) {
      { node.name } -> std::same_as<const AstNode::string_view&>;
    };

// ...and the same question of a node whose class is not known here, answered
// by the tag: whether a class declares a name is a fact about the class, so
// the tag carries the reader and no grammar is asked.
[[nodiscard]] constexpr bool is_named(const AstNode& node) noexcept {
  return node.type != nullptr && node.type->readName != nullptr;
}

class NameProvider {
 public:
  virtual ~NameProvider() noexcept = default;

  // Absent when the node declares no name. A view and not a string: the AST
  // borrows every identifier it holds, and copying one per call would be paid
  // on the indexing path.
  [[nodiscard]] virtual std::optional<std::string_view> getName(
      const AstNode& node) const = 0;

  // Absent when the name has no source range -- an unnamed node, one built by
  // hand, or a name a converter materialised rather than matched.
  //
  // The document is what holds the tree the node's `cstNodeId` indexes; left
  // out, the node is asked for its own. Passing it saves the climb when the
  // caller already has it.
  [[nodiscard]] virtual std::optional<CstNodeView> getNameNode(
      const AstNode& node, const Document* document = nullptr) const = 0;
};

}  // namespace cursiv
