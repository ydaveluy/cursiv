#include "cursiv/lsp/ranges/DefaultSelectionRangeProvider.hpp"

#include <lsp/types.h>

#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/core/text/word_scalar.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

[[nodiscard]] bool covers(const CstNode& node, TextOffset offset) noexcept {
  return offset >= node.begin && offset < node.end;
}

// The word under the caret is the innermost chain step, answering inside a
// region the tree records as one block -- a comment above all. A word is a
// run of letters, digits and `_`, read by scalar: a dash or a quote past
// ASCII ends it as its ASCII kin would.
struct TokenSpan {
  TextOffset begin = 0;
  TextOffset end = 0;
};

// The lead of the scalar holding `at`.
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
           word_scalar(scalar_at(text.data() + lead, size - lead, len));
  };
  const auto width = [&](TextOffset lead) {
    return static_cast<TextOffset>(utf8_width(text[lead]));
  };
  TextOffset cursor = offset < size ? scalar_start(text, offset) : size;
  // The caret just past a word is still on it -- the same half-open-span rule
  // the whole storey follows.
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
// a step of any selection. Roots are walked backwards via `subtree_begin`;
// empty when no visible record covers the offset.
[[nodiscard]] std::vector<NodeId> chain_at(const Cst& cst, TextOffset offset,
                                           const CancellationToken& token) {
  std::vector<NodeId> chain;
  NodeId cursor = cst.size();
  NodeId found = kNoNode;
  while (cursor > 0) {
    throw_if_cancelled(token);
    const NodeId root = cursor - 1;
    if (covers(cst[root], offset)) {
      // Roots are disjoint: a hidden root covering the offset means no
      // visible one does.
      if (cst[root].hidden == 0) found = root;
      break;
    }
    cursor = cst.subtree_begin(root);
  }
  while (found != kNoNode) {
    chain.push_back(found);
    NodeId next = kNoNode;
    for (const NodeId child : cst.reverse_children(found)) {
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
    // is half-open, so a double-click or word-wise motion often lands one
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
    for (unsigned int& it : std::views::reverse(chain)) {
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
    // Capped: `parent` is a `unique_ptr`, so the chain tears down by recursing
    // once per step, and a client expands a handful. An outermost step short of
    // the root is what the protocol allows for.
    // Innermost first in `spans`, so the cap drops the outer steps.
    constexpr std::size_t kMaxDepth = 64;
    if (spans.size() > kMaxDepth) spans.resize(kMaxDepth);
    // Outermost first into the protocol's nesting: the value is the innermost
    // range and every `parent` is one step out.
    std::unique_ptr<lsp::SelectionRange> parent;
    for (auto& span : std::views::reverse(spans)) {
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
