#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

#include <pegium/core/syntax-tree/AstReflection.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/syntax-tree/Reference.hpp>

namespace pegium {

class AstArena;

struct AstNode;

/// Returns the reflection registry for `node`'s arena, or nullptr when no
/// language services are bound (standalone test fixtures).
[[nodiscard]] const AstReflection *
ast_reflection_of(const AstNode &node) noexcept;

/// Base type for every AST node produced by pegium parsers.
///
/// `AstNode` stores the structural links of the tree:
/// - direct children (via an intrusive sibling-linked list)
/// - parent/container metadata
/// - the originating CST node when available
///
/// Nodes are intentionally non-copyable and non-movable because container and
/// CST links point into a concrete tree instance.
struct AstNode {
  AstNode() = default;

  /// AST nodes are tree-owned and cannot be moved.
  AstNode(AstNode &&other) = delete;
  /// AST nodes are tree-owned and cannot be copied.
  AstNode(const AstNode &other) = delete;

  /// AST nodes are tree-owned and cannot be move-assigned.
  AstNode &operator=(AstNode &&other) = delete;
  /// AST nodes are tree-owned and cannot be copy-assigned.
  AstNode &operator=(const AstNode &other) = delete;

  /// Virtual destructor for polymorphic AST hierarchies.
  virtual ~AstNode() noexcept = default;

  /// A reference to an AstNode of type T.
  ///
  /// References participate in linking and can resolve across documents.
  template <typename T> using reference = Reference<T>;
  /// A multi-valued reference to AST nodes of type `T`.
  template <typename T> using multi_reference = MultiReference<T>;

  /// A pointer on an object of type T.
  ///
  /// AstNode children are owned by the document's `AstArena`. The field stores
  /// a non-owning raw pointer; the arena handles destruction at parse-result
  /// teardown.
  template <typename T> using pointer = T *;

  /// Optional value convenience alias used in AST structs.
  template <typename T> using optional = std::optional<T>;
  /// Variant value convenience alias used in AST structs.
  template <typename... T> using variant = std::variant<T...>;

  /// Convenience aliases re-exported for generated AST declarations.
  using int8_t = std::int8_t;
  using int16_t = std::int16_t;
  using int32_t = std::int32_t;
  using int64_t = std::int64_t;
  using uint8_t = std::uint8_t;
  using uint16_t = std::uint16_t;
  using uint32_t = std::uint32_t;
  using uint64_t = std::uint64_t;
  using string = std::string;

  /// A vector of elements of type T.
  /// @tparam T type of element
  template <typename T> using vector = std::vector<T>;

  /// Returns the direct children of this node.
  ///
  /// Iteration follows attach-order (which mirrors source order for parser-built
  /// trees). Child pointers are never null.
  auto getContent() noexcept { return ChildRange<AstNode *>(_firstChild); }
  /// Returns the direct children of this node.
  auto getContent() const noexcept {
    return ChildRange<const AstNode *>(_firstChild);
  }

  /// Returns the direct children of type T.
  template <typename T>
    requires std::derived_from<T, AstNode>
  auto getContent() const noexcept {
    return of_type<T>(getContent());
  }
  /// Returns the direct children of type T.
  template <typename T>
    requires std::derived_from<T, AstNode>
  auto getContent() noexcept {
    return of_type<T>(getContent());
  }

  /// Returns all descendants of this node.
  ///
  /// The current node itself is not included. Traversal order is depth-first
  /// pre-order and preserves child order. Descendant pointers are never null.
  auto getAllContent() noexcept { return DescendantRange<AstNode *>(this); }
  auto getAllContent() const noexcept {
    return DescendantRange<const AstNode *>(this);
  }

  /// Returns all descendants of type T.
  template <typename T>
    requires std::derived_from<T, AstNode>
  auto getAllContent() const noexcept {
    return of_type<T>(getAllContent());
  }

  /// Returns all descendants of type T.
  template <typename T>
    requires std::derived_from<T, AstNode>
  auto getAllContent() noexcept {
    return of_type<T>(getAllContent());
  }

  /// Returns `true` when this AST node is associated with a CST node.
  [[nodiscard]] bool hasCstNode() const noexcept {
    return _cstNodeId != kNoNode;
  }

  /// Returns the CST node from which this AST node was parsed.
  ///
  /// Callers should check `hasCstNode()` before assuming the result is valid.
  /// Returns by value: the view is materialized from the owning arena's CST
  /// root and the stored node id, so it costs a single memory load.
  [[nodiscard]] CstNodeView getCstNode() const noexcept;

