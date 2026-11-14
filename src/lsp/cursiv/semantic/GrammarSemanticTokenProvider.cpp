#include "cursiv/semantic/GrammarSemanticTokenProvider.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/semantic/AbstractSemanticTokenProvider.hpp"
#include "cursiv/symbols/NamedNodes.hpp"

namespace cursiv {

namespace {

// What this provider tells apart, each coloured with the legend's type of the
// same name.
enum class Category : std::uint8_t {
  Keyword,   // a guarded literal: a word the grammar reserved
  Type,      // a declaration that owns other declarations
  Variable,  // a reference: a name standing for a declaration elsewhere
  Property,  // a declaration that owns none: a member of its owner
  Number,    // a leaf whose text opens like a number
  String,    // a leaf whose text opens with a quote
  Comment,   // a record of the grammar's `hidden` list
  Operator,  // an unguarded literal: punctuation
};
constexpr std::array<std::string_view, 8> kCategoryTypes = {
    "keyword", "type",   "variable", "property",
    "number",  "string", "comment",  "operator"};

// The CST does not say what a terminal lexes: the first character tells a
// string from a number.
[[nodiscard]] bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] bool literal_token(std::string_view text, Category& category) {
  if (text.empty()) return false;
  const char head = text.front();
  if (head == '"' || head == '\'') {
    category = Category::String;
    return true;
  }
  if (const bool signedNumber = (head == '-' || head == '+' || head == '.') &&
                                text.size() > 1 && is_digit(text[1]);
      is_digit(head) || signedNumber) {
    category = Category::Number;
    return true;
  }
  return false;
}

// Whether a declaration lies below `node`, as `collect_named_nodes` nests
// them; found among the nodes of its record's subtree.
[[nodiscard]] bool owns_a_name(const Document& document,
                               const NameProvider& names, const AstNode& node,
                               const CancellationToken& token) {
  const Cst& cst = document.parseResult.cst();
  const OwnedNodes all = document.parseResult.nodes();
  const NodeId id = node.cstNodeId;
  for (auto at = std::ranges::lower_bound(all, cst.subtreeBegin(id), {},
                                          &AstNode::cstNodeId);
       at != all.end() && (*at)->cstNodeId <= id; ++at) {
    throw_if_cancelled(token);
    if (*at == &node || !names.getNameNode(**at, &document)) continue;
    for (const AstNode* p = (*at)->parent; p != nullptr; p = p->parent)
      if (p == &node) return true;
  }
  return false;
}

}  // namespace

