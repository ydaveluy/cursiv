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
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/word_scalar.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

void Cst::indexNodes() const {
  // A second reader waits here instead of building the index too.
  const std::lock_guard lock(_indexLock);
  if (published(_nodeIndex)) return;
  _nodeRecords.assign((std::size_t{size()} + 63) / 64, 0);
  for (const AstNode* const node : _owned)
    if (const NodeId id = node->cstNodeId; id < size())
      _nodeRecords[id / 64] |= std::uint64_t{1} << (id % 64);
  publish(_nodeIndex);
}

namespace detail {

// Backwards, as postorder lists a parent's children. A member's writer is
// asked before the node boundary, since the span that wrote a member may open
// the node it holds; anything else after it.
template <bool kWriterFirst, class Match, class Take>
void walk_cst(const Cst& cst, NodeId root, const Match& matches,
              const Take& take) {
  if (root == kNoNode || root >= cst.size()) return;
  const NodeId begin = cst.subtreeBegin(root);
  if (root <= begin) return;
  const auto* const records = cst.nodeRecords();
  const auto opens = [records](NodeId id) {
    return ((records[id / 64] >> (id % 64)) & 1U) != 0;
  };
  for (NodeId cursor = root; cursor > begin;) {
    const NodeId child = cursor - 1;
    const NodeId past = cst.subtreeBegin(child);
    const bool boundary = opens(child);
    if ((kWriterFirst || !boundary) && matches(child)) {
      take(child);
      cursor = past;
    } else {
      cursor = boundary ? past : child;
    }
  }
}

}  // namespace detail

namespace {

const Grammar* grammar_of(const CstNodeView& node) noexcept {
  return node ? node.cst->grammar() : nullptr;
}

template <class Match>
std::vector<CstNodeView> collect(const CstNodeView& node,
                                 const Match& matches) {
  std::vector<CstNodeView> found;
  const Cst& cst = *node.cst;
  detail::walk_cst<false>(cst, node.id, matches, [&](NodeId id) {
    found.push_back(CstNodeView{&cst, id});
  });
  std::ranges::reverse(found);
  return found;
}

// The last match the reverse walk reaches is the first in document order.
template <class Match>
CstNodeView first_of(const CstNodeView& node, const Match& matches) {
  CstNodeView first;
  const Cst& cst = *node.cst;
  detail::walk_cst<false>(cst, node.id, matches,
                                 [&](NodeId id) { first = CstNodeView{&cst, id}; });
  return first;
}

CstNodeView clamped(const std::vector<CstNodeView>& found, std::size_t index) {
  if (found.empty()) return {};
  return found[std::min(index, found.size() - 1)];
}

// A visible literal leaf that spells the keyword. A leaf that differs in case
// alone counts where the grammar reads the literal in any case.
class SpellsKeyword {
 public:
  SpellsKeyword(const Cst& cst, const Grammar& grammar,
                std::string_view keyword) noexcept
      : _cst(&cst), _grammar(&grammar), _keyword(keyword) {}

  bool operator()(NodeId id) const noexcept {
    const CstNode& n = (*_cst)[id];
    if (n.subtreeSize != 1 || n.isHidden()) return false;
    if (const std::string_view text = _cst->spelling(id);
        text != _keyword && !(foldedEqual(text, _keyword) && folds()))
      return false;
    return _grammar->recordIsLiteral(n);
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

// Reverse document order except where an adoption moved an enclosing write to a later record.
template <bool kFirst, class Take>
bool property_sites(const AstNode& owner, const Document* document,
                    std::string_view feature, const Take& take) {
  if (document == nullptr) document = owner.getDocument();
  if (document == nullptr || feature.empty()) return false;
  const CstNodeView node = cstNodeOf(owner, document);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr) return false;
  const Cst& cst = *node.cst;
  auto origins = document->parseResult.propertyOrigins();
  const auto origin = [&](NodeId id) -> const AstNode* {
    if (!origins.empty() && origins.back().record > id) {
      const auto end = std::ranges::upper_bound(origins, id, {}, &ParseResult::PropertyOrigin::record);
      origins = {origins.begin(), end};
    }
    if (origins.empty() || origins.back().record != id) return nullptr;
    const AstNode* previous = origins.back().owner;
    origins = origins.first(origins.size() - 1);
    return previous;
  };
  std::size_t depth = origin(node.id) != nullptr;
  for (const AstNode* p = owner.parent; p && p->cstNodeId == node.id; p = p->parent) ++depth;
  const auto sites = grammar->propertySites(cst[node.id], feature, depth, owner);
  const auto emit = [&](NodeId id, const Grammar::PropertySites& writes) {
    if (const auto* child = writes.nested; child && child->parent == &owner &&
        child->cstNodeId < cst.size() && cst.contains(id, child->cstNodeId)) {
      const auto candidates =
          std::ranges::equal_range(cst.owned(), child->cstNodeId, {}, &AstNode::cstNodeId);
      if (std::ranges::find(candidates, child) != candidates.end())
        take(CstNodeView{&cst, child->cstNodeId});
    }
    if (writes.here) take(CstNodeView{&cst, id});
  };
  if constexpr (kFirst)
    if (sites.here) {
      take(node);
      return false;
    }
  bool adopted = false;
  if (sites.below)
    detail::walk_cst<true>(cst, node.id, [&](NodeId id) {
      if (const AstNode* previous = origin(id)) {
        if (previous != &owner) return true;
        const auto was = grammar->propertySites(cst[id], feature, 0, owner);
        emit(id, was);
        adopted = adopted || was.here || was.nested != nullptr;
        return !was.below;
      }
      if (!grammar->recordWrites(cst[id], feature)) return false;
      take(CstNodeView{&cst, id});
      return true;
    }, [](NodeId) {});
  emit(node.id, sites);
  return adopted;
}

bool before(CstNodeView a, CstNodeView b) {
  const NodeId first = a.cst->subtreeBegin(a.id), second = b.cst->subtreeBegin(b.id);
  return first < second || (first == second && a.id > b.id);
}

std::vector<CstNodeView> spellings_of(const CstNodeView& node,
                                      std::string_view keyword) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return {};
  const SpellsKeyword spells(*node.cst, *grammar, keyword);
  if (spells(node.id)) return {node};
  return collect(node, spells);
}

CstNodeView spelling_of(const CstNodeView& node, std::string_view keyword,
                        std::size_t index) {
  if (index != 0) return clamped(spellings_of(node, keyword), index);
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || keyword.empty()) return {};
  const SpellsKeyword spells(*node.cst, *grammar, keyword);
  if (spells(node.id)) return node;
  return first_of(node, spells);
}

