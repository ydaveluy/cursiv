#include <pegium/lsp/formatting/AbstractFormatter.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/utils/FunctionRef.hpp>

namespace pegium {

namespace {

enum class CommentLineKind : std::uint8_t { Text, Blank, Tag, TagContinuation };

struct FormattedCommentLine {
  CommentLineKind kind = CommentLineKind::Text;
  std::string text;
};

[[nodiscard]] bool is_horizontal_whitespace(char ch) noexcept {
  return ch == ' ' || ch == '\t';
}

[[nodiscard]] std::string_view trim_horizontal(std::string_view text) noexcept {
  while (!text.empty() && is_horizontal_whitespace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_horizontal_whitespace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string compact_whitespace(std::string_view text) {
  std::string result;
  result.reserve(text.size());

  bool pendingSpace = false;
  for (const auto ch : text) {
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

[[nodiscard]] std::string_view strip_comment_line_prefix(
    std::string_view line,
    const MultilineCommentFormatOptions &options) noexcept {
  line = trim_horizontal(line);
  if (const auto linePrefix = trim_horizontal(options.newLineStart);
      !linePrefix.empty() && line.starts_with(linePrefix)) {
    line.remove_prefix(linePrefix.size());
    if (!line.empty() && is_horizontal_whitespace(line.front())) {
      line.remove_prefix(1);
    }
  }
  return trim_horizontal(line);
}

[[nodiscard]] std::string normalize_line_text(
    std::string_view line, const MultilineCommentFormatOptions &options) {
  return options.normalizeWhitespace ? compact_whitespace(line)
                                     : std::string(line);
}

[[nodiscard]] bool is_tag_line(
    std::string_view line, const MultilineCommentFormatOptions &options) noexcept {
  return options.tagStart.has_value() && !options.tagStart->empty() &&
         line.starts_with(*options.tagStart);
}

[[nodiscard]] std::string
normalize_tag_line(std::string_view line,
                   const MultilineCommentFormatOptions &options) {
  line = trim_horizontal(line);
  if (!is_tag_line(line, options)) {
    return normalize_line_text(line, options);
  }

  const auto separator = line.find_first_of(" \t");
  if (separator == std::string_view::npos) {
    return std::string(line);
  }

  const auto tag = line.substr(0, separator);
  const auto rest = normalize_line_text(line.substr(separator + 1), options);
  if (rest.empty()) {
    return std::string(tag);
  }
  return std::string(tag) + " " + rest;
}

[[nodiscard]] std::string derive_comment_end_prefix(
    std::string_view newLineStart) {
  const auto originalPrefix = newLineStart;
  while (!newLineStart.empty() && is_horizontal_whitespace(newLineStart.back())) {
    newLineStart.remove_suffix(1);
  }
  const auto linePrefix = trim_horizontal(newLineStart);
  if (linePrefix.empty()) {
    return std::string(originalPrefix);
  }
  auto prefix = std::string(newLineStart);
  if (prefix.ends_with(linePrefix)) {
    prefix.erase(prefix.size() - linePrefix.size());
  }
  return prefix;
}

[[nodiscard]] std::string_view
unwrap_comment_body(std::string_view text,
                    const MultilineCommentFormatOptions &options) noexcept {
  if (!text.starts_with(options.start) || !text.ends_with(options.end) ||
      text.size() < options.start.size() + options.end.size()) {
    return {};
  }
  text.remove_prefix(options.start.size());
  text.remove_suffix(options.end.size());
  return text;
}

void append_prefixed_comment_line(std::string &result, std::string_view prefix,
                                  std::string_view text) {
  result.append(prefix);
  if (text.empty()) {
    return;
  }
  if (!prefix.empty() && !is_horizontal_whitespace(prefix.back())) {
    result.push_back(' ');
  }
  result.append(text);
}

struct FormattingKey {
  NodeId nodeId = kNoNode;
  FormattingMode mode = FormattingMode::Prepend;

  friend bool operator==(const FormattingKey &,
                         const FormattingKey &) noexcept = default;
};

struct FormattingKeyHash {
  [[nodiscard]] std::size_t operator()(const FormattingKey &key) const noexcept {
    return (static_cast<std::size_t>(key.nodeId) << 1) |
           static_cast<std::size_t>(key.mode == FormattingMode::Append);
  }
};

struct FormattingContext {
  const workspace::Document &document;
  const workspace::TextDocument &textDocument;
  const ::lsp::FormattingOptions &options;
  std::int32_t indentation = 0;
};

struct PendingEdit {
  TextOffset begin = 0;
  TextOffset end = 0;
  std::string newText;
};

[[nodiscard]] constexpr std::uint32_t
effective_tab_size(const ::lsp::FormattingOptions &options) noexcept {
  return options.tabSize == 0 ? 1u : options.tabSize;
}

[[nodiscard]] std::string indentation_text(std::int32_t indentation,
                                           const ::lsp::FormattingOptions &options) {
  indentation = std::max(indentation, 0);
  if (options.insertSpaces) {
    return std::string(static_cast<std::size_t>(
                           indentation * static_cast<std::int32_t>(
                                             effective_tab_size(options))),
                       ' ');
  }
  return std::string(static_cast<std::size_t>(indentation), '\t');
}

[[nodiscard]] std::string whitespace_text(std::int32_t characters,
                                          const ::lsp::FormattingOptions &options) {
  characters = std::max(characters, 0);
  return std::string(static_cast<std::size_t>(characters),
                     options.insertSpaces ? ' ' : '\t');
}

[[nodiscard]] ::lsp::Range to_lsp_range(const workspace::Document &document,
                                        TextOffset begin,
                                        TextOffset end) {
  const auto &textDocument = document.textDocument();
  ::lsp::Range range{};
  range.start = textDocument.positionAt(begin);
  range.end = textDocument.positionAt(end);
  return range;
}

[[nodiscard]] bool ranges_overlap(const ::lsp::Range &inside,
                                  const ::lsp::Range &total) noexcept {
  // Standard inclusive line-interval overlap. The previous three-case form
  // missed edits that straddle the TOP boundary of the requested range
  // (inside.start < total.start <= inside.end < total.end), silently dropping
  // them from a range/on-type format.
  return inside.start.line <= total.end.line &&
         total.start.line <= inside.end.line;
}

[[nodiscard]] bool edit_inside_range(const workspace::Document &document,
                                     const PendingEdit &edit,
                                     const std::optional<::lsp::Range> &range) {
  if (!range.has_value()) {
    return true;
  }
  return ranges_overlap(to_lsp_range(document, edit.begin, edit.end), *range);
}

[[nodiscard]] bool node_inside_range(const workspace::Document &document,
                                     const CstNodeView &node,
                                     const std::optional<::lsp::Range> &range) {
  if (!range.has_value()) {
    return true;
  }
  return ranges_overlap(
      to_lsp_range(document, node.getBegin(), node.getEnd()), *range);
}

[[nodiscard]] std::int32_t get_existing_indentation_character_count(
    std::string_view text, const FormattingContext &context) noexcept {
  const auto tabSize = effective_tab_size(context.options);
  if (context.options.insertSpaces) {
    std::int32_t count = 0;
    for (const auto ch : text) {
      if (ch == ' ') {
        ++count;
      } else if (ch == '\t') {
        count += static_cast<std::int32_t>(tabSize);
      } else {
        break;
      }
    }
    return count;
  }

  std::int32_t count = 0;
  std::uint32_t spaces = 0;
  for (const auto ch : text) {
    if (ch == ' ') {
      ++spaces;
      continue;
    }
    count += static_cast<std::int32_t>(spaces / tabSize);
    count += static_cast<std::int32_t>(spaces % tabSize);
    spaces = 0;
    if (ch == '\t') {
      ++count;
      continue;
    }
    break;
  }
  count += static_cast<std::int32_t>(spaces / tabSize);
  count += static_cast<std::int32_t>(spaces % tabSize);
  return count;
}

[[nodiscard]] std::int32_t
get_indentation_character_count(const FormattingContext &context,
                                const FormattingMove *move = nullptr) noexcept {
  auto indentation = context.indentation;
  if (move != nullptr && move->tabs.has_value()) {
    indentation += *move->tabs;
  }
  return context.options.insertSpaces
             ? indentation * static_cast<std::int32_t>(
                                  effective_tab_size(context.options))
             : indentation;
}

[[nodiscard]] std::int32_t fit_into_options(std::int32_t value,
                                            std::int32_t existing,
                                            const FormattingActionOptions &options) noexcept {
  if (options.allowMore) {
    value = std::max(existing, value);
  } else if (options.allowLess) {
    value = std::min(existing, value);
  }
  return value;
}

[[nodiscard]] const FormattingMove *
find_fitting_move(const ::lsp::Range &range, const FormattingAction &formatting) noexcept {
  if (formatting.moves.empty()) {
    return nullptr;
  }
  if (formatting.moves.size() == 1u) {
    return &formatting.moves.front();
  }

  const auto existingLines = static_cast<std::int32_t>(range.end.line) -
                             static_cast<std::int32_t>(range.start.line);
  for (const auto &move : formatting.moves) {
    if (move.lines.has_value() && existingLines <= *move.lines) {
      return &move;
    }
    if (!move.lines.has_value() && existingLines == 0) {
      return &move;
    }
  }
  return &formatting.moves.back();
}

[[nodiscard]] PendingEdit create_space_edit(const workspace::Document &document,
                                            TextOffset begin, TextOffset end,
                                            std::int32_t spaces,
                                            const FormattingActionOptions &options) {
  if (auto range = to_lsp_range(document, begin, end);
      range.start.line == range.end.line) {
    const auto existingSpaces =
        static_cast<std::int32_t>(range.end.character) -
        static_cast<std::int32_t>(range.start.character);
    spaces = fit_into_options(spaces, existingSpaces, options);
  }
  return PendingEdit{.begin = begin,
                     .end = end,
                     .newText = std::string(static_cast<std::size_t>(
                                                std::max(spaces, 0)),
                                            ' ')};
}

[[nodiscard]] PendingEdit create_line_edit(const workspace::Document &document,
                                           TextOffset begin, TextOffset end,
                                           std::int32_t lines,
                                           const FormattingContext &context,
                                           const FormattingActionOptions &options) {
  const auto range = to_lsp_range(document, begin, end);
  const auto existingLines =
      static_cast<std::int32_t>(range.end.line) -
      static_cast<std::int32_t>(range.start.line);
  lines = fit_into_options(lines, existingLines, options);
  std::string newText(static_cast<std::size_t>(std::max(lines, 0)), '\n');
  newText += indentation_text(context.indentation, context.options);
  return PendingEdit{.begin = begin, .end = end, .newText = std::move(newText)};
}

[[nodiscard]] PendingEdit create_tab_edit(const workspace::Document &document,
                                          TextOffset begin, TextOffset end,
                                          bool hasPrevious,
                                          const FormattingContext &context) {
  const auto range = to_lsp_range(document, begin, end);
  const auto minimumLines = hasPrevious ? 1 : 0;
  const auto lines = std::max(
      static_cast<std::int32_t>(range.end.line) -
          static_cast<std::int32_t>(range.start.line),
      minimumLines);
  std::string newText(static_cast<std::size_t>(lines), '\n');
  newText += indentation_text(context.indentation, context.options);
  return PendingEdit{.begin = begin, .end = end, .newText = std::move(newText)};
}

[[nodiscard]] std::string reindent_hidden_text(std::string_view text,
                                               std::int32_t characterIncrease,
                                               const FormattingContext &context) {
  if (characterIncrease == 0) {
    return std::string(text);
  }

  const auto indentPrefix =
      characterIncrease > 0
          ? whitespace_text(characterIncrease, context.options)
          : std::string();

  std::string result;
  result.reserve(text.size() +
                 (characterIncrease > 0
                      ? static_cast<std::size_t>(characterIncrease)
                      : 0u));

  bool firstLine = true;
  std::size_t cursor = 0;
  while (cursor <= text.size()) {
    const auto next = text.find('\n', cursor);
    const auto lineEnd = next == std::string_view::npos ? text.size() : next;
    auto line = text.substr(cursor, lineEnd - cursor);
    bool hadCarriageReturn = false;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
      hadCarriageReturn = true;
    }

    if (firstLine) {
      result.append(line.begin(), line.end());
      firstLine = false;
    } else if (characterIncrease > 0) {
      result += indentPrefix;
      result.append(line.begin(), line.end());
    } else {
      std::size_t trim = 0;
      auto remaining = static_cast<std::size_t>(-characterIncrease);
      while (trim < line.size() && remaining > 0 &&
             (line[trim] == ' ' || line[trim] == '\t')) {
        ++trim;
        --remaining;
      }
      result.append(line.substr(trim).begin(), line.substr(trim).end());
    }

    if (hadCarriageReturn) {
      result.push_back('\r');
    }
    if (next == std::string_view::npos) {
      break;
    }
    result.push_back('\n');
    cursor = next + 1;
  }
  return result;
}

[[nodiscard]] std::vector<PendingEdit>
create_hidden_text_edits(const std::optional<CstNodeView> &previous,
                         const CstNodeView &hidden,
                         const FormattingAction *formatting,
                         const std::string *replacementText,
                         const FormattingContext &context) {
  const auto startLine =
      context.textDocument.positionAt(hidden.getBegin()).line;
  const auto previousEndsOnSameLine =
      previous.has_value() &&
      context.textDocument.positionAt(previous->getEnd()).line == startLine;

  const auto lineStartOffset =
      context.textDocument.offsetAt(text::Position(startLine, 0));
  const auto hiddenStartText =
      context.textDocument.getText().substr(lineStartOffset,
                                             hidden.getBegin() - lineStartOffset);
  const auto begin = previous.has_value() ? previous->getEnd() : 0u;
  const auto end = hidden.getBegin();
  const auto startRange = to_lsp_range(context.document, begin, end);
  const auto *move =
      formatting == nullptr ? nullptr : find_fitting_move(startRange, *formatting);

  FormattingContext effectiveContext = context;
  if (move != nullptr && move->tabs.has_value()) {
    effectiveContext.indentation += *move->tabs;
  }

  // A comment is anchored by the newlines already surrounding it: the formatter
  // never rewrites the gap between the previous token and a following comment.
  // Only the comment's own indentation is adjusted.
  std::vector<PendingEdit> edits;

  const auto hiddenStartChars =
      get_existing_indentation_character_count(hiddenStartText, context);
  const auto shouldReindentHiddenText = !previousEndsOnSameLine;
  const auto expectedStartChars =
      shouldReindentHiddenText ? get_indentation_character_count(effectiveContext)
                               : hiddenStartChars;

  // A reformatted comment (onHidden replacement) is rewritten wholesale, its
  // body reindented and its first-line indentation folded into the replacement
  // text (a standalone leading-whitespace insertion would collide with the
  // node-text replacement at the same offset). A trailing comment keeps its
  // leading whitespace untouched.
  if (replacementText != nullptr) {
    const auto characterIncrease = expectedStartChars - hiddenStartChars;
    const bool commentAtLineStart = lineStartOffset == hidden.getBegin();
    std::string foldedIndent;
    if (shouldReindentHiddenText && characterIncrease != 0) {
      if (commentAtLineStart) {
        foldedIndent =
            indentation_text(effectiveContext.indentation, context.options);
      } else {
        edits.push_back(PendingEdit{
            .begin = lineStartOffset,
            .end = hidden.getBegin(),
            .newText =
                indentation_text(effectiveContext.indentation, context.options),
        });
      }
    }
    auto normalizedHiddenText =
        shouldReindentHiddenText
            ? reindent_hidden_text(*replacementText, expectedStartChars,
                                   effectiveContext)
            : std::string(*replacementText);
    if (!foldedIndent.empty()) {
      normalizedHiddenText.insert(0, foldedIndent);
    }
    edits.push_back(PendingEdit{
        .begin = hidden.getBegin(),
        .end = hidden.getEnd(),
        .newText = std::move(normalizedHiddenText),
    });
    return edits;
  }

  // Indentation only: shift the comment by the delta computed on its first line
  // and apply that same delta to every line, so the comment's internal relative
  // indentation (e.g. the ` * ` alignment of a block comment) is preserved. Each
  // line is adjusted by a column-0 edit — no whole-node replacement, hence no
  // offset collision to guard against. A trailing comment (same line as its
  // previous node) is left untouched.
  if (!shouldReindentHiddenText) {
    return edits;
  }
  const auto characterIncrease = expectedStartChars - hiddenStartChars;
  if (characterIncrease == 0) {
    return edits;
  }
  const auto hiddenText = hidden.getText();
  std::int32_t lineIndex = 0;
  std::size_t cursor = 0;
  while (cursor <= hiddenText.size()) {
    const auto next = hiddenText.find('\n', cursor);
    const auto lineEnd = next == std::string_view::npos ? hiddenText.size() : next;
    auto line = hiddenText.substr(cursor, lineEnd - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    const auto lineOffset =
        context.textDocument.offsetAt(text::Position(startLine + lineIndex, 0));
    if (characterIncrease > 0) {
      edits.push_back(PendingEdit{
          .begin = lineOffset,
          .end = lineOffset,
          .newText = whitespace_text(characterIncrease, context.options),
      });
    } else {
      // Remove up to `-characterIncrease` leading whitespace characters actually
      // present on the line (the first line's whitespace lives before
      // `hidden.begin`, in `hiddenStartText`).
      const std::string_view lineWs = lineIndex == 0 ? hiddenStartText : line;
      std::int32_t removable = 0;
      while (static_cast<std::size_t>(removable) < lineWs.size() &&
             removable < -characterIncrease &&
             (lineWs[static_cast<std::size_t>(removable)] == ' ' ||
              lineWs[static_cast<std::size_t>(removable)] == '\t')) {
        ++removable;
      }
      if (removable > 0) {
        edits.push_back(PendingEdit{
            .begin = lineOffset,
            .end = context.textDocument.offsetAt(
                text::Position(startLine + lineIndex, removable)),
            .newText = std::string(),
        });
      }
    }
    if (next == std::string_view::npos) {
      break;
    }
    ++lineIndex;
    cursor = next + 1;
  }
  return edits;
}

[[nodiscard]] std::vector<PendingEdit>
create_text_edits(const std::optional<CstNodeView> &previous,
                  const CstNodeView &node, const FormattingAction &formatting,
                  FormattingContext &context) {
  // A hidden node reached as the previous token's append target is formatted by
  // its own visit in collect_cst_edits (its prepend action carries the block
  // indentation, and its text replacement runs there). Formatting it again from
  // the append side would only re-derive its indentation from the previous
  // token's context — which lacks that block indent — and emit a conflicting
  // leading-whitespace edit. Leave the comment to its owning visit.
  if (node.isHidden()) {
    return {};
  }
  if (previous.has_value() && previous->getEnd() > node.getBegin()) {
    return {};
  }

  const auto begin = previous.has_value() ? previous->getEnd() : 0u;
  const auto end = node.getBegin();
  const auto range = to_lsp_range(context.document, begin, end);
  const auto *move = find_fitting_move(range, formatting);
  if (move == nullptr) {
    return {};
  }

  const auto existingIndentation = context.indentation;
  if (move->tabs.has_value()) {
    context.indentation += *move->tabs;
  }

  std::vector<PendingEdit> edits;
  if (move->characters.has_value()) {
    if (!previous.has_value() || !previous->isHidden()) {
      edits.push_back(create_space_edit(context.document, begin, end,
                                        *move->characters, formatting.options));
    }
  } else if (move->lines.has_value()) {
    edits.push_back(create_line_edit(context.document, begin, end, *move->lines,
                                     context, formatting.options));
  } else if (move->tabs.has_value()) {
    edits.push_back(create_tab_edit(context.document, begin, end,
                                    previous.has_value(), context));
  }

  if (node.isLeaf()) {
    context.indentation = existingIndentation;
  }
  return edits;
}

[[nodiscard]] const std::string *
find_hidden_replacement(const std::unordered_map<NodeId, std::string> &replacements,
                        const CstNodeView &node) {
  if (const auto it = replacements.find(node.id()); it != replacements.end()) {
    return &it->second;
  }
  return nullptr;
}

[[nodiscard]] bool is_necessary_edit_text(const workspace::Document &document,
                                          const PendingEdit &edit) {
  auto existing =
      std::string(document.textDocument().getText().substr(
          edit.begin, edit.end - edit.begin));
  std::erase(existing, '\r');
  return edit.newText != existing;
}

void append_filtered_edits(const workspace::Document &document,
                          const std::vector<PendingEdit> &candidates,
                          const std::optional<::lsp::Range> &requestedRange,
                          std::vector<PendingEdit> &edits) {
  for (const auto &edit : candidates) {
    if (!edit_inside_range(document, edit, requestedRange)) {
      continue;
    }
    edits.push_back(edit);
  }
}

void collect_cst_edits(
    const CstNodeView &node,
    const std::unordered_map<FormattingKey, FormattingAction, FormattingKeyHash>
        &formattings,
    const std::unordered_map<NodeId, std::string> &hiddenReplacements,
    utils::function_ref<void(const CstNodeView &, const FormattingContext &)>
        hiddenFormatter,
    FormattingContext &context, const std::optional<::lsp::Range> &requestedRange,
    std::optional<CstNodeView> &lastLeaf, std::vector<PendingEdit> &edits,
    const utils::CancellationToken &cancelToken) {
  utils::throw_if_cancelled(cancelToken);
  const auto initialIndentation = context.indentation;

  const auto prependIt =
      formattings.find(FormattingKey{node.id(), FormattingMode::Prepend});
  if (node.isHidden()) {
    auto hiddenContext = context;
    if (prependIt != formattings.end()) {
      const auto begin = lastLeaf.has_value() ? lastLeaf->getEnd() : 0u;
      const auto range = to_lsp_range(context.document, begin, node.getBegin());
      if (const auto *move = find_fitting_move(range, prependIt->second);
          move != nullptr && move->tabs.has_value()) {
        hiddenContext.indentation += *move->tabs;
      }
    }
    hiddenFormatter(node, hiddenContext);
  }

  if (prependIt != formattings.end() && !node.isHidden()) {
    append_filtered_edits(
        context.document, create_text_edits(lastLeaf, node, prependIt->second, context),
        requestedRange, edits);
  }

  if (const auto appendIt =
          formattings.find(FormattingKey{node.id(), FormattingMode::Append});
      appendIt != formattings.end()) {
    if (const auto next = find_next_leaf(node); next.has_value()) {
      append_filtered_edits(context.document,
                            create_text_edits(node, *next, appendIt->second, context),
                            requestedRange, edits);
    }
  }

  if (node.isHidden()) {
    append_filtered_edits(context.document,
                          create_hidden_text_edits(
                              lastLeaf, node,
                              prependIt == formattings.end() ? nullptr
                                                             : &prependIt->second,
                              find_hidden_replacement(hiddenReplacements, node),
                              context),
                          requestedRange, edits);
  }

  if (node.isLeaf()) {
    lastLeaf = node;
    context.indentation = initialIndentation;
    return;
  }

  for (const auto child : node) {
    collect_cst_edits(child, formattings, hiddenReplacements, hiddenFormatter,
                      context, requestedRange, lastLeaf, edits, cancelToken);
  }
  context.indentation = initialIndentation;
}

[[nodiscard]] std::vector<::lsp::TextEdit>
finalize_edits(const workspace::Document &document, std::vector<PendingEdit> edits) {
  std::ranges::stable_sort(edits, [](const PendingEdit &left,
                                     const PendingEdit &right) {
    if (left.begin != right.begin) {
      return left.begin < right.begin;
    }
    return left.end < right.end;
  });

  std::vector<PendingEdit> filtered;
  for (const auto &edit : edits) {
    auto last = filtered.empty() ? nullptr : &filtered.back();
    while (last != nullptr && edit.begin < last->end) {
      filtered.pop_back();
      last = filtered.empty() ? nullptr : &filtered.back();
    }
    // Two edits over the same zero-width gap (begin == end, e.g. a leaf's
    // `append` and the adjacent next leaf's `prepend`) do not satisfy the
    // `begin < end` overlap test above, so collapse an exact-range duplicate
    // here. Keep the later edit, matching the overlap rule's "later wins".
    if (last != nullptr && last->begin == edit.begin && last->end == edit.end) {
      filtered.pop_back();
    }
    filtered.push_back(edit);
  }

  std::vector<::lsp::TextEdit> result;
  result.reserve(filtered.size());
  for (const auto &edit : filtered) {
    if (!is_necessary_edit_text(document, edit)) {
      continue;
    }
    result.push_back(::lsp::TextEdit{
        .range = to_lsp_range(document, edit.begin, edit.end),
        .newText = edit.newText,
    });
  }
  return result;
}

[[nodiscard]] int compare_moves(const FormattingMove &left,
                                const FormattingMove &right) noexcept {
  if (const auto leftLines = left.lines.value_or(0),
      rightLines = right.lines.value_or(0);
      leftLines != rightLines) {
    return leftLines < rightLines ? -1 : 1;
  }

  if (const auto leftTabs = left.tabs.value_or(0),
      rightTabs = right.tabs.value_or(0);
      leftTabs != rightTabs) {
    return leftTabs < rightTabs ? -1 : 1;
  }

  if (const auto leftChars = left.characters.value_or(0),
      rightChars = right.characters.value_or(0);
      leftChars != rightChars) {
    return leftChars < rightChars ? -1 : 1;
  }
  return 0;
}

} // namespace

std::string HiddenNodeFormatter::indentation(std::int32_t delta) const {
  return indentation_text(_baseIndentation + delta, _options);
}

std::string AbstractFormatter::formatMultilineComment(
    std::string_view text, const MultilineCommentFormatOptions &options) {
  const auto body = unwrap_comment_body(text, options);
  if (body.data() == nullptr) {
    return std::string(text);
  }

  const auto hasLineBreak = body.find('\n') != std::string_view::npos ||
                            body.find('\r') != std::string_view::npos;
  if (!options.forceMultiline && !hasLineBreak) {
    const auto content =
        normalize_tag_line(strip_comment_line_prefix(body, options), options);
    std::string result = options.start;
    result.push_back(' ');
    result += content;
    if (!content.empty()) {
      result.push_back(' ');
    }
    result += options.end;
    return result;
  }

  std::vector<std::string> normalizedLines;
  std::size_t cursor = 0;
  bool firstLine = true;
  while (cursor <= body.size()) {
    const auto next = body.find('\n', cursor);
    const auto lineEnd = next == std::string_view::npos ? body.size() : next;
    auto line = body.substr(cursor, lineEnd - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    normalizedLines.emplace_back(firstLine ? std::string(line)
                                           : std::string(strip_comment_line_prefix(
                                                 line, options)));
    firstLine = false;
    if (next == std::string_view::npos) {
      break;
    }
    cursor = next + 1;
  }

  auto first = normalizedLines.begin();
  while (first != normalizedLines.end() && first->empty()) {
    ++first;
  }
  auto last = normalizedLines.end();
  while (last != first && (last - 1)->empty()) {
    --last;
  }

  std::vector<FormattedCommentLine> lines;
  std::size_t blankRun = 0;
  bool previousWasTag = false;
  for (auto it = first; it != last; ++it) {
    if (it->empty()) {
      if (blankRun < options.maxBlankLines) {
        lines.push_back(FormattedCommentLine{.kind = CommentLineKind::Blank});
      }
      ++blankRun;
      previousWasTag = false;
      continue;
    }

    blankRun = 0;
    if (const auto normalized = normalize_tag_line(*it, options);
        is_tag_line(normalized, options)) {
      lines.push_back(
          FormattedCommentLine{.kind = CommentLineKind::Tag, .text = normalized});
      previousWasTag = true;
      continue;
    }

    lines.push_back(FormattedCommentLine{
        .kind = previousWasTag ? CommentLineKind::TagContinuation
                               : CommentLineKind::Text,
        .text = normalize_line_text(*it, options),
    });
  }

  if (lines.empty()) {
    return options.start + " " + options.end;
  }

  std::string result = options.start;
  const auto endPrefix = derive_comment_end_prefix(options.newLineStart);
  using enum CommentLineKind;
  for (const auto &line : lines) {
    result.push_back('\n');
    switch (line.kind) {
    case Blank:
      result += options.newLineStart;
      break;
    case TagContinuation:
      append_prefixed_comment_line(result,
                                   options.newLineStart + options.tagContinuation,
                                   line.text);
      break;
    case Tag:
    case Text:
      append_prefixed_comment_line(result, options.newLineStart, line.text);
      break;
    }
  }
  result.push_back('\n');
  result += endPrefix;
  result += options.end;
  return result;
}

std::string AbstractFormatter::formatMultilineComment(
    const HiddenNodeFormatter &comment,
    const MultilineCommentFormatOptions &options) {
  return formatMultilineComment(comment.text(), options);
}

void AbstractFormatter::formatBlock(FormattingRegion open, FormattingRegion close,
                                    FormattingRegion content,
                                    const BlockFormatOptions &options) {
  if (open.empty() || close.empty()) {
    return;
  }

  const auto beforeOpen = options.beforeOpen.value_or(oneSpace);
  const auto insideEmpty = options.insideEmpty.value_or(noSpace);
  const auto afterOpen = options.afterOpen.value_or(newLine);
  const auto beforeClose = options.beforeClose.value_or(newLine);
  const auto contentIndent = options.contentIndent.value_or(indent);

  if (content.empty()) {
    open.prepend(beforeOpen).append(insideEmpty);
    close.prepend(insideEmpty);
    return;
  }

  open.prepend(beforeOpen).append(afterOpen);
  close.prepend(beforeClose);
  content.prepend(contentIndent);
}

void AbstractFormatter::formatSeparatedList(
    FormattingRegion separators, const SeparatedListFormatOptions &options) {
  if (separators.empty()) {
    return;
  }

  separators.prepend(options.beforeSeparator.value_or(noSpace))
      .append(options.afterSeparator.value_or(oneSpace));
}

std::string AbstractFormatter::formatLineComment(
    std::string_view text, const LineCommentFormatOptions &options) {
  if (!text.starts_with(options.start)) {
    return std::string(text);
  }

  auto body = trim_horizontal(text.substr(options.start.size()));
  auto normalized =
      options.normalizeWhitespace ? compact_whitespace(body) : std::string(body);
  if (normalized.empty()) {
    return options.start;
  }

  std::string result = options.start;
  if (options.ensureSpaceAfterStart &&
      (result.empty() || !is_horizontal_whitespace(result.back()))) {
    result.push_back(' ');
  }
  result += normalized;
  return result;
}

std::string AbstractFormatter::formatLineComment(
    const HiddenNodeFormatter &comment,
    const LineCommentFormatOptions &options) {
  return formatLineComment(comment.text(), options);
}

void HiddenNodeFormatter::replace(std::string text) const {
  if (_textCollector) {
    _textCollector(_node, std::move(text));
  }
}

HiddenNodeFormatter &
HiddenNodeFormatter::prepend(const FormattingAction &formatting) {
  if (_collector) {
    _collector(_node, FormattingMode::Prepend, formatting);
  }
  return *this;
}

HiddenNodeFormatter &
HiddenNodeFormatter::append(const FormattingAction &formatting) {
  if (_collector) {
    _collector(_node, FormattingMode::Append, formatting);
  }
  return *this;
}

HiddenNodeFormatter &
HiddenNodeFormatter::surround(const FormattingAction &formatting) {
  prepend(formatting);
  append(formatting);
  return *this;
}

FormattingRegion &FormattingRegion::prepend(const FormattingAction &formatting) {
  for (const auto &node : _nodes) {
    if (_collector) {
      _collector(node, FormattingMode::Prepend, formatting);
    }
  }
  return *this;
}

FormattingRegion &FormattingRegion::append(const FormattingAction &formatting) {
  for (const auto &node : _nodes) {
    if (_collector) {
      _collector(node, FormattingMode::Append, formatting);
    }
  }
  return *this;
}

FormattingRegion &FormattingRegion::surround(const FormattingAction &formatting) {
  prepend(formatting);
  append(formatting);
  return *this;
}

std::size_t FormattingRegion::normalizeIndex(std::ptrdiff_t index) const noexcept {
  if (index < 0) {
    const auto normalized =
        static_cast<std::ptrdiff_t>(_nodes.size()) + index;
    return static_cast<std::size_t>(std::max<std::ptrdiff_t>(normalized, 0));
  }
  return static_cast<std::size_t>(
      std::min<std::ptrdiff_t>(index, static_cast<std::ptrdiff_t>(_nodes.size())));
}

FormattingRegion FormattingRegion::slice(std::ptrdiff_t start) const {
  return slice(start, static_cast<std::ptrdiff_t>(_nodes.size()));
}

FormattingRegion FormattingRegion::slice(std::ptrdiff_t start,
                                         std::ptrdiff_t end) const {
  const auto normalizedStart = normalizeIndex(start);
  const auto normalizedEnd = normalizeIndex(end);
  if (normalizedStart >= normalizedEnd) {
    return FormattingRegion({}, _collector);
  }
  return FormattingRegion(std::vector<CstNodeView>(_nodes.begin() + normalizedStart,
                                                   _nodes.begin() + normalizedEnd),
                          _collector);
}

FormattingAction
AbstractFormatter::fitActions(std::initializer_list<FormattingAction> actions) {
  FormattingAction combined{};
  for (const auto &action : actions) {
    combined.moves.insert(combined.moves.end(), action.moves.begin(),
                          action.moves.end());
    combined.options.priority =
        std::max(combined.options.priority, action.options.priority);
    combined.options.allowMore =
        combined.options.allowMore || action.options.allowMore;
    combined.options.allowLess =
        combined.options.allowLess || action.options.allowLess;
  }
  std::ranges::sort(combined.moves, [](const FormattingMove &left,
                                       const FormattingMove &right) {
    return compare_moves(left, right) < 0;
  });
  return combined;
}

FormattingAction AbstractFormatter::spaces(std::int32_t count,
                                           FormattingActionOptions options) {
  return FormattingAction{
      .options = std::move(options),
      .moves = {FormattingMove{.characters = count}},
  };
}

FormattingAction AbstractFormatter::newLines(std::int32_t count,
                                             FormattingActionOptions options) {
  return FormattingAction{
      .options = std::move(options),
      .moves = {FormattingMove{.lines = count}},
  };
}

void AbstractFormatter::format(FormattingBuilder &builder,
                               const AstNode *node) const {
  if (node == nullptr) {
    return;
  }

  const auto it = _formatters.find(std::type_index(typeid(*node)));
  if (it == _formatters.end()) {
    return;
  }
  it->second(builder, node);
}

void AbstractFormatter::registerHiddenFormatter(std::string_view terminalRuleName,
                                                HiddenNodeCallback callback) {
  if (terminalRuleName.empty() || !callback) {
    return;
  }

  _hiddenFormatters.insert_or_assign(std::string(terminalRuleName),
                                     std::move(callback));
}

void AbstractFormatter::formatHidden(HiddenNodeFormatter &hidden) const {
  const auto it = _hiddenFormatters.find(hidden.ruleName());
  if (it == _hiddenFormatters.end()) {
    return;
  }
  it->second(hidden);
}

std::vector<::lsp::TextEdit>
AbstractFormatter::formatDocument(
    const workspace::Document &document,
    const ::lsp::DocumentFormattingParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  if (!document.parseResult.parseDiagnostics.empty()) {
    return {};
  }
  return doDocumentFormat(document, params.options, std::nullopt, cancelToken);
}

std::vector<::lsp::TextEdit>
AbstractFormatter::formatDocumentRange(
    const workspace::Document &document,
    const ::lsp::DocumentRangeFormattingParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  if (!isFormatRangeErrorFree(document, params.range)) {
    return {};
  }
  return doDocumentFormat(document, params.options, params.range, cancelToken);
}

std::vector<::lsp::TextEdit>
AbstractFormatter::formatDocumentOnType(
    const workspace::Document &document,
    const ::lsp::DocumentOnTypeFormattingParams &params,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  ::lsp::Range range{};
  range.start.line = params.position.line;
  range.start.character = 0;
  range.end = params.position;
  if (!isFormatRangeErrorFree(document, range)) {
    return {};
  }
  return doDocumentFormat(document, params.options, range, cancelToken);
}

bool AbstractFormatter::isFormatRangeErrorFree(
    const workspace::Document &document, const ::lsp::Range &range) const {
  if (document.parseResult.parseDiagnostics.empty()) {
    return true;
  }

  std::uint32_t earliestErrorLine = std::numeric_limits<std::uint32_t>::max();
  for (const auto &diagnostic : document.parseResult.parseDiagnostics) {
    earliestErrorLine = std::min(
        earliestErrorLine,
        document.textDocument().positionAt(diagnostic.offset).line);
  }
  return earliestErrorLine > range.end.line;
}

std::vector<::lsp::TextEdit> AbstractFormatter::doDocumentFormat(
    const workspace::Document &document, const ::lsp::FormattingOptions &options,
    const std::optional<::lsp::Range> &range,
    const utils::CancellationToken &cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  const auto *root = document.parseResult.value;
  if (root == nullptr || !root->hasCstNode()) {
    return {};
  }

  std::unordered_map<FormattingKey, FormattingAction, FormattingKeyHash>
      formattings;
  std::unordered_map<NodeId, std::string> hiddenReplacements;
  const FormattingCollector collector =
      [&formattings](const CstNodeView &node, FormattingMode mode,
                     const FormattingAction &formatting) {
        const auto key = FormattingKey{node.id(), mode};
        const auto it = formattings.find(key);
        if (it == formattings.end() ||
            it->second.options.priority <= formatting.options.priority) {
          formattings[key] = formatting;
        }
      };
  const HiddenTextCollector hiddenTextCollector =
      [&hiddenReplacements](const CstNodeView &node, std::string text) {
        hiddenReplacements[node.id()] = std::move(text);
      };

  FormattingBuilder builder(collector);
  format(builder, root);
  for (const auto *node : root->getAllContent()) {
    utils::throw_if_cancelled(cancelToken);
    if (!node->hasCstNode() || !node_inside_range(document, node->getCstNode(), range)) {
      continue;
    }
    format(builder, node);
  }

  FormattingContext context{.document = document,
                            .textDocument = document.textDocument(),
                            .options = options};
  const auto hiddenFormatter = [this, &collector, &hiddenTextCollector, &options](
                                   const CstNodeView &node,
                                   const FormattingContext &currentContext) {
    const auto ruleName = get_terminal_rule_name(node);
    if (!ruleName.has_value()) {
      return;
    }
    HiddenNodeFormatter formatter(node, std::string(*ruleName),
                                  currentContext.indentation, options, collector,
                                  hiddenTextCollector);
    formatHidden(formatter);
  };
  std::vector<PendingEdit> edits;
  std::optional<CstNodeView> lastLeaf;
  for (const auto child : root->getCstNode().root()) {
    collect_cst_edits(child, formattings, hiddenReplacements, hiddenFormatter,
                      context, range, lastLeaf, edits, cancelToken);
  }
  return finalize_edits(document, std::move(edits));
}

} // namespace pegium
