#include <pegium/lsp/support/LspProviderUtils.hpp>

#include <cassert>
#include <algorithm>
#include <string>

#include <pegium/core/grammar/AbstractRule.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/Create.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/grammar/Nest.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/syntax-tree/AbstractReference.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/syntax-tree/CstUtils.hpp>
#include <pegium/core/utils/TextUtils.hpp>
#include <pegium/core/utils/UriUtils.hpp>
#include <pegium/core/workspace/AstDescriptions.hpp>

namespace pegium::provider_detail {

namespace {

bool has_newline_between(std::string_view text, TextOffset begin,
                         TextOffset end) {
  const auto textSize = static_cast<TextOffset>(text.size());
  const auto safeBegin = std::ranges::min(begin, textSize);
  const auto safeEnd = std::ranges::min(end, textSize);
  if (safeEnd <= safeBegin) {
    return false;
  }
  return text.substr(safeBegin, safeEnd - safeBegin).find('\n') !=
         std::string_view::npos;
}

struct SelectionRangeSegment {
  TextOffset begin = 0;
  TextOffset end = 0;
};

void append_if_distinct(std::vector<SelectionRangeSegment> &chain,
                        SelectionRangeSegment range) {
  if (!chain.empty() && chain.back().begin == range.begin &&
      chain.back().end == range.end) {
    return;
  }
  chain.push_back(std::move(range));
}

bool collect_node_chain_for_offset(const CstNodeView &node, TextOffset offset,
                                   std::vector<SelectionRangeSegment> &chain) {
  if (node.isHidden()) {
    return false;
  }
  if (offset < node.getBegin() || offset > node.getEnd()) {
    return false;
  }

  for (const auto &child : node) {
    if (collect_node_chain_for_offset(child, offset, chain)) {
      append_if_distinct(chain, {.begin = node.getBegin(), .end = node.getEnd()});
      return true;
    }
  }

  append_if_distinct(chain, {.begin = node.getBegin(), .end = node.getEnd()});
  return true;
}

} // namespace

::lsp::Range offset_to_range(const workspace::TextDocument &document,
                             TextOffset begin, TextOffset end) {
  ::lsp::Range range{};
  range.start = document.positionAt(begin);
  range.end = document.positionAt(end >= begin ? end : begin);
  return range;
}

::lsp::Range offset_to_range(const workspace::Document &document,
                             TextOffset begin, TextOffset end) {
  return offset_to_range(document.textDocument(), begin, end);
}

TokenSpan token_at(std::string_view text, TextOffset offset) {
  const auto size = static_cast<TextOffset>(text.size());
  const char *const data = text.data();
  const char *const textEnd = data + size;
  const auto isWordAt = [&](TextOffset position) noexcept {
    return position < size &&
           utils::is_identifier_like_codepoint_at(data + position, textEnd);
  };
  const auto previousStart = [&](TextOffset position) noexcept {
    return static_cast<TextOffset>(utils::previous_codepoint_start(text, position));
  };
  const auto afterCodepoint = [&](TextOffset position) noexcept {
    return position +
           static_cast<TextOffset>(utils::utf8_codepoint_length(text[position]));
  };

  TextOffset cursor = std::ranges::min(offset, size);
  // When the cursor sits just past an identifier (at the buffer end or on a
  // non-identifier codepoint), step back onto the last identifier codepoint.
  if (!isWordAt(cursor) && cursor > 0 && isWordAt(previousStart(cursor))) {
    cursor = previousStart(cursor);
  }
  if (!isWordAt(cursor)) {
    return {};
  }

  TextOffset begin = cursor;
  while (begin > 0 && isWordAt(previousStart(begin))) {
    begin = previousStart(begin);
  }
  TextOffset end = afterCodepoint(cursor);
  while (isWordAt(end)) {
    end = afterCodepoint(end);
  }

  return {begin, end, text.substr(begin, end - begin)};
}

std::string display_type_name(std::type_index type) {
  if (type == std::type_index(typeid(void))) {
    return {};
  }
  return parser::detail::runtime_type_name(type);
}

std::string location_key(const LocationData &location) {
  return std::to_string(location.documentId) + "#" +
         std::to_string(location.begin) + ":" + std::to_string(location.end);
}

LocationData to_location(const workspace::ReferenceDescription &reference) {
  return {.documentId = reference.sourceDocumentId,
          .begin = reference.sourceOffset,
          .end = reference.sourceOffset + reference.sourceLength};
}

std::vector<const AstNode *>
find_declarations_at_offset(const workspace::Document &document,
                            TextOffset offset,
                            const references::References &references) {
  if (document.parseResult.cst == nullptr) {
    return {};
  }
  if (const auto selectedNode =
          find_declaration_node_at_offset(*document.parseResult.cst, offset);
      selectedNode.has_value()) {
    return references.findDeclarations(*selectedNode);
  }
  return {};
}

const AbstractReference *
find_reference_at_offset(const workspace::Document &document,
                         TextOffset offset) {
  const AbstractReference *best = nullptr;
  TextOffset bestSpan = std::numeric_limits<TextOffset>::max();
  for (const auto &handle : document.parseResult.references) {
    const auto &reference = *handle.getConst();
    const auto refNode = reference.getRefNode();
    if (!refNode.valid()) {
      continue;
    }
    if (offset < refNode.getBegin() || offset > refNode.getEnd()) {
      continue;
    }
    if (const auto span = refNode.getEnd() - refNode.getBegin();
        best == nullptr || span < bestSpan) {
      best = &reference;
      bestSpan = span;
    }
  }
  return best;
}

::lsp::LocationLink to_location_link(
    const workspace::Document &sourceDocument, const CstNodeView &originNode,
    const AstNode &targetDeclaration,
    const references::NameProvider &nameProvider) {
  const auto &sourceTextDocument = sourceDocument.textDocument();

  const auto originSelectionRange =
      offset_to_range(sourceTextDocument, originNode.getBegin(),
                      originNode.getEnd());

  const auto &targetDocument = getDocument(targetDeclaration);
  const auto &targetTextDocument = targetDocument.textDocument();

  // Target selection range: the declaration name; target range: the full
  // declaration node so a peek preview shows the whole declaration.
  const auto nameNode =
      required_declaration_site_node(targetDeclaration, nameProvider);
  const auto fullNode = targetDeclaration.hasCstNode()
                            ? targetDeclaration.getCstNode()
                            : nameNode;

  ::lsp::LocationLink link{};
  link.targetUri = ::lsp::Uri::parse(targetDocument.uri);
  link.targetRange =
      offset_to_range(targetTextDocument, fullNode.getBegin(),
                      fullNode.getEnd());
  link.targetSelectionRange =
      offset_to_range(targetTextDocument, nameNode.getBegin(),
                      nameNode.getEnd());
  link.originSelectionRange = originSelectionRange;
  return link;
}

std::optional<::lsp::WorkspaceEdit>
to_lsp_workspace_edit(const WorkspaceEditData &workspaceEdit,
                      const pegium::SharedServices &sharedServices,
                      const utils::CancellationToken &cancelToken) {
  ::lsp::WorkspaceEdit lspWorkspaceEdit{};
  ::lsp::Map<::lsp::DocumentUri, ::lsp::Array<::lsp::TextEdit>> changes;

  for (const auto &[documentId, edits] : workspaceEdit.changes) {
    utils::throw_if_cancelled(cancelToken);
    // The document may have been removed from the store after the edit data was
    // computed (the ids come from an index-derived snapshot). Skip such stale
    // targets instead of dereferencing a null document.
    const auto document = sharedServices.workspace.documents->getDocument(documentId);
    if (document == nullptr) {
      continue;
    }
    const auto &textDocument = document->textDocument();

    auto &lspEdits = changes[::lsp::Uri::parse(document->uri)];
    lspEdits.reserve(edits.size());
    for (const auto &edit : edits) {
      utils::throw_if_cancelled(cancelToken);
      ::lsp::TextEdit lspEdit{};
      lspEdit.range = offset_to_range(textDocument, edit.begin, edit.end);
      lspEdit.newText = edit.newText;
      lspEdits.push_back(std::move(lspEdit));
    }
  }

  if (changes.empty()) {
    return std::nullopt;
  }

  lspWorkspaceEdit.changes = std::move(changes);
  return lspWorkspaceEdit;
}

std::string document_highlight_key(const DocumentHighlightData &highlight) {
  return std::to_string(highlight.begin) + ":" + std::to_string(highlight.end);
}

void collect_folding_ranges(const CstNodeView &node, std::string_view text,
                            std::vector<FoldingRangeData> &ranges,
                            utils::TransparentStringSet &seen) {
  const auto push = [&](TextOffset begin, TextOffset end,
                        const ::lsp::FoldingRangeKindEnum &kind) {
    if (end <= begin || !has_newline_between(text, begin, end)) {
      return;
    }
    const auto key = std::to_string(begin) + ":" + std::to_string(end);
    if (seen.insert(key).second) {
      ranges.push_back({.begin = begin, .end = end, .kind = kind});
    }
  };

  if (node.isHidden()) {
    // Ignored trivia (whitespace) never reaches the CST, so a hidden terminal
    // is a comment; one that spans several lines folds as a comment range.
    if (const auto *grammarElement = node.getGrammarElement();
        grammarElement != nullptr &&
        grammarElement->getKind() == grammar::ElementKind::TerminalRule) {
      push(node.getBegin(), node.getEnd(), ::lsp::FoldingRangeKind::Comment);
    }
    return;
  }

  push(node.getBegin(), node.getEnd(), ::lsp::FoldingRangeKind::Region);

  for (const auto &child : node) {
    collect_folding_ranges(child, text, ranges, seen);
  }
}

::lsp::SelectionRange compute_selection_range(const workspace::Document &document,
                                              TextOffset offset) {
  const auto &textDocument = document.textDocument();
  std::vector<SelectionRangeSegment> chain;
  if (const auto token = token_at(textDocument.getText(), offset);
      !token.text.empty()) {
    append_if_distinct(chain, {.begin = token.begin, .end = token.end});
  }

  if (document.parseResult.cst != nullptr) {
    for (const auto &child : *document.parseResult.cst) {
      if (collect_node_chain_for_offset(child, offset, chain)) {
        break;
      }
    }
  }

  ::lsp::SelectionRange selection{};
  if (chain.empty()) {
    ::lsp::Range range{};
    range.start = textDocument.positionAt(offset);
    range.end = textDocument.positionAt(offset);
    selection.range = range;
    return selection;
  }

  ::lsp::Range firstRange{};
  firstRange.start = textDocument.positionAt(chain.front().begin);
  firstRange.end = textDocument.positionAt(chain.front().end);
  selection.range = firstRange;
  auto *current = &selection;
  for (std::size_t index = 1; index < chain.size(); ++index) {
    current->parent = std::make_unique<::lsp::SelectionRange>();
    current = current->parent.get();
    ::lsp::Range range{};
    range.start = textDocument.positionAt(chain[index].begin);
    range.end = textDocument.positionAt(chain[index].end);
    current->range = range;
  }
  return selection;
}

} // namespace pegium::provider_detail
