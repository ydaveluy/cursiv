#include <pegium/lsp/completion/DefaultCompletionProvider.hpp>

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <pegium/core/documentation/DocumentationProvider.hpp>
#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>
#include <pegium/core/workspace/AstDescriptions.hpp>

namespace pegium {
using namespace pegium::provider_detail;

namespace {

const AstNode *find_completion_node(const workspace::Document &document,
                                    TextOffset anchorOffset) {
  if (!document.hasAst()) {
    return nullptr;
  }

  const auto &root = *document.parseResult.value;
  if (const auto *node = find_ast_node_at_offset(root, anchorOffset);
      node != nullptr) {
    return node;
  }
  if (anchorOffset > 0) {
    if (const auto *node = find_ast_node_at_offset(root, anchorOffset - 1);
        node != nullptr) {
      return node;
    }
  }

  if (const auto size =
          static_cast<TextOffset>(document.textDocument().getText().size());
      anchorOffset < size) {
    if (const auto *node = find_ast_node_at_offset(root, anchorOffset + 1);
        node != nullptr) {
      return node;
    }
  }

  return &root;
}

std::optional<::lsp::TextEdit>
build_completion_text_edit(const CompletionContext &context,
                           std::string_view matchText, std::string_view newText,
                           const FuzzyMatcher &matcher) {
  if (!matcher.match(context.prefix, matchText)) {
    return std::nullopt;
  }

  ::lsp::TextEdit textEdit{};
  textEdit.newText = std::string(newText);
  textEdit.range.start =
      context.document.textDocument().positionAt(context.tokenOffset);
  // Replace up to the (possibly widened) end of the token/reference, not just up
  // to the cursor. Completing in the middle of a name otherwise leaves the
  // suffix after the cursor behind. tokenEndOffset equals offset on the
  // suffix-fallback path, so that case is unaffected.
  textEdit.range.end =
      context.document.textDocument().positionAt(context.tokenEndOffset);
  return textEdit;
}

std::string default_sort_text(const CompletionContext &context,
                              const CompletionValue &value) {
  if (value.insertTextFormat.has_value() &&
      *value.insertTextFormat == ::lsp::InsertTextFormat::Snippet) {
    return "1";
  }
  if (!context.feature.isValid()) {
    return "9";
  }
  if (context.feature.expectedReferenceAssignment() != nullptr) {
    return "0";
  }
  if (context.feature.expectedRule() != nullptr) {
    return "1";
  }
  if (context.feature.literal() != nullptr) {
    return "2";
  }
  return "9";
}

} // namespace

std::optional<::lsp::CompletionList>
DefaultCompletionProvider::getCompletion(
    const workspace::Document &document, const ::lsp::CompletionParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);

  const auto &textDocument = document.textDocument();
  const auto text = textDocument.getText();
  const auto offset = textDocument.offsetAt(params.position);
  const auto token = token_at(text, offset);
  const auto tokenOffset = token.text.empty() ? offset : token.begin;
  const auto tokenEndOffset = token.text.empty() ? offset : token.end;
  const auto *reference = find_reference_at_offset(document, offset);

  // A reference parsed through a datatype rule (such as a dotted qualified name)
  // spans more than the token at the cursor. Widen the replaced range and the
  // fuzzy-match prefix to the whole reference so the full name is replaced, not
  // only the segment after the last separator. The parser frontier is still
  // computed at the natural token start: the tokens inside the datatype rule are
  // valid input that `expect` would otherwise consume, hiding the candidates.
  auto replaceOffset = tokenOffset;
  auto replaceEndOffset = tokenEndOffset;
  if (reference != nullptr) {
    if (const auto refNode = reference->getRefNode();
        refNode.valid() && refNode.getBegin() < tokenOffset) {
      replaceOffset = refNode.getBegin();
      replaceEndOffset = std::max(tokenEndOffset, refNode.getEnd());
    }
  }

  const auto prefixEnd = std::clamp(offset, replaceOffset, replaceEndOffset);
  const auto prefix = prefixEnd > replaceOffset
                          ? text.substr(replaceOffset, prefixEnd - replaceOffset)
                          : std::string_view{};
  const auto *node = find_completion_node(document, tokenOffset);

  std::vector<::lsp::CompletionItem> items;
  utils::TransparentStringSet itemsByKey;

  const auto accept =
      [this, &items, &itemsByKey](const CompletionContext &context,
                                  const CompletionValue &value) {
    ::lsp::CompletionItem item{};
    if (!fillCompletionItem(context, value, item)) {
      return;
    }

    const auto detail = item.detail.has_value()
                            ? std::string_view(*item.detail)
                            : std::string_view{};
    std::string key;
    key.reserve(item.label.size() + detail.size() + 1);
    key.append(item.label);
    key.push_back('\x1f');
    key.append(detail);
    if (!itemsByKey.insert(std::move(key)).second) {
      return;
    }
    items.push_back(std::move(item));
  };

