#include "cursiv/lsp/completion/DefaultCompletionProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/core/documentation/DocumentationProvider.hpp"
#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/ScopeProvider.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/completion/FuzzyMatcher.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/symbols/NodeKindProvider.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

// The node the caret sits in; the root when nothing narrower owns it.
[[nodiscard]] AstNode* container_at(const Document& document, const Cst& cst,
                                    TextOffset caret) {
  AstNode* const owner = owner_at(cst, caret);
  return owner != nullptr ? owner : document.parseResult.value;
}

// A hidden leaf is a comment, a recovered one is what a repair discarded --
// even an unclosed `/*` counts, matching no terminal. The caret must sit
// strictly inside the leaf: `begin < caret < end`.
[[nodiscard]] bool caret_in_unread_text(const Cst& cst, TextOffset caret) {
  // The first leaf ending past the caret is the only one that can hold it:
  // ends never decrease, so every earlier leaf closed before it.
  const NodeCount rank = cst.leaves_before(caret);
  if (rank >= cst.leaf_count()) return false;
  const CstLeaf leaf = cst.leaf(rank);
  return (leaf.hidden || leaf.recovered) && leaf.begin < caret &&
         caret < leaf.end;
}

// One keyword, as an item: the grammar carries its bytes and prose whole.
[[nodiscard]] lsp::CompletionItem item_for(const Grammar& grammar,
                                           ElementId element,
                                           std::string_view spelled) {
  lsp::CompletionItem out;
  std::string word(spelled);
  out.kind = lsp::CompletionItemKindEnum(lsp::CompletionItemKind::Keyword);
  out.detail = std::string("Keyword");
  // After the names, which sort `0`.
  out.sortText = std::string("1");
  // The prose the grammar carries over the token, which is the same sentence
  // the hover shows over the same word.
  if (const std::string_view doc = grammar.terminalDoc(element); !doc.empty()) {
    lsp::MarkupContent content;
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
    content.value = std::string(doc);
    out.documentation = std::move(content);
  }
  out.label = std::move(word);
  return out;
}

// What one answer offers at most. A reference caret with nothing typed
// enumerates the whole global index, and every entry it keeps costs a handful
// of allocations plus a documentation render, under the workspace read lock.
// `isIncomplete` is already always true, which is the protocol's licence to
// stop: what the author types next asks again.
constexpr std::size_t kMaxItems = 1000;

// `visitScopeEntries` does not deduplicate -- a resolution wants candidates
// in declaration order, so a root-exported declaration is visited twice.
// Keying on (kind, label, detail) keeps two homonyms of different types apart.
[[nodiscard]] std::string dedup_key(const lsp::CompletionItem& item) {
  std::string key = item.kind.has_value()
                        ? std::to_string(static_cast<unsigned>(*item.kind))
                        : std::string("-");
  key += '\0';
  key += item.label;
  key += '\0';
  if (item.detail.has_value()) key += *item.detail;
  return key;
}

}  // namespace

// The word is the token the lexer cut: the leaf ending at the caret, else the
// one holding it or starting there, past the caret too -- the whole word is
// what an item replaces.
DefaultCompletionProvider::Anchor DefaultCompletionProvider::anchorAt(
    const Cst& cst, TextOffset offset) {
  const CstLeaf word = word_at(cst, offset);
  if (!word) return Anchor{offset, offset, {}};
  return Anchor{word.begin, word.end,
                cst.text().substr(word.begin, offset - word.begin)};
}

