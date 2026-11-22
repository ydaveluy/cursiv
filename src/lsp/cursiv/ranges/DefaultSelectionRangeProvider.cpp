#include "cursiv/ranges/DefaultSelectionRangeProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/word_scalar.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/Utf8.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

namespace {

[[nodiscard]] bool covers(const CstNode& node, TextOffset offset) noexcept {
  return offset >= node.begin && offset < node.end;
}

// The word under the caret is the innermost step, inside a region the tree
// keeps as one block, such as a comment. A word is a run of letters, digits
// and `_`.
struct TokenSpan {
  TextOffset begin = 0;
  TextOffset end = 0;
};

[[nodiscard]] TokenSpan token_at(std::string_view text, TextOffset offset) {
  const auto size = static_cast<TextOffset>(text.size());
  const auto is = [&](TextOffset lead) {
    std::size_t len = 0;
    return lead < size &&
           detail::word_scalar(scalarAt(text.data() + lead, size - lead, len));
  };
  const auto width = [&](TextOffset lead) {
    return static_cast<TextOffset>(utf8Width(text[lead]));
  };
  TextOffset cursor = offset < size ? detail::scalar_start(text, offset) : size;
  // A caret just past a word is still on it.
  if (!is(cursor) && cursor > 0 && is(detail::scalar_start(text, cursor - 1)))
    cursor = detail::scalar_start(text, cursor - 1);
  if (!is(cursor)) return {};
  TextOffset begin = cursor;
  while (begin > 0 && is(detail::scalar_start(text, begin - 1)))
    begin = detail::scalar_start(text, begin - 1);
  TextOffset end = cursor + width(cursor);
  while (is(end)) end += width(end);
  return {begin, end};
}

// The children of each record a request reaches, in document order, listed
// once however many carets step through it.
class Children {
 public:
  explicit Children(const Cst& cst) : _cst(cst) {}

  std::span<const NodeId> of(NodeId id, const CancellationToken& token) {
    const auto [at, fresh] = _of.try_emplace(id);
    if (fresh) {
      for (const NodeId child : _cst.reverseChildren(id)) {
        throwIfCancelled(token);
        at->second.push_back(child);
      }
      std::ranges::reverse(at->second);
    }
    return at->second;
  }

 private:
  const Cst& _cst;
  std::unordered_map<NodeId, std::vector<NodeId>> _of;
};

// The chain, outermost first, hidden records excluded -- a comment is not
// a step of any selection. Roots are walked backwards via `subtreeBegin`;
// empty when no visible record covers the offset.
[[nodiscard]] std::vector<NodeId> chain_at(const Cst& cst, Children& children,
                                           TextOffset offset,
                                           const CancellationToken& token) {
  std::vector<NodeId> chain;
  NodeId found = kNoNode;
  for (NodeId cursor = cst.size(); cursor > 0;) {
    throwIfCancelled(token);
    const NodeId root = cursor - 1;
    if (covers(cst[root], offset)) {
      // Roots are disjoint: a hidden root covering the offset means no
      // visible one does.
      if (!cst[root].isHidden()) found = root;
      break;
    }
    cursor = cst.subtreeBegin(root);
  }
  while (found != kNoNode) {
    chain.push_back(found);
    // Siblings never overlap: only the last one starting at or before the
    // offset can cover it.
    const std::span<const NodeId> under = children.of(found, token);
    const auto after = std::ranges::upper_bound(
        under, offset, {}, [&](NodeId child) { return cst[child].begin; });
    found = kNoNode;
    if (after != under.begin())
      if (const NodeId child = *(after - 1);
          !cst[child].isHidden() && covers(cst[child], offset))
        found = child;
  }
  return chain;
}

}  // namespace

std::vector<lsp::SelectionRange>
DefaultSelectionRangeProvider::getSelectionRanges(
    const Document& document, std::span<const lsp::Position> positions,
    const CancellationToken& token) const {
  throwIfCancelled(token);
  std::vector<lsp::SelectionRange> answer;
  const Cst& cst = document.parseResult.cst();

  answer.reserve(positions.size());
  Children children(cst);
  for (const lsp::Position& position : positions) {
    throwIfCancelled(token);
    const TextOffset offset =
        document.textDocument().offsetAt(fromLsp(position));
    // The chain is read where the word under the caret starts, so a caret
    // just past a word also reaches every record the word ends. Off a word,
    // one retry one byte back when nothing covers the caret.
    const TokenSpan word = token_at(document.textDocument().getText(), offset);
    const bool onWord = word.end > word.begin;
    std::vector<NodeId> chain =
        chain_at(cst, children, onWord ? word.begin : offset, token);
    if (chain.empty() && !onWord && offset > 0)
      chain = chain_at(cst, children, offset - 1, token);

    // Innermost first: the word seeds the chain, then each record must
    // contain what it grows, since the protocol requires every step's
    // parent to cover it. A record adding no visible bytes is said once.
    std::vector<TextRange> spans;
    if (onWord) spans.push_back(TextRange{word.begin, word.end});
    for (const unsigned int it : std::views::reverse(chain)) {
      const CstNode& node = cst[it];
      if (!spans.empty()) {
        const TextRange& last = spans.back();
        if (node.begin > last.begin || node.end < last.end) continue;
        if (node.begin == last.begin && node.end == last.end) continue;
      }
      spans.push_back(TextRange{node.begin, node.end});
    }

    // A position nothing answers still gets an entry, the empty range at the
    // caret: the reply is read positionally, so an omission would shift every
    // cursor after it onto another cursor's answer.
    if (spans.empty()) {
      lsp::SelectionRange bare;
      bare.range = toLsp(document.textDocument().rangeAt(offset, offset));
      answer.push_back(std::move(bare));
      continue;
    }
    // Capped: `parent` is a `unique_ptr`, so the chain is torn down by
    // recursion. `spans` is innermost first, so the cap drops the outer steps.
    if (constexpr std::size_t kMaxDepth = 64; spans.size() > kMaxDepth)
      spans.resize(kMaxDepth);
    // Outermost first into the protocol's nesting: the value is the innermost
    // range and every `parent` is one step out.
    std::unique_ptr<lsp::SelectionRange> parent;
    for (const auto& span : std::views::reverse(spans)) {
      auto next = std::make_unique<lsp::SelectionRange>();
      next->range =
          toLsp(document.textDocument().rangeAt(span.begin, span.end));
      next->parent = std::move(parent);
      parent = std::move(next);
    }
    answer.push_back(std::move(*parent));
  }
  return answer;
}

}  // namespace cursiv
