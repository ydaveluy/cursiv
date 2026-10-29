#include "cursiv/lsp/semantic/DefaultSemanticTokenProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/symbols/NamedNodes.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

// `named` is the tie-break when a name node and the leaf inside it cover the
// same bytes, so only one span survives.
struct Token {
  TextOffset begin;
  TextOffset end;
  SemanticTokenType type;
  bool named;
};

// The five wire integers: `deltaLine` from the previous token's line,
// `deltaStart` from its column but only on the same line. Callers must
// present tokens in source order, never overlapping.
void encode(std::vector<lsp::Uint>& data, lsp::Uint& prevLine,
            lsp::Uint& prevChar, lsp::Uint line, lsp::Uint start,
            lsp::Uint stop, SemanticTokenType type) {
  if (stop <= start) return;  // a zero-width token colours nothing
  const lsp::Uint deltaLine = line - prevLine;
  data.push_back(deltaLine);
  data.push_back(deltaLine == 0 ? start - prevChar : start);
  data.push_back(stop - start);
  data.push_back(static_cast<lsp::Uint>(type));
  // No modifiers: the legend declares none, so the bitset is zero for every
  // token rather than sometimes a bit nobody named.
  data.push_back(0);
  prevLine = line;
  prevChar = start;
}

// The protocol has no token that crosses a line: one line number and one
// column length per token. A multi-line span -- a block comment -- is cut at
// each line end and emitted as one token per line.
void emit(std::vector<lsp::Uint>& data, TextDocument::Cursor& cursor,
          lsp::Uint& prevLine, lsp::Uint& prevChar, const Token& token) {
  Position from = cursor.positionAt(token.begin);
  while (cursor.crossesLine(token.end)) {
    const Position end = cursor.lineEnd();
    encode(data, prevLine, prevChar, from.line, from.character, end.character,
           token.type);
    from = cursor.nextLine();
  }
  const Position to = cursor.positionAt(token.end);
  encode(data, prevLine, prevChar, from.line, from.character, to.character,
         token.type);
}

// The CST names a terminal but not what it lexes, so only the one thing every
// grammar spells the same way -- the match's first character -- decides
// string vs number here.
[[nodiscard]] bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] bool literal_token(std::string_view text,
                                 SemanticTokenType& type) {
  if (text.empty()) return false;
  const char head = text.front();
  if (head == '"' || head == '\'') {
    type = SemanticTokenType::String;
    return true;
  }
  const bool signedNumber = (head == '-' || head == '+' || head == '.') &&
                            text.size() > 1 && is_digit(text[1]);
  if (is_digit(head) || signedNumber) {
    type = SemanticTokenType::Number;
    return true;
  }
  return false;
}

}  // namespace

lsp::SemanticTokens DefaultSemanticTokenProvider::semanticHighlight(
    const Document& document, const CancellationToken& token) const {
  const TextDocument& text = document.textDocument();
  return highlight(document, 0, static_cast<TextOffset>(text.getText().size()),
                   token);
}

lsp::SemanticTokens DefaultSemanticTokenProvider::semanticHighlightRange(
    const Document& document, lsp::Range range,
    const CancellationToken& token) const {
  const TextDocument& text = document.textDocument();
  return highlight(document, text.offsetAt(from_lsp(range.start)),
                   text.offsetAt(from_lsp(range.end)), token);
}

