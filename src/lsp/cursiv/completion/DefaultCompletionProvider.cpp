#include "cursiv/completion/DefaultCompletionProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/documentation/DocumentationProvider.hpp"
#include "cursiv/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/word_scalar.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/completion/FuzzyMatcher.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/symbols/NodeKindProvider.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv {

namespace {

// The owner of the leaf holding `offset`, else of the last one before it: null
// where neither stands. Trivia and what a repair discarded hold nothing.
[[nodiscard]] AstNode* node_at(const Cst& cst, TextOffset offset) {
  const NodeCount rank = cst.leaves_before(offset);
  if (rank < cst.leaf_count())
    if (const CstLeaf at = cst.leaf(rank);
        at.begin <= offset && offset < at.end && !at.hidden && !at.recovered)
      return owner_of_leaf(cst, at.id);
  return owner_at(cst, offset);
}

// A hidden leaf is a comment, a recovered one is what a repair discarded --
// even an unclosed block comment counts, matching no terminal. The caret must
// sit strictly inside the leaf: `begin < caret < end`.
[[nodiscard]] bool caret_in_unread_text(const Cst& cst, TextOffset caret) {
  // The first leaf ending past the caret is the only one that can hold it:
  // ends never decrease, so every earlier leaf closed before it.
  const NodeCount rank = cst.leaves_before(caret);
  if (rank >= cst.leaf_count()) return false;
  const CstLeaf leaf = cst.leaf(rank);
  return (leaf.hidden || leaf.recovered) && leaf.begin < caret &&
         caret < leaf.end;
}

[[nodiscard]] bool is_letter(char32_t scalar) noexcept {
  return unicode::within(unicode::detail::kL, std::size(unicode::detail::kL),
                         scalar);
}

// The names one answer offers at most; its keywords always come. A reference
// caret with nothing typed enumerates the whole global index, and every entry
// it keeps costs a handful of allocations plus a documentation render, under
// the workspace read lock.
// `isIncomplete` is already always true, which is the protocol's licence to
// stop: what the author types next asks again.
constexpr std::size_t kMaxItems = 1000;

// Kind, label and detail: two homonyms of different types stay apart. Read
// through views: the items outlive the set, and nothing is copied per item.
struct ItemKey {
  unsigned kind;
  std::string_view label;
  std::string_view detail;
  bool operator==(const ItemKey&) const = default;
};
struct ItemKeyHash {
  std::size_t operator()(const ItemKey& key) const noexcept {
    return std::hash<std::string_view>{}(key.label) ^
           std::hash<std::string_view>{}(key.detail);
  }
};

// A data rule reads tokens and writes nothing: no member, no node of its own.
// The records of `[from, to)` read so already, which an inner rule's are.
[[nodiscard]] bool writes_nothing(const Cst& cst, const Grammar& grammar,
                                  NodeId record, NodeId from, NodeId to) {
  const auto writes = [&](NodeId k) {
    const CstNode& n = cst[k];
    return !n.hidden && (!grammar.recordFeature(n).empty() ||
                         (n.subtreeSize > 1 && grammar.recordRule(n).empty()));
  };
  for (NodeId k = cst.subtree_begin(record); k < from; ++k)
    if (writes(k)) return false;
  for (NodeId k = to; k < record; ++k)
    if (writes(k)) return false;
  return true;
}

}  // namespace

AstNode* CompletionContext::node() const {
  if (!_node.has_value())
    _node = node_at(document.parseResult.cst(), _nodeOffset);
  return *_node;
}

lsp::CompletionList DefaultCompletionProvider::getCompletion(
    const Document& document, const lsp::CompletionParams& params,
    const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  lsp::CompletionList list;
  list.isIncomplete = true;
  std::vector<lsp::CompletionItem> items;
  const auto accept = [&](const CompletionContext& context,
                          CompletionValueItem&& value) {
    if (fillCompletionItem(context, value))
      items.push_back(std::move(static_cast<lsp::CompletionItem&>(value)));
    return items.size() < kMaxItems;
  };
  buildContexts(document, params, cancelToken,
                [&](const CompletionContext& context) {
                  for (const NextFeature& next : context.features) {
                    throw_if_cancelled(cancelToken);
                    completionFor(context, next, accept);
                  }
                  return continueCompletion(items);
                });
  list.items = deduplicateItems(std::move(items));
  return list;
}