void GrammarSemanticTokenProvider::computeHighlighting(
    const Document& document, TextOffset begin, TextOffset end,
    SemanticTokenAcceptor accept, const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return;

  // Resolved per request: a derived provider may rename the legend's rows.
  std::array<std::optional<std::uint32_t>, kCategoryTypes.size()> types;
  for (std::size_t i = 0; i < types.size(); ++i)
    types[i] = tokenType(kCategoryTypes[i]);
  // A name accepted before the leaf inside it covers the same bytes, so the
  // name is what survives.
  const auto colour = [&](TextOffset from, TextOffset to, Category category) {
    if (const auto& type = types[static_cast<std::size_t>(category)])
      accept(SemanticToken{.begin = from, .end = to, .type = *type});
  };

  const ParseResult& parsed = document.parseResult;
  const Cst& cst = parsed.cst();
  const Grammar& grammar = services.parser->getGrammar();
  // A token touches the window when it neither ends before it nor starts
  // after it; one cut by the edge is coloured whole.
  const auto touches = [&](TextOffset from, TextOffset to) {
    return from < end && to > begin;
  };

  // The window's leaves: from the first one ending past `begin` to the last
  // one starting before `end`, `past` the leaf after it or `size()`.
  const bool whole = begin == 0 && end >= cst.text().size();
  const NodeId first = whole ? 0 : cst.leafAfter(begin);
  NodeId past = first;
  while (!whole && past < cst.size() && cst[past].begin < end)
    past = cst.nextLeaf(past);

  // 1. Declarations and their owners, nested as the outline nests them.
  const auto declare = [&](const CstNodeView& nameNode, bool owner) {
    if (nameNode.end() <= nameNode.begin() ||
        !touches(nameNode.begin(), nameNode.end()))
      return;
    colour(nameNode.begin(), nameNode.end(),
           owner ? Category::Type : Category::Property);
  };
  if (whole) {
    const detail::NamedNodes named =
        detail::collect_named_nodes(document, *services.nameProvider, token);
    std::vector<bool> owns(named.nodes.size(), false);
    for (const std::size_t owner : named.owners)
      if (owner != detail::NamedNodes::kNoOwner) owns[owner] = true;
    for (std::size_t i = 0; i < named.nodes.size(); ++i)
      declare(named.nameNodes[i], owns[i]);
  } else if (first < cst.size() && past > 0) {
    // A name lies in its node's record, so only the nodes whose record holds a
    // leaf of the window, or the gap it falls in, are asked.
    const NodeId lo = first;
    const NodeId hi = cst.leafBefore(past);
    const OwnedNodes all = parsed.nodes();
    std::vector<const AstNode*> touching;
    auto at = std::ranges::lower_bound(all, lo, {}, &AstNode::cstNodeId);
    for (; at != all.end() && (*at)->cstNodeId <= hi; ++at)
      touching.push_back(*at);
    for (const AstNode* p = at != all.end() ? *at : nullptr; p != nullptr;
         p = p->parent)
      if (p->cstNodeId < cst.size() && cst.subtreeBegin(p->cstNodeId) <= hi)
        touching.push_back(p);
    for (const AstNode* const node : touching) {
      throw_if_cancelled(token);
      if (const auto nameNode =
              services.nameProvider->getNameNode(*node, &document);
          nameNode && nameNode.end() > nameNode.begin() &&
          touches(nameNode.begin(), nameNode.end()))
        declare(nameNode,
                owns_a_name(document, *services.nameProvider, *node, token));
    }
  }

  // 2. References, read from the document's table since only the grammar can
  // resolve one. Both walks run: an unresolved name is still coloured. A
  // window asks the table for the names that touch it only.
  if (const DocumentReferences* const refs = parsed.references.get()) {
    const auto reference = [&](TextOffset from, TextOffset to) {
      if (to > from && touches(from, to)) colour(from, to, Category::Variable);
    };
    const auto resolved = [&](TextOffset lo, TextOffset hi, const AstNode&,
                              bool) { reference(lo, hi); };
    const auto unresolved = [&](RefState, TextOffset lo, TextOffset hi,
                                const ReferenceInfo&) { reference(lo, hi); };
    if (whole) {
      refs->visitResolved(cst, resolved);
      refs->visitUnresolved(cst, unresolved);
    } else {
      refs->visitIn(cst, begin, end, resolved, unresolved);
    }
  }

  // 3. Every leaf of the whole tree, not just the entry's range: a comment
  // before the first keyword sits before `entryBegin` but is still text the
  // editor colours.
  const std::string_view source = cst.text();
  const auto colourLeaf = [&](const CstLeaf& leaf) {
    throw_if_cancelled(token);
    if (leaf.end <= leaf.begin) return;
    // `hidden` means comment: the grammar's `hidden` list is recorded, its
    // `ignored` list is not, so whitespace never appears here to be mistaken
    // for one.
    if (leaf.hidden) {
      colour(leaf.begin, leaf.end, Category::Comment);
      return;
    }
    const std::string_view text =
        source.substr(leaf.begin, leaf.end - leaf.begin);
    Category category = Category::Keyword;
    // An empty record means the leaf is a literal the grammar spelled, not
    // text captured by a named rule.
    if (grammar.recordRule(cst[leaf.id]).empty()) {
      // A literal that opens like a word is a keyword the grammar reserved;
      // the rest of them is punctuation.
      const char head = text.empty() ? '\0' : text.front();
      const bool word =
          head == '_' || (static_cast<unsigned char>(head | 0x20) >= 'a' &&
                          static_cast<unsigned char>(head | 0x20) <= 'z');
      category = word ? Category::Keyword : Category::Operator;
    } else if (!literal_token(text, category)) {
      return;  // an identifier that is neither declared nor referenced
    }
    colour(leaf.begin, leaf.end, category);
  };
  if (whole) {
    cst.visitLeaves(colourLeaf);
  } else {
    for (NodeId id = first; id < past; id = cst.nextLeaf(id))
      colourLeaf(cst.asLeaf(id));
  }
}

}  // namespace cursiv
