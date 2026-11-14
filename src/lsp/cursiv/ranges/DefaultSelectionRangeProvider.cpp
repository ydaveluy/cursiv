#include "cursiv/ranges/DefaultSelectionRangeProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
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

// The first byte of the code point holding `at`.
[[nodiscard]] TextOffset scalar_start(std::string_view text,
                                      TextOffset at) noexcept {
  while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u)
    --at;
  return at;
}

[[nodiscard]] TokenSpan token_at(std::string_view text, TextOffset offset) {
  const auto size = static_cast<TextOffset>(text.size());
  const auto is = [&](TextOffset lead) {
    std::size_t len = 0;
    return lead < size &&
           detail::word_scalar(scalar_at(text.data() + lead, size - lead, len));
  };
  const auto width = [&](TextOffset lead) {
    return static_cast<TextOffset>(utf8_width(text[lead]));
  };
  TextOffset cursor = offset < size ? scalar_start(text, offset) : size;
  // A caret just past a word is still on it.
  if (!is(cursor) && cursor > 0 && is(scalar_start(text, cursor - 1)))
    cursor = scalar_start(text, cursor - 1);
  if (!is(cursor)) return {};
  TextOffset begin = cursor;
  while (begin > 0 && is(scalar_start(text, begin - 1)))
    begin = scalar_start(text, begin - 1);
  TextOffset end = cursor + width(cursor);
  while (is(end)) end += width(end);
  return {begin, end};
}

// The chain, outermost first, hidden records excluded -- a comment is not
// a step of any selection. The first leaf ending past the offset names the
// innermost node covering it: its owners give the outer steps, the tree below
// it the inner ones. Without one, the roots are walked backwards via
// `subtreeBegin`. Empty when no visible record covers the offset.
[[nodiscard]] std::vector<NodeId> chain_at(const Cst& cst, TextOffset offset,
                                           const CancellationToken& token) {
  std::vector<NodeId> chain;
  const AstNode* owner = nullptr;
  if (const NodeId after = cst.leafAfter(offset); after < cst.size()) {
    owner = owner_of_leaf(cst, after);
    while (owner != nullptr && owner->cstNodeId != kNoNode &&
           !covers(cst[owner->cstNodeId], offset))
      owner = owner->parent;
  }
  NodeId found = kNoNode;
  if (owner != nullptr && owner->cstNodeId != kNoNode) {
    for (const AstNode* outer = owner->parent;
         outer != nullptr && outer->cstNodeId != kNoNode; outer = outer->parent)
      chain.push_back(outer->cstNodeId);
    std::ranges::reverse(chain);
    found = owner->cstNodeId;
  } else {
    for (NodeId cursor = cst.size(); cursor > 0;) {
      throw_if_cancelled(token);
      const NodeId root = cursor - 1;
      if (covers(cst[root], offset)) {
        // Roots are disjoint: a hidden root covering the offset means no
        // visible one does.
        if (cst[root].hidden == 0) found = root;
        break;
      }
      cursor = cst.subtreeBegin(root);
    }
  }
  while (found != kNoNode) {
    chain.push_back(found);
    NodeId next = kNoNode;
    for (const NodeId child : cst.reverseChildren(found)) {
      throw_if_cancelled(token);
      if (cst[child].hidden == 0 && covers(cst[child], offset)) {
        next = child;
        break;
      }
    }
    found = next;
  }
  return chain;
}

}  // namespace

std::vector<lsp::SelectionRange>
DefaultSelectionRangeProvider::getSelectionRanges(
    const Document& document, std::span<const lsp::Position> positions,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  std::vector<lsp::SelectionRange> answer;
  const Cst& cst = document.parseResult.cst();

  answer.reserve(positions.size());
  for (const lsp::Position& position : positions) {
    throw_if_cancelled(token);
    const TextOffset offset =
        document.textDocument().offsetAt(from_lsp(position));
    std::vector<NodeId> chain = chain_at(cst, offset, token);
    // A caret one past the last character is still on the word: a CST span
    // is half-open, so a double-click or word-wise motion often falls one
    // past the name. One retry, only when nothing covered the offset.
    if (chain.empty() && offset > 0) chain = chain_at(cst, offset - 1, token);

    // Innermost first: the word seeds the chain, then each record must
    // contain what it grows, since the protocol requires every step's
    // parent to cover it. A record adding no visible bytes is said once.
    std::vector<TextRange> spans;
    if (const TokenSpan word =
            token_at(document.textDocument().getText(), offset);
        word.end > word.begin)
      spans.push_back(TextRange{word.begin, word.end});
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
      bare.range = to_lsp(document.textDocument().rangeAt(offset, offset));
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
          to_lsp(document.textDocument().rangeAt(span.begin, span.end));
      next->parent = std::move(parent);
      parent = std::move(next);
    }
    answer.push_back(std::move(*parent));
  }
  return answer;
}

}  // namespace cursiv
