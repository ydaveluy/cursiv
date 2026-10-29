#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/lsp/formatting/FormattingAction.hpp"

// The vocabulary a rule is written in, and the compound shapes every
// language repeats. None of them names a grammar, so they compile once for
// the tree. They live at namespace scope; `AbstractFormatter` re-exposes
// them as members so a rule reads `oneSpace`, not `cursiv::oneSpace`.

namespace cursiv {

struct SpaceActionBuilder {
  std::int32_t count = 0;
  [[nodiscard]] FormattingAction operator()(
      FormattingActionOptions options = {}) const {
    return FormattingAction{.options = options,
                            .moves = {FormattingMove{.characters = count}}};
  }
  [[nodiscard]] operator FormattingAction() const { return operator()({}); }
};

struct LineActionBuilder {
  std::int32_t count = 0;
  [[nodiscard]] FormattingAction operator()(
      FormattingActionOptions options = {}) const {
    return FormattingAction{.options = options,
                            .moves = {FormattingMove{.lines = count}}};
  }
  [[nodiscard]] operator FormattingAction() const { return operator()({}); }
};

// `indent` is a break and a level at once: the break puts the child on its own
// line and the level travels down its whole subtree.
struct IndentActionBuilder {
  [[nodiscard]] FormattingAction operator()(
      FormattingActionOptions options = {}) const {
    return FormattingAction{.options = options,
                            .moves = {FormattingMove{.tabs = 1, .lines = 1}}};
  }
  [[nodiscard]] operator FormattingAction() const { return operator()({}); }
};

struct NoIndentActionBuilder {
  [[nodiscard]] FormattingAction operator()(
      FormattingActionOptions options = {}) const {
    return FormattingAction{.options = options,
                            .moves = {FormattingMove{.tabs = 0}}};
  }
  [[nodiscard]] operator FormattingAction() const { return operator()({}); }
};

// An action with no move leaves the gap alone, and it is a real answer rather
// than an absent rule: a compound shape whose default would touch a boundary
// the language wants untouched says so by naming it.
struct KeepActionBuilder {
  [[nodiscard]] FormattingAction operator()(
      FormattingActionOptions options = {}) const {
    return FormattingAction{.options = options, .moves = {}};
  }
  [[nodiscard]] operator FormattingAction() const { return operator()({}); }
};

// The named instances and the compound shapes live as members of
// `AbstractFormatter`; only what a member is built from lives here.
[[nodiscard]] FormattingAction make_spaces(
    std::int32_t count, FormattingActionOptions options = {});
[[nodiscard]] FormattingAction make_new_lines(
    std::int32_t count, FormattingActionOptions options = {});
[[nodiscard]] FormattingAction fit_actions(
    std::initializer_list<FormattingAction> actions);

struct BlockFormatOptions {
  std::optional<FormattingAction> beforeOpen = std::nullopt;
  std::optional<FormattingAction> insideEmpty = std::nullopt;
  std::optional<FormattingAction> afterOpen = std::nullopt;
  std::optional<FormattingAction> beforeClose = std::nullopt;
  std::optional<FormattingAction> contentIndent = std::nullopt;
};

struct SeparatedListFormatOptions {
  std::optional<FormattingAction> beforeSeparator = std::nullopt;
  std::optional<FormattingAction> afterSeparator = std::nullopt;
};

struct LineCommentFormatOptions {
  std::string start = "//";
  bool normalizeWhitespace = true;
  bool ensureSpaceAfterStart = true;
};

// Javadoc by default: the `start` and `end` below, ` *` per line, `@tag`
// continuation.
struct MultilineCommentFormatOptions {
  std::string start = "/**";
  std::string end = "*/";
  std::string newLineStart = " *";
  std::string tagContinuation = "  ";
  std::optional<std::string> tagStart = "@";
  bool normalizeWhitespace = true;
  bool forceMultiline = false;
  std::size_t maxBlankLines = 1;
  // Empty reads the style off the comment itself, which says nothing when the
  // comment is being made multi-line for the first time.
  std::string_view newlineStyle = {};
};

// The compound shapes, called through members of `AbstractFormatter` --
// `formatBlock` and the three others -- a language author never spells
// these directly.
void do_format_block(FormattingRegion open, FormattingRegion close,
                     FormattingRegion content,
                     const BlockFormatOptions& options);

void do_format_separated_list(FormattingRegion separators,
                              const SeparatedListFormatOptions& options);

[[nodiscard]] std::string do_format_line_comment(
    std::string_view text, const LineCommentFormatOptions& options);

[[nodiscard]] std::string do_format_multiline_comment(
    std::string_view text, const MultilineCommentFormatOptions& options);

}  // namespace cursiv