void DefaultCompletionProvider::buildContexts(
    const Document& document, const lsp::CompletionParams& params,
    const CancellationToken& cancelToken,
    function_ref<bool(const CompletionContext&)> visit) const {
  const lsp::Position position = params.position;
  const TextDocument& lines = document.textDocument();
  const std::string_view text = lines.getText();
  const TextOffset offset = std::min(lines.offsetAt(from_lsp(position)),
                                     static_cast<TextOffset>(text.size()));
  const Cst& cst = document.parseResult.cst();
  // Nothing is offered inside a comment: what stands there is prose, not a
  // continuation the grammar recognizes. Same for text a repair discarded --
  // the parse read neither as grammar.
  if (caret_in_unread_text(cst, offset)) return;
  const std::uint32_t line = lines.positionAt(offset).line;
  const TextOffset lineStart = lines.offsetAt({line, 0});
  const TextOffset lineEnd =
      lines.offsetAt({line, std::numeric_limits<std::uint32_t>::max()});
  // One token, `identifier` its part on the caret's line where it spans lines.
  const auto ask = [&](TextOffset begin, TextOffset end, TextOffset nodeOffset,
                       std::string_view identifier) {
    std::vector<NextFeature> features;
    findFeaturesAt(document, begin, end, features);
    const TextOffset from = std::max(begin, lineStart);
    const CompletionContext context(
        document, features, begin, end, nodeOffset, offset, position,
        identifier, to_lsp(lines.rangeAt(from, offset)),
        to_lsp(lines.rangeAt(from, std::min(end, lineEnd))), params.context,
        cancelToken);
    return visit(context);
  };
  const auto typed_from = [&](TextOffset begin) {
    const TextOffset from = std::max(begin, lineStart);
    return text.substr(from, offset - from);
  };

  // All a data rule holds up to the caret is what is typed of it, so a comment
  // inside it matches nothing.
  if (const auto rule = findDataTypeRuleStart(cst, offset); rule.has_value())
    if (!ask(rule->first, rule->second, rule->first,
             text.substr(rule->first, offset - rule->first)))
      return;

  const CompletionBacktrackingInformation tokens =
      backtrackToAnyToken(cst, offset);
  // A caret still before the next token stands on the previous one's node.
  const TextOffset nodeOffset =
      offset <= tokens.nextTokenStart && tokens.previousTokenStart.has_value()
          ? *tokens.previousTokenStart
          : tokens.nextTokenStart;
  bool performNext = true;
  if (tokens.previousTokenEnd == offset) {
    const TextOffset start = *tokens.previousTokenStart;
    const std::string_view token = text.substr(start, offset - start);
    if (performCurrentTokenCompletion(document, token, start, offset) &&
        !ask(start, offset, nodeOffset, typed_from(start)))
      return;
    performNext = performNextTokenCompletion(document, token, start, offset);
    if (performNext && !ask(offset, offset, nodeOffset, {})) return;
    // The next token is the same place where none holds the caret.
    if (tokens.nextTokenStart == offset && tokens.nextTokenEnd == offset)
      return;
  }
  if (performNext || node_at(cst, nodeOffset) == nullptr)
    (void)ask(tokens.nextTokenStart, tokens.nextTokenEnd, nodeOffset,
              typed_from(tokens.nextTokenStart));
}

void DefaultCompletionProvider::findFeaturesAt(
    const Document& document, TextOffset offset, TextOffset end,
    std::vector<NextFeature>& features) const {
  services.parser->nextFeaturesAt(document.parseResult.cst(), offset, end,
                                  features);
}

bool DefaultCompletionProvider::performCurrentTokenCompletion(
    const Document&, std::string_view text, TextOffset, TextOffset) const {
  for (std::size_t at = 0; at < text.size();) {
    std::size_t len = 0;
    if (detail::word_scalar(
            detail::scalar_at(text.data() + at, text.size() - at, len)))
      return true;
    at += std::max<std::size_t>(len, 1);
  }
  return false;
}