  /// Associates this AST node with its originating CST node.
  ///
  /// The view's root must match the CST root attached to this node's arena.
  [[gnu::always_inline]] void setCstNode(const CstNodeView &node) noexcept {
    assert(node.valid());
    assert(_cstNodeId == kNoNode);
    _cstNodeId = node.id();
  }

  /// Returns the parent node, or nullptr if this is the root.
  const AstNode *getContainer() const noexcept { return _container; }
  /// Returns the parent node, or nullptr if this is the root.
  AstNode *getContainer() noexcept { return _container; }

  /// Returns the nearest ancestor of type `T`, or `nullptr` if none is found.
  ///
  /// If the current node itself matches, it is returned.
  template <typename T>
    requires std::derived_from<T, AstNode>
  const T *getContainer() const noexcept {
    const auto *item = this;
    do {
      if (auto *casted = ast_ptr_cast<const T>(item)) {
        return casted;
      }
      item = item->getContainer();
    } while (item);
    return nullptr;
  }

  /// Returns the nearest ancestor of type `T`, or `nullptr` if none is found.
  ///
  /// If the current node itself matches, it is returned.
  template <typename T>
    requires std::derived_from<T, AstNode>
  T *getContainer() noexcept {
    auto *item = this;
    do {
      if (auto *casted = ast_ptr_cast<T>(item)) {
        return casted;
      }
      item = item->getContainer();
    } while (item);
    return nullptr;
  }

  /// Attaches this node to `container` as the last child.
  ///
  /// Container assignment is write-once: re-attaching a node that already has a
  /// parent trips an assertion.
  void setContainer(AstNode &container) noexcept {
    assert(_container == nullptr);
    _container = &container;
    if (container._lastChild == nullptr) {
      container._firstChild = this;
    } else {
      container._lastChild->_nextSibling = this;
    }
    container._lastChild = this;
  }

  /// Returns the symbol id of this node within its owning `AstArena`.
  ///
  /// The id is dense within the arena (0..N-1 in allocation order) and is the
  /// canonical key used by `Document::getAstNode(SymbolId)`.
  [[nodiscard]] std::uint32_t symbolId() const noexcept { return _symbolId; }

  /// Returns the arena that owns this AST node, or nullptr for a default-
  /// constructed node not yet placed in an arena.
  [[nodiscard]] AstArena *arena() const noexcept { return _arena; }

private:
  friend class AstArena;

  /// Parent in the AST. Null for the root.
  AstNode *_container = nullptr;
  /// First child in attach order, or null when this node has no children.
  AstNode *_firstChild = nullptr;
  /// Last child in attach order. Used to keep `setContainer` O(1).
  AstNode *_lastChild = nullptr;
  /// Next sibling under the same parent, or null when this is the last child.
  AstNode *_nextSibling = nullptr;
  /// Owning arena. Set by `AstArena::create`. The arena reaches the workspace
  /// document and the originating CST root without per-node duplication.
  AstArena *_arena = nullptr;
  /// Index of this node within the owning arena. Set by `AstArena::create`.
  NodeId _symbolId = kNoNode;
  /// CST node id within the arena's CST root, or `kNoNode` when unattached.
  /// `getCstNode()` materializes a `CstNodeView` from this id and the arena.
  NodeId _cstNodeId = kNoNode;

  template <typename T, typename Range>
  static auto of_type(Range &&range) noexcept {
    using Ptr = std::ranges::range_value_t<Range>;
    using CastedPtr =
        std::conditional_t<std::is_const_v<std::remove_pointer_t<Ptr>>,
                           const T *, T *>;
    return std::forward<Range>(range) |
           std::views::filter([](Ptr ptr) noexcept {
             return is_a<T>(ptr);
           }) |
           std::views::transform(
               [](Ptr ptr) noexcept { return static_cast<CastedPtr>(ptr); });
  }

  /// Forward iterator walking the sibling chain `_firstChild` → `_nextSibling`.
  template <typename NodePtr> class ChildIterator {
  public:
    using value_type = NodePtr;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using reference = NodePtr;
    using pointer = void;
    ChildIterator() = default;
    explicit ChildIterator(NodePtr current) noexcept : _current(current) {}
    reference operator*() const noexcept { return _current; }
    ChildIterator &operator++() noexcept {
      _current = _current->_nextSibling;
      return *this;
    }
    ChildIterator operator++(int) noexcept {
      auto temp = *this;
      ++(*this);
      return temp;
    }
    bool operator==(const ChildIterator &other) const noexcept = default;

  private:
    NodePtr _current = nullptr;
  };