lsp::SemanticTokens DefaultSemanticTokenProvider::highlight(
    const Document& document, TextOffset begin, TextOffset end,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  lsp::SemanticTokens answer;
  if (!document.hasAst()) return answer;

  const ParseResult& parsed = document.parseResult;
  const Cst& cst = parsed.cst();
  // An element id means something only through the grammar model.
  const Grammar& grammar = _core.parser->getGrammar();
  std::vector<Token> tokens;
  // A token touches the window when it neither ends before it nor starts
  // after it; one cut by the edge is coloured whole.
  const auto touches = [&](TextOffset from, TextOffset to) {
    return from < end && to > begin;
  };

  // 1. Declarations and their owners, nested as the outline nests them.
  {
    const NamedNodes named =
        collect_named_nodes(document, *_core.nameProvider, token);
    std::vector<bool> owns(named.nodes.size(), false);
    for (const std::size_t owner : named.owners)
      if (owner != NamedNodes::kNoOwner) owns[owner] = true;
    for (std::size_t i = 0; i < named.nodes.size(); ++i) {
      const CstNodeView& nameNode = named.nameNodes[i];
      if (nameNode.end() <= nameNode.begin() ||
          !touches(nameNode.begin(), nameNode.end()))
        continue;
      tokens.push_back(
          Token{nameNode.begin(), nameNode.end(),
                owns[i] ? SemanticTokenType::Type : SemanticTokenType::Property,
                true});
    }
  }

  // 2. References, read from the document's table since only the grammar can
  // resolve one. Both walks run: an unresolved name is still coloured.
  if (const DocumentReferences* const refs = parsed.references.get()) {
    const auto colour = [&](TextOffset from, TextOffset to) {
      if (to > from && touches(from, to))
        tokens.push_back(Token{from, to, SemanticTokenType::Variable, true});
    };
    refs->visitResolved(cst, [&](TextOffset begin, TextOffset end,
                                 const AstNode&, bool) { colour(begin, end); });
    refs->visitUnresolved(cst,
                          [&](RefState, TextOffset begin, TextOffset end,
                              const ReferenceInfo&) { colour(begin, end); });
  }

  // 3. Every leaf of the whole tree, not just the entry's range: a comment
  // before the first keyword sits before `entry_begin` but is still text the
  // editor colours.
  const std::string_view source = cst.text();
  const auto colourLeaf = [&](const CstLeaf& leaf) {
    throw_if_cancelled(token);
    if (leaf.end <= leaf.begin) return;
    // `hidden` means comment: the grammar's `hidden` list is recorded, its
    // `ignored` list is not, so whitespace never appears here to be mistaken
    // for one.
    if (leaf.hidden) {
      tokens.push_back(
          Token{leaf.begin, leaf.end, SemanticTokenType::Comment, false});
      return;
    }
    const std::string_view text =
        source.substr(leaf.begin, leaf.end - leaf.begin);
    SemanticTokenType type = SemanticTokenType::Keyword;
    // An empty record means the leaf is a literal the grammar spelled, not
    // text captured by a named rule.
    if (grammar.recordRule(cst[leaf.id]).empty()) {
      // A literal that opens like a word is a keyword the grammar reserved;
      // the rest of them is punctuation.
      const char head = text.empty() ? '\0' : text.front();
      const bool word =
          head == '_' || (static_cast<unsigned char>(head | 0x20) >= 'a' &&
                          static_cast<unsigned char>(head | 0x20) <= 'z');
      type = word ? SemanticTokenType::Keyword : SemanticTokenType::Operator;
    } else if (!literal_token(text, type)) {
      return;  // an identifier that is neither declared nor referenced
    }
    tokens.push_back(Token{leaf.begin, leaf.end, type, false});
  };
  if (begin == 0 && end >= source.size()) {
    cst.visit_leaves(colourLeaf);
  } else {
    // Off the leaf index: from the first leaf ending past `begin` to the last
    // one starting before `end`.
    for (NodeCount rank = cst.leaves_before(begin); rank < cst.leaf_count();
         ++rank) {
      const CstLeaf leaf = cst.leaf(rank);
      if (leaf.begin >= end) break;
      colourLeaf(leaf);
    }
  }

  // Sorted so overlaps resolve correctly: the wider span comes first at an
  // equal start, and a name outranks the leaf inside it. `stable_sort` keeps
  // equal tokens in collection order, so the answer is reproducible.
  std::ranges::stable_sort(tokens, [](const Token& a, const Token& b) {
    if (a.begin != b.begin) return a.begin < b.begin;
    if (a.end != b.end) return a.end > b.end;
    return a.named && !b.named;
  });

  if (tokens.empty()) return answer;
  TextDocument::Cursor cursor(document.textDocument(),
                                    tokens.front().begin);
  lsp::Uint prevLine = 0;
  lsp::Uint prevChar = 0;
  // The end of the last token published. Zero is the right start: nothing can
  // begin before the first byte, so the first token is never dropped by it.
  TextOffset emitted = 0;
  for (const Token& t : tokens) {
    throw_if_cancelled(token);
    if (t.begin < emitted) continue;
    emit(answer.data, cursor, prevLine, prevChar, t);
    emitted = t.end;
  }
  return answer;
}

}  // namespace cursiv
