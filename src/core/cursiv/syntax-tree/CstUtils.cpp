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

// The one reader of `Cst::carriesNode`, which a walk asks of every record
// it passes.
namespace detail {
struct CstWalk {
  static bool carriesNode(const Cst& cst, NodeId id) {
    return cst.carriesNode(id);
  }
};
}  // namespace detail

void Cst::indexNodes() const {
  buildOnce(_nodeIndex, [this] {
    _nodeRecords.assign((std::size_t{size()} + 63) / 64, 0);
    for (const AstNode* const node : _owned)
      if (const NodeId id = node->cstNodeId; id < size())
        _nodeRecords[id / 64] |= std::uint64_t{1} << (id % 64);
  });
}

namespace {

// Backwards, as postorder lists a parent's children. A member's writer is
// asked before the node boundary, since the span that wrote a member may open
// the node it holds; anything else after it.
template <bool kWriterFirst, class Match, class Take>
void walk(const Cst& cst, NodeId root, const Match& matches, const Take& take) {
  if (root == kNoNode || root >= cst.size()) return;
  const auto opens = [&cst](NodeId id) {
    return detail::CstWalk::carriesNode(cst, id);
  };
  const NodeId begin = cst.subtreeBegin(root);
  for (NodeId cursor = root; cursor > begin;) {
    const NodeId child = cursor - 1;
    const NodeId past = cst.subtreeBegin(child);
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
      // when this record is childless.
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
CstNodeView first_of(const CstNodeView& node, const Match& matches) {
  CstNodeView first;
  const Cst& cst = *node.cst;
  walk<kWriterFirst>(cst, node.id, matches,
                     [&](NodeId id) { first = CstNodeView{&cst, id}; });
  return first;
}

CstNodeView clamped(const std::vector<CstNodeView>& found, std::size_t index) {
  if (found.empty()) return {};
  return found[std::min(index, found.size() - 1)];
}

// A visible leaf that spells the keyword and names no rule. A leaf that
// differs in case alone counts where the grammar reads the literal in any
// case.
class SpellsKeyword {
 public:
  SpellsKeyword(const Cst& cst, const Grammar& grammar,
                std::string_view keyword) noexcept
      : _cst(&cst), _grammar(&grammar), _keyword(keyword) {}

  bool operator()(NodeId id) const noexcept {
    const CstNode& n = (*_cst)[id];
    if (n.subtreeSize != 1 || n.hidden != 0) return false;
    if (const std::string_view text = _cst->spelling(id);
        text != _keyword && !(folded_equal(text, _keyword) && folds()))
      return false;
    return _grammar->recordRule(n).empty();
  }

 private:
  bool folds() const noexcept {
    if (!_folds.has_value()) _folds = _grammar->spellingFolds(_keyword);
    return *_folds;
  }

  const Cst* _cst;
  const Grammar* _grammar;
  std::string_view _keyword;
  mutable std::optional<bool> _folds;
};

std::vector<CstNodeView> writers_of(const CstNodeView& node,
                                    std::string_view feature) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || feature.empty()) return {};
  const Cst& cst = *node.cst;
  std::vector<CstNodeView> found = collect<true>(
      node, [&](NodeId id) { return grammar->recordWrites(cst[id], feature); });
  // The node's own record answers first: a rule whose match is one element
  // writes no record of its own, so the span that wrote the member opened it.
  if (grammar->recordFills(cst[node.id], feature))
    found.insert(found.begin(), node);
  return found;
}

CstNodeView writer_of(const CstNodeView& node, std::string_view feature,
                      std::size_t index) {
  if (index != 0) return clamped(writers_of(node, feature), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || feature.empty()) return {};
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

CstNodeView spelling_of(const CstNodeView& node, std::string_view keyword,
                        std::size_t index) {
  if (index != 0) return clamped(spellings_of(node, keyword), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return {};
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

CstNodeView cst_node_of(const AstNode& node, const Document* document) {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return {};
  const Cst& cst = document->parseResult.cst();
  if (node.cstNodeId >= cst.size()) return {};
  return CstNodeView{&cst, node.cstNodeId};
}

std::vector<CstNodeView> find_nodes_for_property(const AstNode& node,
                                                 std::string_view feature,
                                                 const Document* document) {
  return writers_of(cst_node_of(node, document), feature);
}

CstNodeView find_node_for_property(const AstNode& node,
                                   std::string_view feature, std::size_t index,
                                   const Document* document) {
  return writer_of(cst_node_of(node, document), feature, index);
}

std::vector<CstNodeView> find_nodes_for_keyword(const AstNode& node,
                                                std::string_view keyword,
                                                const Document* document) {
  return spellings_of(cst_node_of(node, document), keyword);
}

CstNodeView find_node_for_keyword(const AstNode& node, std::string_view keyword,
                                  std::size_t index, const Document* document) {
  return spelling_of(cst_node_of(node, document), keyword, index);
}

std::vector<CstNodeView> find_hidden_nodes(const AstNode& node,
                                           std::string_view ruleName,
                                           const Document* document) {
  return hidden_of(cst_node_of(node, document), ruleName);
}

CstLeaf find_leaf_before_offset(const Cst& cst, TextOffset offset) {
  if (!cst.hasEntry()) return {};
  const NodeId first = cst.entryBegin();
  // Ends never decrease, so the leaves ending at or before the caret come
  // before the first one ending past it, and the answer is the last eligible.
  for (NodeId at = cst.leafAfter(offset); at > 0;) {
    const CstLeaf leaf = cst.asLeaf(at = cst.leafBefore(at));
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
  return detail::word_scalar(
      scalar_at(text.data() + last, text.size() - last, len));
}

}  // namespace

CstLeaf detail::word_at(const Cst& cst, TextOffset offset) {
  const std::string_view text = cst.text();
  const auto word = [&](const CstLeaf& leaf) {
    return !leaf.hidden && leaf.end > leaf.begin && ends_a_word(text, leaf);
  };
  const NodeId after = cst.leafAfter(offset);
  // Every leaf ending at the caret comes just before the first one ending past
  // it -- a repair's zero-width record can stand over the word the author
  // typed.
  for (NodeId r = after; r > 0;) {
    const CstLeaf before = cst.asLeaf(r = cst.leafBefore(r));
    if (before.end != offset) break;
    if (word(before)) return before;
  }
  // `<=`: a caret at a word's own start writes in that word's place. Asked at
  // the caret alone it would answer nothing but punctuation -- every spelling
  // ending in a letter lexes into the word behind it.
  if (after < cst.size())
    if (const CstLeaf at = cst.asLeaf(after); at.begin <= offset && word(at))
      return at;
  return {};
}

std::vector<NodeId> interior_nodes(const Cst& cst, NodeId open, NodeId close) {
  std::vector<NodeId> interior;
  if (open >= cst.size() || close >= cst.size() || open == close)
    return interior;
  if (open > close) std::swap(open, close);
  NodeId parent = close + 1;
  while (parent < cst.size() && cst.subtreeBegin(parent) > open) ++parent;
  if (parent == cst.size()) return interior;
  bool behindClose = false;
  for (const NodeId child : cst.reverseChildren(parent)) {
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