  template <typename NodePtr>
  class ChildRange : public std::ranges::view_interface<ChildRange<NodePtr>> {
  public:
    using iterator = ChildIterator<NodePtr>;
    ChildRange() = default;
    explicit ChildRange(NodePtr first) noexcept : _first(first) {}
    iterator begin() const noexcept { return iterator{_first}; }
    iterator end() const noexcept { return iterator{}; }

  private:
    NodePtr _first = nullptr;
  };

  /// Depth-first pre-order iterator over descendants (root excluded).
  template <typename NodePtr> class DescendantIterator {
  public:
    using value_type = NodePtr;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using reference = NodePtr;
    using pointer = void;
    DescendantIterator() = default;
    explicit DescendantIterator(NodePtr root) {
      if (root != nullptr && root->_firstChild != nullptr) {
        _stack.push_back(root->_firstChild);
      }
    }
    reference operator*() const { return _stack.back(); }
    DescendantIterator &operator++() {
      NodePtr current = _stack.back();
      _stack.pop_back();
      // Push next sibling first (visited after the current subtree).
      if (current->_nextSibling != nullptr) {
        _stack.push_back(current->_nextSibling);
      }
      // Then descend into the first child (visited next).
      if (current->_firstChild != nullptr) {
        _stack.push_back(current->_firstChild);
      }
      return *this;
    }
    DescendantIterator operator++(int) {
      auto temp = *this;
      ++(*this);
      return temp;
    }
    bool operator==(const DescendantIterator &other) const = default;

  private:
    std::vector<NodePtr> _stack;
  };

  template <typename NodePtr>
  class DescendantRange
      : public std::ranges::view_interface<DescendantRange<NodePtr>> {
  public:
    using iterator = DescendantIterator<NodePtr>;
    explicit DescendantRange(NodePtr root) noexcept : _root(root) {}
    iterator begin() const { return iterator{_root}; }
    iterator end() const { return iterator{}; }

  private:
    NodePtr _root = nullptr;
  };
};
static_assert(sizeof(AstNode) <= 56,
              "AstNode should stay compact");
/// AST base class for declarations that expose a semantic `name`.
///
/// Languages can inherit from `NamedAstNode` to let the default naming
/// services read names directly from the AST without additional customization.
struct NamedAstNode : AstNode {
  string name;
};

template <typename T>
concept DefaultConstructibleAstNode =
    std::derived_from<T, AstNode> && std::default_initializable<T>;

// Parser-managed AST nodes are built as mutable shells and populated through
// assignments after recognition succeeds. Concrete node types produced
// directly by Rule/Create/Nest/Infix therefore need to be default
// constructible. This is an intentional runtime contract of the generic
// parser design, not a recommendation to encode semantic invariants in
// constructors.

template <typename T, typename U>
  requires std::derived_from<std::remove_cv_t<T>, AstNode> &&
           std::derived_from<std::remove_cv_t<U>, AstNode>
[[nodiscard]] T *ast_ptr_cast(U *ptr) noexcept;

/// Returns whether `node` is an instance of `T`.
///
/// Delegates to `ast_ptr_cast`, the single source of the reflection-lookup /
/// RTTI-fallback dispatch rule. Both are header-inline, so the whole type check
/// still inlines on this hot path.
template <typename T>
  requires std::derived_from<T, AstNode>
[[nodiscard]] bool is_a(const AstNode &node) noexcept {
  return ast_ptr_cast<const T>(&node) != nullptr;
}

/// Pointer overload.
template <typename T>
  requires std::derived_from<T, AstNode>
[[nodiscard]] bool is_a(const AstNode *node) noexcept {
  return node != nullptr && is_a<T>(*node);
}

/// Casts an AST node pointer to a concrete derived type, returning nullptr on
/// type mismatch.
template <typename T, typename U>
  requires std::derived_from<std::remove_cv_t<T>, AstNode> &&
           std::derived_from<std::remove_cv_t<U>, AstNode>
[[nodiscard]] T *ast_ptr_cast(U *ptr) noexcept {
  if (ptr == nullptr) {
    return nullptr;
  }
  if (const auto *reflection = ast_reflection_of(*ptr);
      reflection != nullptr &&
      reflection->isKnown(std::type_index(typeid(T)))) {
    return reflection->isSubtype(std::type_index(typeid(*ptr)),
                                 std::type_index(typeid(T)))
               ? static_cast<T *>(ptr)
               : nullptr;
  }
  // No reflection, or `T` is an abstract base the grammar never names (so the
  // reflection cannot answer): C++ RTTI always can.
  return dynamic_cast<T *>(ptr);
}

} // namespace pegium
