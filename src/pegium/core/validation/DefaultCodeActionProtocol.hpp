#pragma once

#include <string_view>

namespace pegium::validation {

/// Wire schema for the diagnostic-embedded default quick-fix protocol. The
/// producer (DefaultDocumentValidator) writes an array of edit objects under
/// `kDefaultCodeActionsKey` in a diagnostic's `data`, and the consumer
/// (DefaultCodeActionProvider) reads them back. Both sides reference these
/// names so the schema has a single source of truth.
inline constexpr std::string_view kDefaultCodeActionsKey =
    "pegiumDefaultCodeActions";
inline constexpr std::string_view kCodeActionKindField = "kind";
inline constexpr std::string_view kCodeActionEditKindField = "editKind";
inline constexpr std::string_view kCodeActionTitleField = "title";
inline constexpr std::string_view kCodeActionBeginField = "begin";
inline constexpr std::string_view kCodeActionEndField = "end";
inline constexpr std::string_view kCodeActionNewTextField = "newText";
inline constexpr std::string_view kQuickFixActionKind = "quickfix";

} // namespace pegium::validation