  assert(services.parser != nullptr);
  const auto expect = services.parser->expect(text, tokenOffset, cancelToken);
  const auto &alternatives = expect.frontier;

  for (const auto &feature : alternatives) {
    utils::throw_if_cancelled(cancelToken);
    CompletionContext context{document, params, offset, replaceOffset,
                              replaceEndOffset, token.text, prefix, node,
                              reference, feature};
    completionFor(context, [&accept, &context](const CompletionValue &value) {
      accept(context, value);
    });
    if (!continueCompletion(context)) {
      break;
    }
  }

  // A reference whose name is a datatype rule (such as a dotted qualified name)
  // is not surfaced as a reference assignment in the parser frontier, so its
  // candidates would be missed. When a concrete reference sits under the cursor,
  // complete it directly over the widened (whole-name) range.
  if (reference != nullptr) {
    parser::ExpectPath referenceFeature;
    referenceFeature.elements.push_back(
        std::addressof(reference->getAssignment()));
    CompletionContext context{document,         params,     offset,
                              replaceOffset,     replaceEndOffset,
                              token.text,        prefix,     node,
                              reference,         referenceFeature};
    completionFor(context, [&accept, &context](const CompletionValue &value) {
      accept(context, value);
    });
  }

  if (items.empty() && offset > tokenOffset) {
    const auto suffixExpect = services.parser->expect(text, offset, cancelToken);
    const auto &suffixAlternatives = suffixExpect.frontier;
    for (const auto &feature : suffixAlternatives) {
      utils::throw_if_cancelled(cancelToken);
      CompletionContext context{document, params, offset, offset, offset,
                                {},          {},     node,   reference,
                                feature};
      completionFor(context, [&accept, &context](const CompletionValue &value) {
        accept(context, value);
      });
      if (!continueCompletion(context)) {
        break;
      }
    }
  }

  ::lsp::CompletionList completionList{};
  completionList.isIncomplete = true;
  utils::throw_if_cancelled(cancelToken);
  completionList.items = std::move(items);
  return std::optional<::lsp::CompletionList>{std::move(completionList)};
}

void DefaultCompletionProvider::completionFor(
    const CompletionContext &context,
    const CompletionAcceptor &acceptor) const {
  if (!context.feature.isValid()) {
    return;
  }

  if (const auto *assignment = context.feature.expectedReferenceAssignment();
      assignment != nullptr) {
    completionForReference(context, makeReferenceInfo(context, *assignment),
                           acceptor);
    return;
  }
  if (const auto *rule = context.feature.expectedRule(); rule != nullptr) {
    completionForRule(context, *rule, acceptor);
    return;
  }
  if (const auto *contextRule = context.feature.contextRule();
      contextRule != nullptr) {
    completionForRule(context, *contextRule, acceptor);
  }
  if (const auto *literal = context.feature.literal(); literal != nullptr) {
    completionForKeyword(context, *literal, acceptor);
  }
}

void DefaultCompletionProvider::completionForReference(
    const CompletionContext &context,
    const ReferenceInfo &reference,
    const CompletionAcceptor &acceptor) const {
  for (const auto *candidate : getReferenceCandidates(context, reference)) {
    auto value =
        createReferenceCompletionItem(context, reference, *candidate);
    if (!value.has_value()) {
      continue;
    }
    acceptor(std::move(*value));
  }
}

std::vector<const workspace::AstNodeDescription *>
DefaultCompletionProvider::getReferenceCandidates(
    const CompletionContext &context, const ReferenceInfo &reference) const {
  (void)context;
  const auto *scopeProvider = services.references.scopeProvider.get();
  auto allCandidates = reference;
  allCandidates.referenceText = {};
  std::vector<const workspace::AstNodeDescription *> candidates;
  const auto collectCandidate =
      [&candidates](const workspace::AstNodeDescription &candidate) {
        candidates.push_back(std::addressof(candidate));
        return true;
      };
  (void)scopeProvider->visitScopeEntries(
      allCandidates,
      utils::function_ref<bool(const workspace::AstNodeDescription &)>(
          collectCandidate));
  return candidates;
}

std::optional<CompletionValue>
DefaultCompletionProvider::createReferenceCompletionItem(
    const CompletionContext &context,
    const ReferenceInfo &reference,
    const workspace::AstNodeDescription &candidate) const {
  (void)context;
  (void)reference;
  assert(!candidate.name.empty());

  CompletionValue value;
  value.label = candidate.name;
  value.description = &candidate;
  value.sortText = "0";
  return value;
}