bool DefaultCompletionProvider::performNextTokenCompletion(
    const Document&, std::string_view text, TextOffset, TextOffset) const {
  if (text.empty()) return false;
  std::size_t last = text.size() - 1;
  while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xC0u) == 0x80u)
    --last;
  std::size_t len = 0;
  return !is_letter(
      detail::scalar_at(text.data() + last, text.size() - last, len));
}

std::optional<std::pair<TextOffset, TextOffset>>
DefaultCompletionProvider::findDataTypeRuleStart(const Cst& cst,
                                                 TextOffset offset) const {
  const Grammar& grammar = services.parser->getGrammar();
  const std::string_view text = cst.text();
  // The leaf at the caret, or the one behind it where no word goes on there.
  std::size_t len = 0;
  const bool word = offset < text.size() &&
                    detail::word_scalar(detail::scalar_at(
                        text.data() + offset, text.size() - offset, len));
  const TextOffset at = word || offset == 0 ? offset : offset - 1;
  const NodeCount rank = cst.leaves_before(at);
  if (rank >= cst.leaf_count()) return std::nullopt;
  const CstLeaf leaf = cst.leaf(rank);
  if (leaf.hidden || leaf.begin > at || at >= leaf.end) return std::nullopt;
  // A data rule of one token has no record but its leaf.
  NodeId found = grammar.recordIsDataRule(cst[leaf.id]) ? leaf.id : kNoNode;
  NodeId from = leaf.id;
  NodeId to = leaf.id;
  for (NodeId i = leaf.id + 1; i < cst.size(); ++i) {
    if (!cst.contains(i, leaf.id)) continue;
    if (grammar.recordRule(cst[i]).empty() ||
        !writes_nothing(cst, grammar, i, from, to))
      break;
    found = i;
    from = cst.subtree_begin(i);
    to = i;
  }
  if (found == kNoNode) return std::nullopt;
  return std::pair{cst[found].begin, cst[found].end};
}

CompletionBacktrackingInformation
DefaultCompletionProvider::backtrackToAnyToken(const Cst& cst,
                                               TextOffset offset) const {
  const auto is_token = [](const CstLeaf& leaf) {
    return !leaf.hidden && leaf.end > leaf.begin;
  };
  CompletionBacktrackingInformation out;
  out.nextTokenStart = out.nextTokenEnd = offset;
  const NodeCount rank = cst.leaves_before(offset);
  for (NodeCount r = rank; r > 0; --r)
    if (const CstLeaf before = cst.leaf(r - 1); is_token(before)) {
      out.previousTokenStart = before.begin;
      out.previousTokenEnd = before.end;
      break;
    }
  for (NodeCount r = rank; r < cst.leaf_count(); ++r)
    if (const CstLeaf after = cst.leaf(r); is_token(after)) {
      if (after.begin < offset) {
        out.nextTokenStart = after.begin;
        out.nextTokenEnd = after.end;
      }
      break;
    }
  return out;
}

bool DefaultCompletionProvider::continueCompletion(
    const std::vector<lsp::CompletionItem>& items) const {
  return items.empty();
}

std::vector<lsp::CompletionItem> DefaultCompletionProvider::deduplicateItems(
    std::vector<lsp::CompletionItem> items) const {
  {
    std::unordered_set<ItemKey, ItemKeyHash> seen;
    seen.reserve(items.size());
    std::vector<bool> doubled(items.size());
    bool any = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const lsp::CompletionItem& item = items[i];
      const std::string_view detail =
          item.detail.has_value() ? std::string_view(*item.detail)
                                  : std::string_view{};
      const unsigned kind = item.kind.has_value()
                                ? static_cast<unsigned>(*item.kind)
                                : ~0u;
      doubled[i] = !seen.insert(ItemKey{kind, item.label, detail}).second;
      any = any || doubled[i];
    }
    if (!any) return items;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
      if (!doubled[i]) {
        if (kept != i) items[kept] = std::move(items[i]);
        ++kept;
      }
    items.resize(kept);
  }
  return items;
}

