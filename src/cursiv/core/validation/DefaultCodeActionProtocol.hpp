#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/text/TextRange.hpp"

// The wire schema of the default quick fixes: edit objects under
// `kDefaultCodeActionsKey`, written into a diagnostic's `data` by the
// validator or computed on demand for it (`spelling_actions`), and read back
// by the code-action provider. Both sides spell the names from here, so the
// schema has one source of truth.
//
// It travels in `data` and not the message: a quick fix that re-parsed the
// prose would break on the first reworded sentence.

namespace cursiv {

inline constexpr std::string_view kDefaultCodeActionsKey =
    "cursivDefaultCodeActions";
inline constexpr std::string_view kKindField = "kind";
inline constexpr std::string_view kEditKindField = "editKind";
inline constexpr std::string_view kTitleField = "title";
inline constexpr std::string_view kBeginField = "begin";
inline constexpr std::string_view kEndField = "end";
inline constexpr std::string_view kNewTextField = "newText";
// The text revision begin/end were computed against: a client may hold a
// diagnostic across further edits, and the consumer would otherwise apply
// stale offsets to different text. The revision and not the client's version,
// so a version stamped on unchanged bytes keeps the rows.
inline constexpr std::string_view kRevisionField = "revision";
inline constexpr std::string_view kQuickFixKind = "quickfix";
// The two edit kinds.
inline constexpr std::string_view kInsertEdit = "insert";
inline constexpr std::string_view kReplaceEdit = "replace";

// One edit row, the one shape every writer emits.
[[nodiscard]] JsonValue make_action_row(std::string_view editKind,
                                        std::string title, TextRange range,
                                        std::string newText,
                                        std::uint64_t revision);

}  // namespace cursiv
