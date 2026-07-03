#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <string_view>

#include <pegium/core/syntax-tree/RootCstNode.hpp>

namespace pegium {

class CstNodeView;

/// Forward iterator over the direct children of a CST node or CST root.
///
/// Iteration follows sibling order. Dereferencing yields a lightweight
/// `CstNodeView` by value.
// ---------------- Child iterator ----------------

class ChildIterator {
public:
  using iterator_concept = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type = CstNodeView;
  using difference_type = std::ptrdiff_t;
  using reference = value_type;
  using pointer = void;

  /// Creates the end iterator.
  ChildIterator() = default;
  /// Creates an iterator positioned on child node id `cur` in `root`.
  ChildIterator(const RootCstNode *root, NodeId cur) noexcept
      : _root(root), _cur(cur) {}

  /// Returns the current child view.
  reference operator*() const noexcept;
  /// Advances to the next sibling.
  ChildIterator &operator++() noexcept;
  /// Advances to the next sibling and returns the previous iterator state.
  ChildIterator operator++(int) noexcept;

  friend bool operator==(const ChildIterator &a,
                         const ChildIterator &b) noexcept = default;

private:
  const RootCstNode *_root = nullptr;
  NodeId _cur = kNoNode;
};

/// Lightweight, non-owning view over one CST node inside a `RootCstNode`.
///
/// `CstNodeView` is cheap to copy and remains valid as long as the owning
/// `RootCstNode` stays alive.
///
/// Offsets exposed by this type follow the half-open convention `[begin, end)`.
// ---------------- CstNodeView ----------------

class CstNodeView {
public:
  /// Creates an invalid view.
  CstNodeView() = default;
  /// Creates a view for node `id` inside `root`.
  CstNodeView(const RootCstNode *root, NodeId id) noexcept
      : _root(root), _id(id) {}

  /// Returns `true` when this view points to an existing CST node.
  [[nodiscard]] bool valid() const noexcept {
    return _root != nullptr && _id != kNoNode && _id < _root->_nodeCount;
  }

  /// Boolean shorthand for `valid()`.
  explicit operator bool() const noexcept { return valid(); }

  /// Returns the next node in allocation order.
  ///
  /// This is a low-level traversal primitive over the underlying storage, not
  /// the next sibling in the tree.
  [[nodiscard]] CstNodeView next() const noexcept {
    if (auto nextId = _id + 1; nextId < _root->_nodeCount) {
      return {_root, nextId};
    }
    return {};
  }

  /// Returns the next sibling node.
  ///
  /// The result is invalid when there is no following sibling.
  [[nodiscard]] CstNodeView nextSibling() const noexcept {
    const auto nextSiblingId = _root->node(_id).nextSiblingId;
    return nextSiblingId == kNoNode ? CstNodeView{} : CstNodeView{_root, nextSiblingId};
  }

  /// Returns the previous node in allocation order.
  ///
  /// This is a low-level traversal primitive over the underlying storage, not
  /// the previous sibling in the tree.
  [[nodiscard]] CstNodeView previous() const noexcept {
    if (_id > 0) {
      return {_root, _id - 1};
    }
    return {};
  }

  /// Returns `true` when there is uncovered source text after this node.
  ///
  /// The comparison is done against the next node in allocation order, or
  /// against the end of the root text for the last node.
  [[nodiscard]] bool hasGapAfter() const noexcept {
    if (auto nextId = _id + 1; nextId < _root->_nodeCount) {
      return getEnd() < _root->node(nextId).begin;
    }
    return getEnd() < _root->getText().length();
  }

  /// Returns `true` when there is uncovered source text before this node.
  [[nodiscard]] bool hasGapBefore() const noexcept {
    if (_id > 0) {
      return getBegin() > _root->node(_id - 1).end;
    }
    return getBegin() > 0;
  }

  /// Returns the exact source text covered by this node.
  [[nodiscard]] std::string_view getText() const noexcept {
    const auto &n = _root->node(_id);
    const auto text = _root->getText();
    return std::string_view(text.data() + n.begin, n.end - n.begin);
  }
  /// Returns the begin offset of this node in the document text, inclusive.
  [[nodiscard]] TextOffset getBegin() const noexcept { return _root->node(_id).begin; }
  /// Returns the end offset of this node in the document text, exclusive.
  [[nodiscard]] TextOffset getEnd() const noexcept { return _root->node(_id).end; }
  /// Returns `true` for hidden CST nodes such as comments or skipped trivia.
  [[nodiscard]] bool isHidden() const noexcept { return _root->node(_id).isHidden; }
  /// Returns `true` when this node has no direct children.
  [[nodiscard]] bool isLeaf() const noexcept { return _root->node(_id).isLeaf; }
  /// Returns `true` when this node originates from parser recovery.
  [[nodiscard]] bool isRecovered() const noexcept { return _root->node(_id).isRecovered; }

  /// Returns the grammar element responsible for producing this CST node.
  [[nodiscard]] const grammar::AbstractElement *getGrammarElement() const noexcept {
    const auto *grammarElement = _root->node(_id).grammarElement;
    assert(grammarElement && "Every CstNode must reference a grammar element");
    return grammarElement;
  }
  /// Returns the underlying raw CST storage record.
  ///
  /// This is primarily useful for low-level algorithms that need direct access
  /// to builder-oriented metadata such as `nextSiblingId`.
  [[nodiscard]] inline const CstNode &node() const noexcept { return _root->node(_id); }
  /// Returns the owning CST root.
  [[nodiscard]] inline const RootCstNode &root() const noexcept { return *_root; }
  /// Returns the underlying node id inside the owning root.
  [[nodiscard]] inline NodeId id() const noexcept { return _id; }
  /// Returns an iterator over the direct children of this node.
  [[nodiscard]] ChildIterator begin() const noexcept {
    if (_root->node(_id).isLeaf) {
      return ChildIterator(_root, kNoNode);
    }
    return ChildIterator(_root, _id + 1);
  }

  /// Returns the end iterator for child traversal.
  [[nodiscard]] ChildIterator end() const noexcept { return ChildIterator(_root, kNoNode); }

private:
  const RootCstNode *_root = nullptr;
  NodeId _id = kNoNode;
};

/// Returns an iterator over the top-level CST nodes.
inline ChildIterator RootCstNode::begin() const noexcept {
  return ChildIterator(this, _nodeCount == 0 ? kNoNode : 0u);
}

/// Returns the end iterator for top-level CST traversal.
inline ChildIterator RootCstNode::end() const noexcept {
  return ChildIterator(this, kNoNode);
}

/// Returns the CST node view for `id`, or an invalid view when out of bounds.
inline CstNodeView RootCstNode::get(NodeId id) const noexcept {
  if (id == kNoNode || id >= _nodeCount) {
    return {};
  }
  return CstNodeView(this, id);
}

/// Dereferences the iterator into a `CstNodeView`.
inline ChildIterator::reference ChildIterator::operator*() const noexcept {
  return CstNodeView(_root, _cur);
}

/// Advances the iterator to the next sibling.
inline ChildIterator &ChildIterator::operator++() noexcept {
  if (_cur == kNoNode) {
    return *this;
  }

  _cur = _root->node(_cur).nextSiblingId;
  return *this;
}

/// Advances the iterator and returns the previous position.
inline ChildIterator ChildIterator::operator++(int) noexcept {
  auto copy = *this;
  ++(*this);
  return copy;
}

} // namespace pegium
