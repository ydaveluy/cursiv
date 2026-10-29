#include "cursiv/core/syntax-tree/CstUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/OwnedNodes.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/text/word_scalar.hpp"

namespace cursiv {

namespace {

// Which records opened a node, read in lockstep with the walk: the ids it
// asks strictly decrease, so a boundary is found by stepping down.
class OwnedCursor {
 public:
  OwnedCursor(OwnedNodes owned, NodeId root) noexcept
      : _first(owned.data()),
        _at(std::upper_bound(owned.data(), owned.data() + owned.size(), root,
                             [](NodeId want, const AstNode* row) {
                               return want < row->cstNodeId;
                             })) {}

  bool opens(NodeId id) noexcept {
    while (_at != _first && (*(_at - 1))->cstNodeId > id) --_at;
    return _at != _first && (*(_at - 1))->cstNodeId == id;
  }

 private:
  AstNode* const* _first;
  AstNode* const* _at;
};

// Backwards, which is how the run reads: postorder gives a parent's children
// in reverse, so the walk follows them there and a list is reversed once.
// A member's writer is asked before the boundary -- the span that wrote a
// member may open the node it holds -- and anything else after it: a keyword
// that is a nested node's only record is that node's.
template <bool kWriterFirst, class Match, class Take>
void walk(const Cst& cst, NodeId root, const Match& matches, const Take& take) {
  if (root == kNoNode || root >= cst.size()) return;
  OwnedCursor owned(cst.owned(), root);
  const NodeId begin = cst.subtree_begin(root);
  for (NodeId cursor = root; cursor > begin;) {
    const NodeId child = cursor - 1;
    if constexpr (!kWriterFirst) {
      if (owned.opens(child)) {
        cursor = cst.subtree_begin(child);
        continue;
      }
    }
    if (matches(child)) {
      take(child);
      cursor = cst.subtree_begin(child);
    } else if (kWriterFirst && owned.opens(child)) {
      cursor = cst.subtree_begin(child);
    } else {
      // The preceding record is the last child, or the previous sibling
      // when this record is childless. No recursive stack is needed.
      --cursor;
    }
  }
}

const Grammar* grammar_of(const CstNodeView& node) noexcept {
  return node ? node.cst->grammar() : nullptr;
}

template <bool kWriterFirst, class Match>
std::vector<CstNodeView> collect(const CstNodeView& node,
                                 const Match& matches) {
  std::vector<CstNodeView> found;
  const Cst& cst = *node.cst;
  walk<kWriterFirst>(cst, node.id, matches, [&](NodeId id) {
    found.push_back(CstNodeView{&cst, id});
  });
  std::reverse(found.begin(), found.end());
  return found;
}

// The last match the reverse walk reaches is the first in document order.
template <bool kWriterFirst, class Match>
std::optional<CstNodeView> first_of(const CstNodeView& node,
                                    const Match& matches) {
  std::optional<CstNodeView> first;
  const Cst& cst = *node.cst;
  walk<kWriterFirst>(cst, node.id, matches,
                     [&](NodeId id) { first = CstNodeView{&cst, id}; });
  return first;
}

std::optional<CstNodeView> clamped(const std::vector<CstNodeView>& found,
                                   std::size_t index) {
  if (found.empty()) return std::nullopt;
  return found[std::min(index, found.size() - 1)];
}

// A visible leaf whose bytes are the keyword and that names no rule: a
// literal the grammar spelled. The rule is asked last, of those leaves only.
bool spells_keyword(const Cst& cst, const Grammar& grammar, NodeId id,
                    std::string_view keyword) {
  const CstNode& n = cst[id];
  return n.subtreeSize == 1 && n.hidden == 0 && cst.text(id) == keyword &&
         grammar.recordRule(n).empty();
}

}  // namespace

std::vector<CstNodeView> find_nodes_for_feature(const CstNodeView& node,
                                                std::string_view feature) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || feature.empty()) return {};
  const Cst& cst = *node.cst;
  std::vector<CstNodeView> found = collect<true>(
      node, [&](NodeId id) { return grammar->recordWrites(cst[id], feature); });
  // The node's own record answers first: a rule whose match is one element
  // lays no record of its own, so the span that wrote the member opened it.
  if (grammar->recordFills(cst[node.id], feature))
    found.insert(found.begin(), node);
  return found;
}

std::optional<CstNodeView> find_node_for_feature(const CstNodeView& node,
                                                 std::string_view feature,
                                                 std::size_t index) {
  if (index != 0) return clamped(find_nodes_for_feature(node, feature), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || feature.empty()) return std::nullopt;
  const Cst& cst = *node.cst;
  if (grammar->recordFills(cst[node.id], feature)) return node;
  return first_of<true>(
      node, [&](NodeId id) { return grammar->recordWrites(cst[id], feature); });
}

