#include "cursiv/lsp/formatting/FormattingRules.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/lsp/formatting/FormattingAction.hpp"
#include "cursiv/lsp/formatting/LineTerminator.hpp"

namespace cursiv {

namespace {

// The same five the base exposes to a language author, spelled here for the
// compound shapes' own defaults.
constexpr SpaceActionBuilder noSpace{0};
constexpr SpaceActionBuilder oneSpace{1};
constexpr LineActionBuilder newLine{1};
constexpr IndentActionBuilder indent{};

}  // namespace

namespace {

[[nodiscard]] std::string_view trim_horizontal(std::string_view text) noexcept {
  while (!text.empty() && is_horizontal_whitespace(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && is_horizontal_whitespace(text.back()))
    text.remove_suffix(1);
  return text;
}

[[nodiscard]] std::string compact_whitespace(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  bool pendingSpace = false;
  for (const char ch : text) {
    if (is_horizontal_whitespace(ch)) {
      pendingSpace = !result.empty();
      continue;
    }
    if (pendingSpace) {
      result.push_back(' ');
      pendingSpace = false;
    }
    result.push_back(ch);
  }
  return result;
}

using MultilineOptions = MultilineCommentFormatOptions;

[[nodiscard]] std::string_view strip_line_prefix(
    std::string_view line, const MultilineOptions& options) noexcept {
  line = trim_horizontal(line);
  if (const std::string_view prefix = trim_horizontal(options.newLineStart);
      !prefix.empty() && line.starts_with(prefix)) {
    line.remove_prefix(prefix.size());
    if (!line.empty() && is_horizontal_whitespace(line.front()))
      line.remove_prefix(1);
  }
  return trim_horizontal(line);
}

[[nodiscard]] std::string normalize_line(std::string_view line,
                                         const MultilineOptions& options) {
  return options.normalizeWhitespace ? compact_whitespace(line)
                                     : std::string(line);
}

[[nodiscard]] bool is_tag_line(std::string_view line,
                               const MultilineOptions& options) noexcept {
  return options.tagStart.has_value() && !options.tagStart->empty() &&
         line.starts_with(*options.tagStart);
}

[[nodiscard]] std::string normalize_tag_line(std::string_view line,
                                             const MultilineOptions& options) {
  line = trim_horizontal(line);
  if (!is_tag_line(line, options)) return normalize_line(line, options);
  const std::size_t separator = line.find_first_of(" \t");
  if (separator == std::string_view::npos) return std::string(line);
  const std::string_view tag = line.substr(0, separator);
  const std::string rest = normalize_line(line.substr(separator + 1), options);
  if (rest.empty()) return std::string(tag);
  return std::string(tag) + " " + rest;
}

[[nodiscard]] std::string derive_end_prefix(std::string_view newLineStart) {
  const std::string_view original = newLineStart;
  while (!newLineStart.empty() && is_horizontal_whitespace(newLineStart.back()))
    newLineStart.remove_suffix(1);
  const std::string_view trimmed = trim_horizontal(newLineStart);
  if (trimmed.empty()) return std::string(original);
  std::string prefix(newLineStart);
  if (prefix.ends_with(trimmed)) prefix.erase(prefix.size() - trimmed.size());
  return prefix;
}

[[nodiscard]] std::string_view unwrap_body(
    std::string_view text, const MultilineOptions& options) noexcept {
  if (!text.starts_with(options.start) || !text.ends_with(options.end) ||
      text.size() < options.start.size() + options.end.size())
    return {};
  text.remove_prefix(options.start.size());
  text.remove_suffix(options.end.size());
  return text;
}

void append_prefixed(std::string& result, std::string_view prefix,
                     std::string_view text) {
  result.append(prefix);
  if (text.empty()) return;
  if (!prefix.empty() && !is_horizontal_whitespace(prefix.back()))
    result.push_back(' ');
  result.append(text);
}

enum class CommentLineKind : std::uint8_t { Text, Blank, Tag, TagContinuation };

struct CommentLine {
  CommentLineKind kind = CommentLineKind::Text;
  std::string text = {};
};

// Moves are ordered from tightest to loosest so `find_fitting_move` can stop at
// the first candidate the gap already fits: lines dominate, then indentation,
// then inline spacing.
[[nodiscard]] int compare_moves(const FormattingMove& left,
                                const FormattingMove& right) noexcept {
  if (const auto l = left.lines.value_or(0), r = right.lines.value_or(0);
      l != r)
    return l < r ? -1 : 1;
  if (const auto l = left.tabs.value_or(0), r = right.tabs.value_or(0); l != r)
    return l < r ? -1 : 1;
  if (const auto l = left.characters.value_or(0),
      r = right.characters.value_or(0);
      l != r)
    return l < r ? -1 : 1;
  return 0;
}

}  // namespace

FormattingAction fit_actions(std::initializer_list<FormattingAction> actions) {
  FormattingAction combined{};
  for (const FormattingAction& action : actions)
    combined.moves.insert(combined.moves.end(), action.moves.begin(),
                          action.moves.end());
  std::ranges::sort(combined.moves,
                    [](const FormattingMove& l, const FormattingMove& r) {
                      return compare_moves(l, r) < 0;
                    });
  return combined;
}

FormattingAction make_spaces(std::int32_t count,
                             FormattingActionOptions options) {
  return FormattingAction{.options = options,
                          .moves = {FormattingMove{.characters = count}}};
}

FormattingAction make_new_lines(std::int32_t count,
                                FormattingActionOptions options) {
  return FormattingAction{.options = options,
                          .moves = {FormattingMove{.lines = count}}};
}

void do_format_block(FormattingRegion open, FormattingRegion close,
                     FormattingRegion content,
                     const BlockFormatOptions& options) {
  if (open.empty() || close.empty()) return;

  const FormattingAction beforeOpen = options.beforeOpen.value_or(oneSpace);
  const FormattingAction insideEmpty = options.insideEmpty.value_or(noSpace);
  const FormattingAction afterOpen = options.afterOpen.value_or(newLine);
  const FormattingAction beforeClose = options.beforeClose.value_or(newLine);
  const FormattingAction contentIndent = options.contentIndent.value_or(indent);

  // An empty block stays on its line: `{}` is a shape, not a block that lost
  // its content, and breaking it would be the formatter inventing structure.
  if (content.empty()) {
    open.prepend(beforeOpen).append(insideEmpty);
    close.prepend(insideEmpty);
    return;
  }

  open.prepend(beforeOpen).append(afterOpen);
  close.prepend(beforeClose);
  content.prepend(contentIndent);
}

void do_format_separated_list(FormattingRegion separators,
                              const SeparatedListFormatOptions& options) {
  if (separators.empty()) return;
  separators.prepend(options.beforeSeparator.value_or(noSpace))
      .append(options.afterSeparator.value_or(oneSpace));
}

std::string do_format_line_comment(std::string_view text,
                                   const LineCommentFormatOptions& options) {
  if (!text.starts_with(options.start)) return std::string(text);
  const std::string_view body =
      trim_horizontal(text.substr(options.start.size()));
  const std::string normalized = options.normalizeWhitespace
                                     ? compact_whitespace(body)
                                     : std::string(body);
  if (normalized.empty()) return options.start;

  std::string result = options.start;
  if (options.ensureSpaceAfterStart &&
      (result.empty() || !is_horizontal_whitespace(result.back())))
    result.push_back(' ');
  result += normalized;
  return result;
}

std::string do_format_multiline_comment(
    std::string_view text, const MultilineCommentFormatOptions& options) {
  const std::string_view body = unwrap_body(text, options);
  // Not this comment's shape: a `/* ... */` asked to be formatted as a `/** */`
  // is left exactly as written rather than half-converted.
  if (body.data() == nullptr) return std::string(text);

  const bool hasLineBreak = body.find_first_of("\n\r") != std::string_view::npos;
  if (!options.forceMultiline && !hasLineBreak) {
    const std::string content =
        normalize_tag_line(strip_line_prefix(body, options), options);
    std::string result = options.start;
    result.push_back(' ');
    result += content;
    if (!content.empty()) result.push_back(' ');
    result += options.end;
    return result;
  }

  std::vector<std::string> lines;
  std::size_t cursor = 0;
  bool firstLine = true;
  while (cursor <= body.size()) {
    const auto terminator = find_next_line_terminator(body, cursor);
    const std::size_t lineEnd =
        terminator.has_value() ? terminator->position : body.size();
    const std::string_view line = body.substr(cursor, lineEnd - cursor);
    lines.emplace_back(firstLine
                           ? std::string(line)
                           : std::string(strip_line_prefix(line, options)));
    firstLine = false;
    if (!terminator.has_value()) break;
    cursor = terminator->position + terminator->length;
  }

  auto first = lines.begin();
  while (first != lines.end() && first->empty()) ++first;
  auto last = lines.end();
  while (last != first && (last - 1)->empty()) --last;

  std::vector<CommentLine> formatted;
  std::size_t blankRun = 0;
  bool previousWasTag = false;
  for (auto it = first; it != last; ++it) {
    if (it->empty()) {
      if (blankRun < options.maxBlankLines)
        formatted.push_back(CommentLine{.kind = CommentLineKind::Blank});
      ++blankRun;
      previousWasTag = false;
      continue;
    }
    blankRun = 0;
    if (const std::string normalized = normalize_tag_line(*it, options);
        is_tag_line(normalized, options)) {
      formatted.push_back(
          CommentLine{.kind = CommentLineKind::Tag, .text = normalized});
      previousWasTag = true;
      continue;
    }
    formatted.push_back(
        CommentLine{.kind = previousWasTag ? CommentLineKind::TagContinuation
                                           : CommentLineKind::Text,
                    .text = normalize_line(*it, options)});
  }

  if (formatted.empty()) return options.start + " " + options.end;

  const std::string_view newlineStyle = options.newlineStyle.empty()
                                            ? detect_newline_style(text)
                                            : options.newlineStyle;
  const std::string endPrefix = derive_end_prefix(options.newLineStart);
  std::string result = options.start;
  using enum CommentLineKind;
  for (const CommentLine& line : formatted) {
    result += newlineStyle;
    switch (line.kind) {
      case Blank:
        result += options.newLineStart;
        break;
      case TagContinuation:
        append_prefixed(result, options.newLineStart + options.tagContinuation,
                        line.text);
        break;
      case Tag:
      case Text:
        append_prefixed(result, options.newLineStart, line.text);
        break;
    }
  }
  result += newlineStyle;
  result += endPrefix;
  result += options.end;
  return result;
}

}  // namespace cursiv