void DefaultCompletionProvider::completionFor(const CompletionContext& context,
                                              const NextFeature& next,
                                              CompletionAcceptor accept) const {
  // A cross-reference is not a token to name but a scope to offer: the grammar
  // names the member, the node at the caret names its class, and reflection
  // joins the two into the reference the scope answers for.
  AstNodePtr entered;
  if (const std::optional<ReferenceInfo> reference =
          referenceInfoOf(context, next, entered)) {
    completionForCrossReference(context, next, *reference, accept);
    return;
  }
  // Everything else is a keyword or nothing at all: a named terminal says
  // nothing of its contents, and a data rule offers its words through its
  // keywords.
  const Grammar& grammar = services.parser->getGrammar();
  if (!grammar.terminalName(next.feature).empty()) return;
  for (const std::string_view spelled : grammar.terminalSpellings(next.feature))
    completionForKeyword(context, next, spelled, accept);
}

std::optional<ReferenceInfo> DefaultCompletionProvider::referenceInfoOf(
    const CompletionContext& context, const NextFeature& next,
    AstNodePtr& entered) const {
  // Asked first: the caret's node is looked up once, and only when needed.
  if (next.property.empty()) return std::nullopt;
  return detail::reference_at_caret(*services.astReflection, context.document,
                                    context.node(), next, entered);
}

void DefaultCompletionProvider::completionForCrossReference(
    const CompletionContext& context, const NextFeature&,
    const ReferenceInfo& reference, CompletionAcceptor accept) const {
  const FuzzyMatcher& matcher = *services.lspShared.fuzzyMatcher;
  // Asked before an item is made: making one renders its documentation, and
  // with nothing typed the whole global index goes by. The scope does not
  // deduplicate -- a resolution wants candidates in declaration order, so a
  // root-exported declaration comes twice.
  using Seen = std::pair<std::string_view, const AstTypeTag*>;
  const auto hash = [](const Seen& s) {
    return std::hash<std::string_view>{}(s.first) ^
           std::hash<const AstTypeTag*>{}(s.second);
  };
  std::unordered_set<Seen, decltype(hash)> seen(16, hash);
  visitReferenceCandidates(
      reference, context, [&](const AstNodeDescription& candidate) {
        throw_if_cancelled(context.cancelToken);
        if (!matcher.match(context.identifier, candidate.name)) return true;
        if (!seen.insert(Seen{candidate.name, candidate.type}).second)
          return true;
        CompletionValueItem item =
            createReferenceCompletionItem(candidate, reference, context);
        item.matched = item.label.empty();
        return accept(context, std::move(item));
      });
}

void DefaultCompletionProvider::visitReferenceCandidates(
    const ReferenceInfo& reference, const CompletionContext&,
    function_ref<bool(const AstNodeDescription&)> visitor) const {
  (void)services.scopeProvider->visitScopeEntries(reference, visitor);
}

CompletionValueItem DefaultCompletionProvider::createReferenceCompletionItem(
    const AstNodeDescription& candidate, const ReferenceInfo&,
    const CompletionContext&) const {
  CompletionValueItem item;
  item.nodeDescription = &candidate;
  item.kind = lsp::CompletionItemKindEnum(
      services.lspShared.nodeKindProvider->getCompletionItemKind(candidate));
  if (candidate.type != nullptr)
    item.detail = std::string(candidate.type->name);
  item.sortText = std::string("0");
  if (std::optional<lsp::MarkupContent> documentation =
          getReferenceDocumentation(candidate))
    item.documentation = std::move(*documentation);
  return item;
}