std::vector<CstNodeView> find_nodes_for_keyword(const CstNodeView& node,
                                                std::string_view keyword) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return {};
  const Cst& cst = *node.cst;
  if (spells_keyword(cst, *grammar, node.id, keyword)) return {node};
  return collect<false>(node, [&](NodeId id) {
    return spells_keyword(cst, *grammar, id, keyword);
  });
}

std::optional<CstNodeView> find_node_for_keyword(const CstNodeView& node,
                                                 std::string_view keyword,
                                                 std::size_t index) {
  if (index != 0) return clamped(find_nodes_for_keyword(node, keyword), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return std::nullopt;
  const Cst& cst = *node.cst;
  if (spells_keyword(cst, *grammar, node.id, keyword)) return node;
  return first_of<false>(node, [&](NodeId id) {
    return spells_keyword(cst, *grammar, id, keyword);
  });
}

std::vector<CstNodeView> find_hidden_nodes(const CstNodeView& node,
                                           std::string_view ruleName) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || ruleName.empty()) return {};
  const Cst& cst = *node.cst;
  return collect<false>(node, [&](NodeId id) {
    const CstNode& n = cst[id];
    return n.hidden != 0 && grammar->recordRule(n) == ruleName;
  });
}

CstLeaf find_leaf_before_offset(const Cst& cst, TextOffset offset) {
  if (!cst.has_entry()) return {};
  const NodeId first = cst.entry_begin();
  // Ends never decrease, so the leaves ending at or before the caret are a
  // prefix of the index, and the answer is the last eligible one of it.
  for (NodeCount rank = cst.leaves_before(offset); rank-- > 0;) {
    const CstLeaf leaf = cst.leaf(rank);
    if (leaf.id < first) break;
    if (leaf.hidden || leaf.recovered || leaf.begin == leaf.end) continue;
    return leaf;
  }
  return {};
}

namespace {

// A leaf ending in anything but a word's scalar is complete, and a caret
// behind it asks what follows.
bool ends_a_word(std::string_view text, const CstLeaf& leaf) {
  TextOffset last = leaf.end - 1;
  while (last > leaf.begin &&
         (static_cast<unsigned char>(text[last]) & 0xC0u) == 0x80u)
    --last;
  std::size_t len = 0;
  return word_scalar(scalar_at(text.data() + last, text.size() - last, len));
}

}  // namespace

CstLeaf word_at(const Cst& cst, TextOffset offset) {
  const std::string_view text = cst.text();
  const auto word = [&](const CstLeaf& leaf) {
    return !leaf.hidden && leaf.end > leaf.begin && ends_a_word(text, leaf);
  };
  const NodeCount rank = cst.leaves_before(offset);
  // Every leaf ending at the caret sits at the top of the rank -- a repair's
  // zero-width record can stand over the word the author typed.
  for (NodeCount r = rank; r > 0; --r) {
    const CstLeaf before = cst.leaf(r - 1);
    if (before.end != offset) break;
    if (word(before)) return before;
  }
  // `<=`: a caret at a word's own start writes in that word's place. Asked at
  // the caret alone it would answer nothing but punctuation -- every spelling
  // ending in a letter lexes into the word behind it.
  if (rank < cst.leaf_count())
    if (const CstLeaf at = cst.leaf(rank); at.begin <= offset && word(at))
      return at;
  return {};
}

std::vector<NodeId> interior_nodes(const Cst& cst, NodeId open, NodeId close) {
  std::vector<NodeId> interior;
  if (open >= cst.size() || close >= cst.size() || open == close)
    return interior;
  if (open > close) std::swap(open, close);
  NodeId parent = close + 1;
  while (parent < cst.size() && cst.subtree_begin(parent) > open) ++parent;
  if (parent == cst.size()) return interior;
  bool behindClose = false;
  for (const NodeId child : cst.reverse_children(parent)) {
    if (cst.contains(child, open)) break;
    if (cst.contains(child, close))
      behindClose = true;
    else if (behindClose)
      interior.push_back(child);
  }
  std::reverse(interior.begin(), interior.end());
  return interior;
}

AstNode* owner_of_leaf(const Cst& cst, NodeId leaf) {
  const OwnedNodes owned = cst.owned();
  const auto at = std::lower_bound(
      owned.begin(), owned.end(), leaf,
      [](const AstNode* row, NodeId want) { return row->cstNodeId < want; });
  if (at == owned.end()) return nullptr;
  for (AstNode* node = *at;
       node != nullptr && node->cstNodeId != kNoNode; node = node->parent)
    if (cst.contains(node->cstNodeId, leaf)) return node;
  return nullptr;
}

AstNode* owner_at(const Cst& cst, TextOffset offset) {
  const CstLeaf leaf = find_leaf_before_offset(cst, offset);
  return leaf ? owner_of_leaf(cst, leaf.id) : nullptr;
}

}  // namespace cursiv
