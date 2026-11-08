#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/formatting/FormattingAction.hpp"

// The vocabulary a rule is written in: the action builders, and the options
// of the compound shapes every language repeats. `AbstractFormatter` exposes
// them as members, so a rule reads `oneSpace`, not `cursiv::oneSpace`.

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

}  // namespace cursiv
