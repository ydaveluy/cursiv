#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/utils/UriUtils.hpp"

// A URI-keyed store that answers by prefix: a flat map answers only "this
// exact URI", where a folder-shaped question -- every document under this
// root -- costs a walk of the tree. The key is segments, split on `/` after
// normalisation, so two spellings of one URI land on one node.

namespace cursiv {

template <class T>
class UriTrie {
  static_assert(std::is_nothrow_move_assignable_v<T>,
                "a UriTrie value must move without throwing: a move that "
                "throws halfway leaves children under a moved-from root");

 public:
  UriTrie() = default;
  UriTrie(const UriTrie&) = delete;
  UriTrie& operator=(const UriTrie&) = delete;
  UriTrie(UriTrie&& other) noexcept : _root(std::move(other._root)) {
    reparentRootChildren();
  }
  UriTrie& operator=(UriTrie&& other) noexcept {
    if (this != &other) {
      _root = std::move(other._root);
      reparentRootChildren();
    }
    return *this;
  }

  void clear() {
    _root.children.clear();
    _root.element.reset();
  }

  void insert(std::string_view uri, T element) {
    getNode(normalize_uri(uri), true)->element = std::move(element);
  }

  // The subtree goes with it: erasing a folder erases what it holds. The
  // root has no parent to be erased from, so it stays: that is `clear`.
  void erase(std::string_view uri) {
    Node* const node = getNode(normalize_uri(uri), false);
    if (node != nullptr && node->parent != nullptr)
      node->parent->children.erase(node->segment);
  }

  // ...where this drops the value alone, then the spine that held only it: a
  // path kept for a document that is gone would answer `findAll` for ever.
  void eraseValue(std::string_view uri) {
    Node* node = getNode(normalize_uri(uri), false);
    if (node == nullptr) return;
    node->element.reset();
    while (node->parent != nullptr && node->children.empty() &&
           !node->element.has_value()) {
      Node* const parent = node->parent;
      parent->children.erase(node->segment);
      node = parent;
    }
  }

  [[nodiscard]] bool has(std::string_view uri) const {
    const Node* const node = getNode(normalize_uri(uri));
    return node != nullptr && node->element.has_value();
  }

  [[nodiscard]] const T* find(std::string_view uri) const {
    const Node* const node = getNode(normalize_uri(uri));
    return node != nullptr && node->element.has_value()
               ? std::addressof(*node->element)
               : nullptr;
  }
  [[nodiscard]] T* find(std::string_view uri) {
    Node* const node = getNode(normalize_uri(uri), false);
    return node != nullptr && node->element.has_value()
               ? std::addressof(*node->element)
               : nullptr;
  }

  [[nodiscard]] std::vector<T> all() const { return collectValues(_root); }

  // Everything under `uri`, itself included -- the query a flat map cannot
  // answer without reading every key.
  [[nodiscard]] std::vector<T> findAll(std::string_view uri) const {
    const Node* const node = getNode(normalize_uri(uri));
    return node != nullptr ? collectValues(*node) : std::vector<T>{};
  }

 private:
  struct Node {
    std::string segment;
    Node* parent = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Node>> children;
    std::optional<T> element;
  };

  [[nodiscard]] static std::vector<std::string> splitSegments(
      std::string_view uri) {
    std::vector<std::string> segments;
    if (uri.empty()) return segments;
    std::size_t begin = 0;
    while (begin <= uri.size()) {
      const std::size_t end = uri.find('/', begin);
      if (end == std::string_view::npos) {
        segments.emplace_back(uri.substr(begin));
        break;
      }
      segments.emplace_back(uri.substr(begin, end - begin));
      begin = end + 1;
      if (begin == uri.size()) break;
    }
    return segments;
  }

  // One walk for both constnesses: a const one never creates, which the
  // `if constexpr` states rather than a second body repeating the descent.
  template <class NodeT>
  [[nodiscard]] static NodeT* getNodeImpl(NodeT& root,
                                          std::string_view normalizedUri,
                                          bool create) {
    NodeT* current = std::addressof(root);
    for (const std::string& segment : splitSegments(normalizedUri)) {
      auto child = current->children.find(segment);
      if (child == current->children.end()) {
        if constexpr (std::is_const_v<NodeT>) {
          return nullptr;
        } else {
          if (!create) return nullptr;
          child =
              current->children
                  .try_emplace(segment,
                               std::make_unique<Node>(Node{.segment = segment,
                                                           .parent = current,
                                                           .children = {},
                                                           .element = {}}))
                  .first;
        }
      }
      current = child->second.get();
    }
    return current;
  }

  [[nodiscard]] Node* getNode(std::string_view uri, bool create) {
    return getNodeImpl(_root, uri, create);
  }
  [[nodiscard]] const Node* getNode(std::string_view uri) const {
    return getNodeImpl(_root, uri, false);
  }

  static void appendValues(const Node& node, std::vector<T>& values) {
    if (node.element.has_value()) values.push_back(*node.element);
    for (const auto& [_, child] : node.children) appendValues(*child, values);
  }

  [[nodiscard]] static std::vector<T> collectValues(const Node& node) {
    std::vector<T> values;
    appendValues(node, values);
    return values;
  }

  Node _root;

  void reparentRootChildren() noexcept {
    for (auto& [_, child] : _root.children) child->parent = &_root;
  }
};

}  // namespace cursiv
