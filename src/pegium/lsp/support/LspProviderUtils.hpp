#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_set>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Literal.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>
#include <pegium/core/workspace/IndexManager.hpp>
#include <pegium/core/workspace/TextDocument.hpp>

namespace pegium {
struct AstNode;
class AbstractReference;
} // namespace pegium

namespace pegium::workspace {
class Document;
} // namespace pegium::workspace

namespace pegium::references {
class References;
class NameProvider;
} // namespace pegium::references

namespace pegium::provider_detail {

/// Lightweight source location used by provider helper code.
struct LocationData {
  workspace::DocumentId documentId = workspace::InvalidDocumentId;
  TextOffset begin = 0;
  TextOffset end = 0;
};

/// One text edit expressed with workspace offsets.
struct WorkspaceTextEdit {
  TextOffset begin = 0;
  TextOffset end = 0;
  std::string newText;
};

/// Aggregated workspace edit data before LSP conversion.
struct WorkspaceEditData {
  std::unordered_map<workspace::DocumentId, std::vector<WorkspaceTextEdit>> changes;

  [[nodiscard]] bool empty() const noexcept { return changes.empty(); }
};

/// Token slice around one offset in raw source text.
struct TokenSpan {
  TextOffset begin = 0;
  TextOffset end = 0;
  std::string_view text{};
};

/// Highlight range data before LSP conversion.
struct DocumentHighlightData {
  TextOffset begin = 0;
  TextOffset end = 0;
  ::lsp::DocumentHighlightKind kind = ::lsp::DocumentHighlightKind::Text;
};

/// Folding range data before LSP conversion.
struct FoldingRangeData {
  TextOffset begin = 0;
  TextOffset end = 0;
  ::lsp::FoldingRangeKind kind = ::lsp::FoldingRangeKind::Region;
};

/// Builds an LSP range spanning `[begin, end)` of `document`. The end offset is
/// clamped to `begin` when it precedes it, so the range is never inverted.
[[nodiscard]] ::lsp::Range offset_to_range(const workspace::TextDocument &document,
                                           TextOffset begin, TextOffset end);

/// Builds an LSP range spanning `[begin, end)` of `document`'s text document.
[[nodiscard]] ::lsp::Range offset_to_range(const workspace::Document &document,
                                           TextOffset begin, TextOffset end);

/// Returns the token covering `offset` in `text`.
[[nodiscard]] TokenSpan token_at(std::string_view text, TextOffset offset);

/// Returns a readable name for one runtime type.
[[nodiscard]] std::string display_type_name(std::type_index type);

/// Builds a deduplication key for one source location.
[[nodiscard]] std::string location_key(const LocationData &location);
/// Converts an indexed reference description to a source location.
[[nodiscard]] LocationData
to_location(const workspace::ReferenceDescription &reference);

/// Returns every declaration AST node reachable from the declaration site at
/// `offset` in `document`.
[[nodiscard]] std::vector<const AstNode *>
find_declarations_at_offset(const workspace::Document &document,
                            TextOffset offset,
                            const references::References &references);

/// Returns the smallest reference whose node contains `offset`, or `nullptr`.
[[nodiscard]] const AbstractReference *
find_reference_at_offset(const workspace::Document &document, TextOffset offset);

/// Builds an LSP location link from a source range to a target declaration.
///
/// `originNode` is the source selection (a cross-reference node spans the whole
/// qualified name). `targetRange` spans the full declaration node (peek
/// preview) and `targetSelectionRange` spans its name.
[[nodiscard]] ::lsp::LocationLink
to_location_link(const workspace::Document &sourceDocument,
                 const CstNodeView &originNode,
                 const AstNode &targetDeclaration,
                 const references::NameProvider &nameProvider);

/// Converts buffered workspace edits to an LSP workspace edit.
[[nodiscard]] std::optional<::lsp::WorkspaceEdit>
to_lsp_workspace_edit(const WorkspaceEditData &workspaceEdit,
                      const pegium::SharedServices &sharedServices,
                      const utils::CancellationToken &cancelToken =
                          utils::default_cancel_token);

/// Builds a deduplication key for one document highlight.
[[nodiscard]] std::string
document_highlight_key(const DocumentHighlightData &highlight);

/// Collects folding ranges rooted at `node`.
void collect_folding_ranges(const CstNodeView &node, std::string_view text,
                            std::vector<FoldingRangeData> &ranges,
                            utils::TransparentStringSet &seen);

/// Computes the nested selection range chain at `offset`.
[[nodiscard]] ::lsp::SelectionRange
compute_selection_range(const workspace::Document &document, TextOffset offset);

} // namespace pegium::provider_detail