void DefaultCompletionProvider::completionForRule(
    const CompletionContext &context, const grammar::AbstractRule &rule,
    const CompletionAcceptor &acceptor) const {
  (void)context;
  if (rule.getName().empty()) {
    return;
  }

  CompletionValue value;
  value.label = rule.getName();
  value.newText = std::string("${1:") + std::string(rule.getName()) + "}";
  value.detail = "Rule";
  value.kind = ::lsp::CompletionItemKind::Snippet;
  value.insertTextFormat = ::lsp::InsertTextFormat::Snippet;
  acceptor(std::move(value));
}

void DefaultCompletionProvider::completionForKeyword(
    const CompletionContext &context, const grammar::Literal &keyword,
    const CompletionAcceptor &acceptor) const {
  if (!filterKeyword(context, keyword)) {
    return;
  }

  CompletionValue value;
  value.label = std::string(keyword.getValue());
  value.detail = "Keyword";
  value.kind = ::lsp::CompletionItemKind::Keyword;
  value.sortText = "2";
  acceptor(std::move(value));
}

bool DefaultCompletionProvider::filterKeyword(
    const CompletionContext &context, const grammar::Literal &keyword) const {
  (void)context;
  return !keyword.getValue().empty();
}

bool DefaultCompletionProvider::fillCompletionItem(
    const CompletionContext &context, const CompletionValue &value,
    ::lsp::CompletionItem &item) const {
  if (value.label.empty()) {
    return false;
  }
  const auto &fuzzyMatcher = *services.shared.lsp.fuzzyMatcher;

  const auto matchText =
      value.filterText.has_value() ? std::string_view(*value.filterText)
                                   : std::string_view(value.label);
  const auto insertText =
      value.newText.empty() ? std::string_view(value.label)
                            : std::string_view(value.newText);
  const auto textEdit = value.textEdit.has_value()
                            ? value.textEdit
                             : build_completion_text_edit(context, matchText,
                                                         insertText,
                                                         fuzzyMatcher);
  if (!textEdit.has_value()) {
    return false;
  }

  const auto &nodeKindProvider = *services.shared.lsp.nodeKindProvider;
  const auto &documentationProvider =
      *services.documentation.documentationProvider;
  const auto &documents = *services.shared.workspace.documents;
  const auto *featureLiteral = context.feature.literal();
  const auto *featureReference = context.feature.expectedReferenceAssignment();
  const auto *description = value.description;
  const auto *insertTextFormat = value.insertTextFormat.has_value()
                                     ? std::addressof(*value.insertTextFormat)
                                     : nullptr;

  item.label = value.label;
  item.textEdit = *textEdit;

  if (insertTextFormat != nullptr) {
    item.insertTextFormat = *insertTextFormat;
  }

  if (value.kind.has_value()) {
    item.kind = *value.kind;
  } else if (insertTextFormat != nullptr &&
             *insertTextFormat == ::lsp::InsertTextFormat::Snippet) {
    item.kind = ::lsp::CompletionItemKind::Snippet;
  } else if (description != nullptr) {
    item.kind = nodeKindProvider.getCompletionItemKind(*description);
  } else if (featureLiteral != nullptr) {
    item.kind = ::lsp::CompletionItemKind::Keyword;
  } else if (featureReference != nullptr) {
    item.kind = ::lsp::CompletionItemKind::Reference;
  }

  if (value.detail.has_value()) {
    item.detail = *value.detail;
  } else if (description != nullptr) {
    item.detail = provider_detail::display_type_name(description->type);
  } else if (featureLiteral != nullptr) {
    item.detail = "Keyword";
  }

  if (value.sortText.has_value()) {
    item.sortText = *value.sortText;
  } else {
    item.sortText = default_sort_text(context, value);
  }

  if (value.filterText.has_value()) {
    item.filterText = *value.filterText;
  }

  if (value.documentation.has_value()) {
    item.documentation = *value.documentation;
  } else if (description != nullptr) {
    const auto &node = workspace::resolve_ast_node(documents, *description);
    if (auto documentation = documentationProvider.getDocumentation(node);
        documentation.has_value() && !documentation->empty()) {
      ::lsp::MarkupContent markup{};
      markup.kind = ::lsp::MarkupKind::Markdown;
      markup.value = *documentation;
      item.documentation = std::move(markup);
    }
  }

  return true;
}

bool DefaultCompletionProvider::continueCompletion(
    const CompletionContext &context) const {
  (void)context;
  return true;
}

ReferenceInfo
DefaultCompletionProvider::makeReferenceInfo(
    const CompletionContext &context,
    const grammar::Assignment &assignment) const {
  if (context.reference != nullptr &&
      std::addressof(context.reference->getAssignment()) ==
          std::addressof(assignment)) {
    auto reference = pegium::makeReferenceInfo(*context.reference);
    reference.referenceText = context.prefix;
    return reference;
  }
  return ReferenceInfo{context.node, context.prefix, assignment};
}

} // namespace pegium
