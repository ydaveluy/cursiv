#include <pegium/lsp/navigation/DefaultDocumentHighlightProvider.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>
#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/syntax-tree/CstUtils.hpp>

#include <algorithm>
#include <utility>

#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>

namespace pegium {
using namespace pegium::provider_detail;

std::vector<::lsp::DocumentHighlight>
DefaultDocumentHighlightProvider::getDocumentHighlight(
    const workspace::Document &document,
    const ::lsp::DocumentHighlightParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto offset = document.textDocument().offsetAt(params.position);
  const auto &references = *services.references.references;
  const auto &nameProvider = *services.references.nameProvider;

  std::vector<::lsp::DocumentHighlight> highlights;
  utils::TransparentStringSet seen;

  for (const auto *target :
       find_declarations_at_offset(document, offset, references)) {
    const auto &targetDocument = getDocument(*target);
    const auto includeDeclaration = targetDocument.id == document.id;
    const auto declarationNode =
        references::required_declaration_site_node(*target, nameProvider);
    const auto declarationBegin = declarationNode.getBegin();
    const auto declarationLength =
        declarationNode.getEnd() - declarationNode.getBegin();

    for (const auto &reference : references.findReferences(
             *target, {.documentId = document.id,
                       .includeDeclaration = includeDeclaration})) {
      utils::throw_if_cancelled(cancelToken);
      const auto kind =
          includeDeclaration && reference.sourceDocumentId == document.id &&
                  reference.sourceOffset == declarationBegin &&
                  reference.sourceLength == declarationLength
              ? ::lsp::DocumentHighlightKind::Write
              : ::lsp::DocumentHighlightKind::Read;
      DocumentHighlightData key{
          .begin = reference.sourceOffset,
          .end = reference.sourceOffset + reference.sourceLength,
          .kind = kind,
      };
      if (seen.insert(document_highlight_key(key)).second) {
        ::lsp::DocumentHighlight highlight{};
        highlight.range = offset_to_range(document, key.begin, key.end);
        highlight.kind = key.kind;
        highlights.push_back(std::move(highlight));
      }
    }
  }

  std::ranges::sort(highlights, [](const ::lsp::DocumentHighlight &left,
                                   const ::lsp::DocumentHighlight &right) {
    if (left.range.start.line == right.range.start.line) {
      return left.range.start.character < right.range.start.character;
    }
    return left.range.start.line < right.range.start.line;
  });
  return highlights;
}

} // namespace pegium
