// Walking an AST: what a node contains, read through the reflection that knows
// its members, and what contains it, read through its parents.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"

namespace cursiv {

// What a walk does past a node.
enum class AstWalk : std::uint8_t { enter, prune };

// Each node `node` directly contains, in member order.
template <class Visit>
void visit_contents(const AstReflection& reflection, const AstNode& node,
                    Visit&& visit) {
  reflection.visitMembers(
      node, [&](std::string_view, std::uint32_t, AstNode& child) {
        visit(child);
      });
}

// Each node below `root`, a parent before what it contains, in member order.
// `visit` may return `AstWalk::prune` to skip what a node contains. The walk
// keeps its own stack: a deep tree costs no native stack.
template <class Visit>
void visit_all_contents(const AstReflection& reflection, const AstNode& root,
                        Visit&& visit) {
  std::vector<AstNode*> pending;
  const auto enter = [&](const AstNode& node) {
    const std::size_t from = pending.size();
    visit_contents(reflection, node,
                   [&](AstNode& child) { pending.push_back(&child); });
    std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(from),
                 pending.end());
  };
  enter(root);
  while (!pending.empty()) {
    AstNode& node = *pending.back();
    pending.pop_back();
    if constexpr (std::is_void_v<decltype(visit(node))>) {
      visit(node);
      enter(node);
    } else if (visit(node) != AstWalk::prune) {
      enter(node);
    }
  }
}

// `node` itself or the nearest node containing it that is a `T`, or null.
template <class T>
[[nodiscard]] const T* container_of_type(const AstNode* node) noexcept {
  for (; node != nullptr; node = node->parent)
    if (const T* const found = ast_cast<T>(node)) return found;
  return nullptr;
}

}  // namespace cursiv
