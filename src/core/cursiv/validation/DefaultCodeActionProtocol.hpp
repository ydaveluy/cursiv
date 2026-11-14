#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/JsonValue.hpp"

// The wire schema of the default quick fixes: edit objects under
// `kDefaultCodeActionsKey` in a diagnostic's `data`, written by the validator
// or `spelling_actions` and read by the code-action provider.

namespace cursiv::detail {

inline constexpr std::string_view kDefaultCodeActionsKey =
    "cursivDefaultCodeActions";
inline constexpr std::string_view kKindField = "kind";
inline constexpr std::string_view kEditKindField = "editKind";
inline constexpr std::string_view kTitleField = "title";
inline constexpr std::string_view kBeginField = "begin";
inline constexpr std::string_view kEndField = "end";
inline constexpr std::string_view kNewTextField = "newText";
// The text revision the offsets were computed against, so stale offsets are
// never applied to changed text. Not the client's version, which can change
// on the same bytes.
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

}  // namespace cursiv::detail