// A caret with no container answers no scope -- not the global one: that
// would hand out symbols a local scope excludes, and the author could accept
// a completion resolving to nothing.
bool DefaultCompletionProvider::offerReferences(
    const Document& document, const NextFeature& next, AstNode* container,
    const lsp::InsertReplaceEdit& edit, std::string_view filter,
    std::unordered_set<std::string>& seen, lsp::CompletionList& list,
    const CancellationToken& cancelToken) const {
  const AstReflection& reflection = *services.astReflection;
  const std::string_view feature = next.property;
  if (feature.empty() || container == nullptr) return false;

  std::uint16_t reference = kNoReference;
  if (next.type != nullptr) {
    // A newly entered rule owns the member even when an ancestor spells it too.
    reference = reflection.referenceIndexOf(next.type, feature);
    if (reference == kNoReference) return false;
  } else {
    bool declared = false;
    for (AstNode* at = container; at != nullptr; at = at->parent) {
      reference = reflection.referenceIndexOf(at->type, feature);
      if (reference != kNoReference) break;
      const TypeMetaData meta = reflection.getTypeMetaData(at->type);
      for (const PropertyMetaData& q : meta.properties)
        declared = declared || q.name == feature;
      for (const AstTypeTag* base : meta.superTypes)
        for (const PropertyMetaData& q :
             reflection.getTypeMetaData(base).properties)
          declared = declared || q.name == feature;
    }
    if (reference == kNoReference) {
      if (declared) return false;
      // A repair can remove the owner; only unanimous remaining types suffice.
      for (const AstTypeTag* type : reflection.getAllTypes()) {
        const std::uint16_t at = reflection.referenceIndexOf(type, feature);
        if (at == kNoReference) continue;
        if (reference != kNoReference && at != reference) return false;
        reference = at;
      }
      if (reference == kNoReference) return false;
    }
  }

  ReferenceInfo context;
  context.container = container;
  context.document = &document;
  context.reference = reference;
  context.referenceText = {};
  context.property = feature;
  // The site's own narrowing, which the tree can't say: the reference isn't
  // written yet, so only the grammar knows this site accepts less than the
  // member. Two sites of one member differ here; `seen` unions their offers.
  context.narrow = next.narrow;

  const FuzzyMatcher& matcher = *services.lspShared.fuzzyMatcher;
  const NodeKindProvider& kinds = *services.lspShared.nodeKindProvider;
  (void)services.scopeProvider->visitScopeEntries(
      context, [&](const AstNodeDescription& entry) {
        throw_if_cancelled(cancelToken);
        if (list.items.size() >= kMaxItems) return false;
        if (!matcher.match(filter, entry.name)) return true;
        lsp::CompletionItem item;
        item.label = std::string(entry.name);
        item.kind =
            lsp::CompletionItemKindEnum(kinds.getCompletionItemKind(entry));
        // The declared type is what makes the dedup key discriminate: two
        // homonyms of different types would otherwise merge into one offer.
        if (entry.type != nullptr) item.detail = std::string(entry.type->name);
        item.sortText = std::string("0");
        lsp::InsertReplaceEdit own = edit;
        own.newText = item.label;
        item.textEdit = buildCompletionTextEdit(std::move(own));
        if (seen.insert(dedup_key(item)).second) {
          // The candidate's own documentation, asked of its language's
          // provider -- a global candidate may come from another grammar.
          // After the dedup test, so a discarded double renders nothing.
          if (entry.node != nullptr)
            if (const Document* const home = entry.node->getDocument())
              if (const CoreServices* const language = home->language)
                if (const DocumentationProvider* const docs =
                        language->documentationProvider.get())
                  if (std::optional<std::string> doc =
                          docs->getDocumentation(*entry.node, home)) {
                    lsp::MarkupContent content;
                    content.kind =
                        lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
                    content.value = std::move(*doc);
                    item.documentation = std::move(content);
                  }
          list.items.push_back(std::move(item));
        }
        return true;
      });
  return true;
}

