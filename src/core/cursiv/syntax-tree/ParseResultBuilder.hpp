// How the result of a parse is built over its tree, and the only writer of
// one: the nodes, the text conversions keep, the references and the
// diagnostics. The result is the caller's, and outlives the builder.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstArena.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv {

class ParseResultBuilder {
 public:
  // Builds `out`, a result that holds its tree and nothing else yet, for a
  // language whose AST `meta` describes. Its diagnostics start as the repairs
  // the parse made.
  ParseResultBuilder(ParseResult& out, const detail::AstMetaData& meta);
  ParseResultBuilder(const ParseResultBuilder&) = delete;
  ParseResultBuilder& operator=(const ParseResultBuilder&) = delete;

  [[nodiscard]] const Cst& cst() const noexcept { return _tree.cst; }

  // Room for this many nodes and references.
  void reserve(std::size_t nodes, std::size_t references);

  // A new node of `T` for record `id`, on the result's list.
  template <class T>
  CURSIV_NOINLINE T* createNode(NodeId id) {
    T* const node = _arena.make<T>();
    node->cstNodeId = id;
    try {
      CURSIV_INLINE_CALLS _tree.nodes.emplace_back(node);
    } catch (...) {
      node->~T();
      throw;
    }
    return node;
  }
  // `node` spans record `id` from now on, a later one than its own.
  void moveNode(AstNode& node, NodeId id, bool filled = false) {
    if (node.cstNodeId == id) return;
    if (filled) _tree.origins.push_back({node.cstNodeId, &node});
    if (&node != _tree.nodes.back()) _unsorted = true;
    node.cstNodeId = id;
  }
  // A member of `parent` took `child`.
  CURSIV_ALWAYS_INLINE void attach(AstNode& child, AstNode& parent) noexcept {
    child.parent = &parent;
    ++_attached;
  }

  // `parts` joined, as text that lives as long as the tree.
  [[nodiscard]] std::string_view keepText(std::span<const std::string_view> parts) const {
    return _tree.keepText(parts, &_joined);
  }

  // Binds the reference `slot`, member `member` of `owner` and element
  // `index` of it where the member is a list, to the name `name` read at
  // record `at`. `narrow` says which of the member's narrowings its site
  // states.
  template <class Slot>
  CURSIV_ALWAYS_INLINE void bindReference(Slot& slot, AstNode* owner, std::uint16_t member,
                                          std::uint32_t index, NodeId at, std::string_view name,
                                          std::uint8_t narrow) {
    detail::RefAccess::bind(slot, at, name, narrow);
    detail::RefAccess::resolver(slot) = _resolver;
    detail::RefAccess::handle(slot) = static_cast<std::uint32_t>(_handles->size());
    _handles->emplace_back(owner, member, index);
  }

  void report(ParseDiagnostic diagnostic) { _out.diagnostics.push_back(std::move(diagnostic)); }

  // Ends the result, `root` its value: the nodes no member ties to it are
  // destroyed, and the list is in record order.
  void finish(AstNode* root, AstBuildError error);

 private:
  void prune(AstNode* root);

  ParseResult& _out;
  ParseResult::Tree& _tree;
  mutable ParseResult::Tree::Joined _joined;
  // Where the next node is made, on the tree's pool.
  detail::AstArena _arena;
  // What answers the references and lists them: null for a language with no
  // reference member.
  const detail::RefResolver* _resolver = nullptr;
  std::vector<detail::RefHandle>* _handles = nullptr;
  // Nodes a member took.
  std::size_t _attached = 0;
  // A node took a later record while others were listed after it.
  bool _unsorted = false;
};

}  // namespace cursiv