std::vector<CstNodeView> hidden_of(const CstNodeView& node,
                                   std::string_view ruleName) {
  const Grammar* const grammar = grammar_of(node);
  if (grammar == nullptr || ruleName.empty()) return {};
  const Cst& cst = *node.cst;
  return collect(node, [&](NodeId id) {
    const CstNode& n = cst[id];
    return n.isHidden() && grammar->recordRule(n) == ruleName;
  });
}

}  // namespace

CstNodeView cstNodeOf(const AstNode& node, const Document* document) {
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return {};
  const Cst& cst = document->parseResult.cst();
  if (node.cstNodeId >= cst.size()) return {};
  return CstNodeView{&cst, node.cstNodeId};
}

std::vector<CstNodeView> findNodesForProperty(const AstNode& node,
                                                 std::string_view feature,
                                                 const Document* document) {
  std::vector<CstNodeView> found;
  if (property_sites<false>(node, document, feature, [&](CstNodeView site) { found.push_back(site); }))
    std::ranges::sort(found, before);
  else
    std::ranges::reverse(found);
  return found;
}

CstNodeView findNodeForProperty(const AstNode& node,
                                   std::string_view feature, std::size_t index,
                                   const Document* document) {
  if (index != 0) return clamped(findNodesForProperty(node, feature, document), index);
  CstNodeView first;
  property_sites<true>(node, document, feature, [&](CstNodeView site) {
    if (!first || before(site, first)) first = site;
  });
  return first;
}

std::vector<CstNodeView> findNodesForKeyword(const AstNode& node,
                                                std::string_view keyword,
                                                const Document* document) {
  return spellings_of(cstNodeOf(node, document), keyword);
}

CstNodeView findNodeForKeyword(const AstNode& node, std::string_view keyword,
                                  std::size_t index, const Document* document) {
  return spelling_of(cstNodeOf(node, document), keyword, index);
}

std::vector<CstNodeView> findHiddenNodes(const AstNode& node,
                                           std::string_view ruleName,
                                           const Document* document) {
  return hidden_of(cstNodeOf(node, document), ruleName);
}

CstLeaf findLeafBeforeOffset(const Cst& cst, TextOffset offset) {
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

CstLeaf detail::word_at(const Cst& cst, TextOffset offset) {
  // A spelling ending in anything but a whole word scalar is complete, and a
  // caret behind it asks what follows.
  const auto word = [&](const CstLeaf& leaf) {
    return !leaf.hidden &&
           word_scalar(last_scalar(read_text(cst, leaf.begin, leaf.end)));
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

std::string_view detail::read_text(const Cst& cst, TextOffset begin,
                                   TextOffset end) {
  const SourceTranslation* const translation = cst.translation();
  if (translation == nullptr) return cst.text().substr(begin, end - begin);
  const std::optional<TextOffset> from = translation->translated(begin);
  const std::optional<TextOffset> to = translation->translated(end);
  if (!from || !to || *to < *from) return {};
  return translation->text().substr(*from, *to - *from);
}

std::vector<NodeId> interiorNodes(const AstNode& node, NodeId open,
                                   NodeId close, const Document* document) {
  std::vector<NodeId> interior;
  if (document == nullptr) document = node.getDocument();
  if (document == nullptr) return interior;
  const Cst& cst = document->parseResult.cst();
  if (open >= cst.size() || close >= cst.size() || open == close)
    return interior;
  if (open > close) std::swap(open, close);
  const auto holds = [&cst, open, close](NodeId id) {
    return id < cst.size() && close < id && cst.subtreeBegin(id) <= open;
  };
  const AstNode* from = &node;
  while (from != nullptr && !holds(from->cstNodeId)) from = from->parent;
  if (from == nullptr) return interior;
  // Down through the child holding `close` while it holds `open` too; the
  // children after it are passed over whole.
  NodeId parent = from->cstNodeId;
  for (bool deeper = true; deeper;) {
    deeper = false;
    for (const NodeId child : cst.reverseChildren(parent)) {
      if (cst.subtreeBegin(child) > close) continue;
      deeper = holds(child);
      if (deeper) parent = child;
      break;
    }
  }
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

AstNode* ownerOfLeaf(const Cst& cst, NodeId leaf) {
  const OwnedNodes owned = cst.owned();
  const auto at =
      std::ranges::lower_bound(owned, leaf, {}, &AstNode::cstNodeId);
  if (at == owned.end()) return nullptr;
  for (AstNode* node = *at;
       node != nullptr && node->cstNodeId != kNoNode; node = node->parent)
    if (cst.contains(node->cstNodeId, leaf)) return node;
  return nullptr;
}

AstNode* ownerAt(const Cst& cst, TextOffset offset) {
  const CstLeaf leaf = findLeafBeforeOffset(cst, offset);
  return leaf ? ownerOfLeaf(cst, leaf.id) : nullptr;
}

}  // namespace cursiv
