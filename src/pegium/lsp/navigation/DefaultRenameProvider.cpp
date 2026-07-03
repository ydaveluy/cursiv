#include <pegium/lsp/navigation/DefaultRenameProvider.hpp>
#include <pegium/lsp/support/LspProviderUtils.hpp>

#include <algorithm>
#include <string>
#include <utility>

#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>

namespace pegium {
using namespace pegium::provider_detail;



std::optional<::lsp::WorkspaceEdit> DefaultRenameProvider::rename(
    const workspace::Document &document, const ::lsp::RenameParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto offset = document.textDocument().offsetAt(params.position);
  const auto newName = std::string_view(params.newName);
  const auto &referencesService = *services.references.references;
  if (newName.empty()) {
    return std::nullopt;
  }
  const auto declarations =
      find_declarations_at_offset(document, offset, referencesService);
  if (declarations.empty()) {
    return std::nullopt;
  }

  WorkspaceEditData edit;
  utils::TransparentStringSet seen;
  for (const auto *target : declarations) {
    for (const auto &reference : referencesService.findReferences(
             *target, {.includeDeclaration = true})) {
      utils::throw_if_cancelled(cancelToken);
      const auto location = to_location(reference);
      if (!seen.insert(location_key(location)).second) {
        continue;
      }
      edit.changes[location.documentId].push_back(
          {.begin = location.begin,
           .end = location.end,
           .newText = std::string(newName)});
    }
  }

  for (auto &[documentId, edits] : edit.changes) {
    utils::throw_if_cancelled(cancelToken);
    (void)documentId;
    std::ranges::sort(edits, [](const WorkspaceTextEdit &left,
                                const WorkspaceTextEdit &right) {
      return left.begin > right.begin;
    });
  }

  if (edit.empty()) {
    return std::nullopt;
  }
  return to_lsp_workspace_edit(edit, services.shared,
                               cancelToken);
}

std::optional<::lsp::PrepareRenameResult> DefaultRenameProvider::prepareRename(
    const workspace::Document &document,
    const ::lsp::TextDocumentPositionParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto &referencesService = *services.references.references;
  const auto &textDocument = document.textDocument();
  const auto offset = textDocument.offsetAt(params.position);
  const auto token = token_at(textDocument.getText(), offset);
  if (token.text.empty()) {
    return std::nullopt;
  }
  if (find_declarations_at_offset(document, offset, referencesService).empty()) {
    return std::nullopt;
  }

  // Report the same span rename() will edit. A reference parsed through a
  // datatype rule (e.g. a dotted qualified name a.b.c) spans the whole reference
  // node, not just the token under the cursor; widen to it so the rename UI
  // extent matches the applied edit (rename() edits the whole reference node).
  auto begin = token.begin;
  auto end = token.end;
  if (const auto *reference = find_reference_at_offset(document, offset)) {
    if (const auto refNode = reference->getRefNode(); refNode.valid()) {
      begin = refNode.getBegin();
      end = refNode.getEnd();
    }
  }

  ::lsp::Range range = offset_to_range(textDocument, begin, end);
  ::lsp::PrepareRenameResult result{};
  result = std::move(range);
  return result;
}

} // namespace pegium