// The candidate's own documentation, asked of its language's provider -- a
// global candidate may come from another grammar.
std::optional<lsp::MarkupContent>
DefaultCompletionProvider::getReferenceDocumentation(
    const AstNodeDescription& candidate) const {
  if (candidate.node == nullptr) return std::nullopt;
  const Document* const home = candidate.node->getDocument();
  if (home == nullptr || home->language == nullptr) return std::nullopt;
  const DocumentationProvider* const docs =
      home->language->documentationProvider.get();
  if (docs == nullptr) return std::nullopt;
  std::optional<std::string> doc = docs->getDocumentation(*candidate.node, home);
  if (!doc.has_value()) return std::nullopt;
  lsp::MarkupContent content;
  content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
  content.value = std::move(*doc);
  return content;
}

void DefaultCompletionProvider::completionForKeyword(
    const CompletionContext& context, const NextFeature& next,
    std::string_view keyword, CompletionAcceptor accept) const {
  if (!filterKeyword(context, keyword)) return;
  CompletionValueItem item;
  item.label = std::string(keyword);
  item.kind = lsp::CompletionItemKindEnum(getKeywordCompletionItemKind(keyword));
  item.detail = std::string("Keyword");
  // After the names, which sort `0`.
  item.sortText = std::string("1");
  // The prose the grammar carries over the token, which is the same sentence
  // the hover shows over the same word. A word with several has none here:
  // nothing has read this use yet.
  if (const std::string_view doc =
          services.parser->getGrammar().terminalDoc(next.feature);
      !doc.empty()) {
    lsp::MarkupContent content;
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
    content.value = std::string(doc);
    item.documentation = std::move(content);
  }
  (void)accept(context, std::move(item));
}

lsp::CompletionItemKind DefaultCompletionProvider::getKeywordCompletionItemKind(
    std::string_view) const {
  return lsp::CompletionItemKind::Keyword;
}

bool DefaultCompletionProvider::filterKeyword(const CompletionContext&,
                                              std::string_view keyword) const {
  for (std::size_t at = 0; at < keyword.size();) {
    std::size_t len = 0;
    if (is_letter(
            detail::scalar_at(keyword.data() + at, keyword.size() - at, len)))
      return true;
    at += std::max<std::size_t>(len, 1);
  }
  return false;
}

bool DefaultCompletionProvider::fillCompletionItem(
    const CompletionContext& context, CompletionValueItem& value) const {
  if (value.label.empty()) {
    if (value.node != nullptr) {
      const std::optional<std::string_view> name =
          services.nameProvider->getName(*value.node);
      if (!name.has_value()) return false;
      value.label = std::string(*name);
    } else if (value.nodeDescription != nullptr) {
      value.label = std::string(value.nodeDescription->name);
    } else {
      return false;
    }
  }
  if (!value.textEdit.has_value()) {
    if (!value.matched &&
        !services.lspShared.fuzzyMatcher->match(context.identifier,
                                                value.label))
      return false;
    value.textEdit = buildCompletionTextEdit(
        context, value.insertText.has_value() ? *value.insertText : value.label);
  }
  return true;
}

lsp::OneOf<lsp::TextEdit, lsp::InsertReplaceEdit>
DefaultCompletionProvider::buildCompletionTextEdit(
    const CompletionContext& context, std::string newText) const {
  if (clientCapabilities.has_value() &&
      clientCapabilities->completionItem.has_value() &&
      clientCapabilities->completionItem->insertReplaceSupport.value_or(false)) {
    lsp::InsertReplaceEdit edit;
    edit.newText = std::move(newText);
    edit.insert = context.insertRange;
    edit.replace = context.replaceRange;
    return edit;
  }
  return lsp::TextEdit{.range = context.insertRange,
                       .newText = std::move(newText)};
}

DefaultCompletionProvider::DefaultCompletionProvider(
    const LspServices& services)
    : services(services) {
  if (LanguageServer* const server = services.lspShared.languageServer.get())
    server->onInitialize([self = std::weak_ptr(_self)](
                             const lsp::InitializeParams& params) {
      if (const auto alive = self.lock()) (*alive)->initialize(params);
    });
}

void DefaultCompletionProvider::initialize(
    const lsp::InitializeParams& params) {
  clientCapabilities = params.capabilities.textDocument.has_value()
                           ? params.capabilities.textDocument->completion
                           : std::nullopt;
}

}  // namespace cursiv