lsp::CompletionList DefaultCompletionProvider::getCompletion(
    const Document& document, lsp::Position position,
    const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  lsp::CompletionList list;
  list.isIncomplete = true;
  const Grammar& grammar = services.parser->getGrammar();

  const std::string_view text = document.textDocument().getText();
  const TextOffset offset =
      std::min(document.textDocument().offsetAt(from_lsp(position)),
               static_cast<TextOffset>(text.size()));
  const Cst& cst = document.parseResult.cst();
  const Anchor anchor = anchorAt(cst, offset);
  // Both ranges, and the editor chooses (`editor.suggest.insertMode`): insert
  // stops at the caret, replace takes the whole word. A single range would
  // force insert, which is what leaves `Boolol` when `Bool` is accepted
  // mid-word.
  lsp::InsertReplaceEdit word;
  word.insert = to_lsp(document.textDocument().rangeAt(anchor.from, offset));
  word.replace =
      to_lsp(document.textDocument().rangeAt(anchor.from, anchor.to));

  std::unordered_set<std::string> seen;
  const FuzzyMatcher& matcher = *services.lspShared.fuzzyMatcher;
  // Nothing is offered inside a comment: what stands there is prose, not a
  // continuation the grammar recognizes. Same for text a repair discarded --
  // the parse read neither as grammar.
  if (caret_in_unread_text(cst, offset)) return list;
  const auto harvest = [&](TextOffset caret, TextOffset caretEnd,
                           std::string_view filter,
                           const lsp::InsertReplaceEdit& edit) {
    // The engine answers first: an LR tape is replayed up to the caret, a
    // descent walks to the last token before it -- either way the derivation
    // path a re-parse would keep.
    std::vector<NextFeature> nexts;
    services.parser->nextFeaturesAt(cst, caret, caretEnd, nexts);
    const CompletionContext context{document, nexts,  caret,
                                    caretEnd, offset, position};
    // Asked only where a member is being filled: reaching the node decodes
    // the tape, and continuations that write none -- every keyword of a
    // grammar without cross references -- must not pay for it.
    AstNode* container = nullptr;
    bool climbed = false;
    const auto container_here = [&]() -> AstNode* {
      if (!climbed) {
        climbed = true;
        container = container_at(document, cst, caret);
      }
      return container;
    };

    for (const NextFeature& next : nexts) {
      throw_if_cancelled(cancelToken);
      // A reference is not a token to name but a scope to offer: the grammar
      // names the member, the node at the caret names its class, and
      // reflection joins the two into the reference the scope answers for.
      if (!next.property.empty() &&
          offerReferences(document, next, container_here(), edit, filter, seen,
                          list, cancelToken))
        continue;
      // Everything else is a keyword or nothing at all: a terminal's name is
      // not its text, and an item labelled `ID` would insert those letters.
      const std::string_view spelled = grammar.terminalText(next.feature);
      if (spelled.empty() || !filterKeyword(context, spelled)) continue;
      if (!matcher.match(filter, spelled)) continue;
      lsp::CompletionItem item = item_for(grammar, next.feature, spelled);
      lsp::InsertReplaceEdit own = edit;
      own.newText = item.label;
      item.textEdit = buildCompletionTextEdit(std::move(own));
      if (seen.insert(dedup_key(item)).second)
        list.items.push_back(std::move(item));
    }
  };

  // The word's own end, not the caret: a literal is offered only where the
  // lexer would still read it spliced in with what the edit leaves behind.
  harvest(anchor.from, anchor.to, anchor.prefix, word);

  // Where the word answers nothing, the caret itself is asked: anchoring at
  // the token start is wrong when the word is already a complete token the
  // grammar accepts, since then what may replace it is the answer, not what
  // follows it. The fallback itself replaces nothing.
  //
  // Only at the word's end: what it offers are the successors of a whole word,
  // which is why it filters on nothing and replaces nothing.
  if (list.items.empty() && offset > anchor.from && offset == anchor.to) {
    throw_if_cancelled(cancelToken);
    lsp::InsertReplaceEdit here;
    here.insert = to_lsp(document.textDocument().rangeAt(offset, offset));
    here.replace = here.insert;
    harvest(offset, offset, {}, here);
  }
  return list;
}

bool DefaultCompletionProvider::filterKeyword(const CompletionContext&,
                                              std::string_view) const {
  return true;
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

lsp::OneOf<lsp::TextEdit, lsp::InsertReplaceEdit>
DefaultCompletionProvider::buildCompletionTextEdit(
    lsp::InsertReplaceEdit edit) const {
  if (clientCapabilities.has_value() &&
      clientCapabilities->completionItem.has_value() &&
      clientCapabilities->completionItem->insertReplaceSupport.value_or(false))
    return edit;
  return lsp::TextEdit{.range = edit.insert,
                       .newText = std::move(edit.newText)};
}

}  // namespace cursiv
