#include "cursiv/syntax-tree/CstUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/word_scalar.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// The one reader of `Cst::carries_node`, which a walk asks of every record
// it passes.
namespace detail {
struct CstWalk {
  static bool carries_node(const Cst& cst, NodeId id) {
    return cst.carries_node(id);
  }
};
}  // namespace detail

void Cst::index_nodes() const {
  build_once(_nodeIndex, [this] {
    _nodeRecords.assign((std::size_t{size()} + 63) / 64, 0);
    for (const AstNode* const node : _owned)
      if (const NodeId id = node->cstNodeId; id < size())
        _nodeRecords[id / 64] |= std::uint64_t{1} << (id % 64);
  });
}

namespace {

// Backwards, which is how the run reads: postorder gives a parent's children
// in reverse, so the walk follows them there and a list is reversed once.
// A member's writer is asked before the boundary -- the span that wrote a
// member may open the node it holds -- and anything else after it: a keyword
// that is a nested node's only record is that node's. A childless record is
// passed the same way whether it opens a node or not, so that is asked of it
// only where it matches.
template <bool kWriterFirst, class Match, class Take>
void walk(const Cst& cst, NodeId root, const Match& matches, const Take& take) {
  if (root == kNoNode || root >= cst.size()) return;
  const auto opens = [&cst](NodeId id) {
    return detail::CstWalk::carries_node(cst, id);
  };
  const NodeId begin = cst.subtree_begin(root);
  for (NodeId cursor = root; cursor > begin;) {
    const NodeId child = cursor - 1;
    const NodeId past = cst.subtree_begin(child);
    const bool childless = past == child;
    if constexpr (!kWriterFirst) {
      if (!childless && opens(child)) {
        cursor = past;
        continue;
      }
    }
    if (matches(child) && (kWriterFirst || !childless || !opens(child))) {
      take(child);
      cursor = past;
    } else if (kWriterFirst && !childless && opens(child)) {
      cursor = past;
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
  std::ranges::reverse(found);
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

// A visible leaf that spells the keyword and names no rule: a literal the
// grammar spelled. A leaf that differs from it in case alone spells it
// where the grammar reads the literal in any case, which is asked once, at the
// first such leaf. The rule is asked last, of those leaves only.
class SpellsKeyword {
 public:
  SpellsKeyword(const Cst& cst, const Grammar& grammar,
                std::string_view keyword) noexcept
      : _cst(&cst), _grammar(&grammar), _keyword(keyword) {}

  bool operator()(NodeId id) const noexcept {
    const CstNode& n = (*_cst)[id];
    if (n.subtreeSize != 1 || n.hidden != 0) return false;
    const std::string_view text = _cst->spelling(id);
    if (text != _keyword && !(folded_equal(text, _keyword) && folds()))
      return false;
    return _grammar->recordRule(n).empty();
  }

 private:
  bool folds() const noexcept {
    if (!_folds) _folds = _grammar->spellingFolds(_keyword);
    return *_folds;
  }

  const Cst* _cst;
  const Grammar* _grammar;
  std::string_view _keyword;
  mutable std::optional<bool> _folds;
};

// The record `node` was built from, in its document's tree: where a search
// starts. Nothing for a node no document holds, or one whose record that tree
// does not have.
CstNodeView record_of(const AstNode& node, const Document* document) {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return {};
  const Cst& cst = document->parseResult.cst();
  if (node.cstNodeId >= cst.size()) return {};
  return CstNodeView{&cst, node.cstNodeId};
}

std::vector<CstNodeView> writers_of(const CstNodeView& node,
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

std::optional<CstNodeView> writer_of(const CstNodeView& node,
                                     std::string_view feature,
                                     std::size_t index) {
  if (index != 0) return clamped(writers_of(node, feature), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || feature.empty()) return std::nullopt;
  const Cst& cst = *node.cst;
  if (grammar->recordFills(cst[node.id], feature)) return node;
  return first_of<true>(
      node, [&](NodeId id) { return grammar->recordWrites(cst[id], feature); });
}

std::vector<CstNodeView> spellings_of(const CstNodeView& node,
                                      std::string_view keyword) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return {};
  const SpellsKeyword spells(*node.cst, *grammar, keyword);
  if (spells(node.id)) return {node};
  return collect<false>(node, spells);
}

std::optional<CstNodeView> spelling_of(const CstNodeView& node,
                                       std::string_view keyword,
                                       std::size_t index) {
  if (index != 0) return clamped(spellings_of(node, keyword), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return std::nullopt;
  const SpellsKeyword spells(*node.cst, *grammar, keyword);
  if (spells(node.id)) return node;
  return first_of<false>(node, spells);
}

std::vector<CstNodeView> hidden_of(const CstNodeView& node,
                                   std::string_view ruleName) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || ruleName.empty()) return {};
  const Cst& cst = *node.cst;
  return collect<false>(node, [&](NodeId id) {
    const CstNode& n = cst[id];
    return n.hidden != 0 && grammar->recordRule(n) == ruleName;
  });
}

}  // namespace

std::vector<CstNodeView> find_nodes_for_feature(const AstNode& node,
                                                std::string_view feature,
                                                const Document* document) {
  return writers_of(record_of(node, document), feature);
}

std::optional<CstNodeView> find_node_for_feature(const AstNode& node,
                                                 std::string_view feature,
                                                 std::size_t index,
                                                 const Document* document) {
  return writer_of(record_of(node, document), feature, index);
}

std::vector<CstNodeView> find_nodes_for_keyword(const AstNode& node,
                                                std::string_view keyword,
                                                const Document* document) {
  return spellings_of(record_of(node, document), keyword);
}

std::optional<CstNodeView> find_node_for_keyword(const AstNode& node,
                                                 std::string_view keyword,
                                                 std::size_t index,
                                                 const Document* document) {
  return spelling_of(record_of(node, document), keyword, index);
}

std::vector<CstNodeView> find_hidden_nodes(const AstNode& node,
                                           std::string_view ruleName,
                                           const Document* document) {
  return hidden_of(record_of(node, document), ruleName);
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

CstLeaf detail::word_at(const Cst& cst, TextOffset offset) {
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
  std::ranges::reverse(interior);
  return interior;
}

AstNode* owner_of_leaf(const Cst& cst, NodeId leaf) {
  const OwnedNodes owned = cst.owned();
  const auto at =
      std::ranges::lower_bound(owned, leaf, {}, &AstNode::cstNodeId);
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
