#include "cursiv/lsp/formatting/FormattingEngine.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/formatting/FormattingAction.hpp"
#include "cursiv/lsp/formatting/LineTerminator.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

// A stretch of the document in both units, converted once.
struct TextSpan {
  TextOffset begin = 0;
  TextOffset end = 0;
  lsp::Range range;
};

struct PendingEdit {
  TextSpan span;
  std::string newText;
};

struct FormatContext {
  const Document& document;
  const Cst& cst;
  const lsp::FormattingOptions& options;
  std::int32_t indentation = 0;
  std::string_view newlineStyle = "\n";
};

constexpr std::uint32_t kMaxTabSize = 64;

[[nodiscard]] constexpr std::uint32_t effective_tab_size(
    const lsp::FormattingOptions& options) noexcept {
  return std::clamp<std::uint32_t>(options.tabSize, 1u, kMaxTabSize);
}

[[nodiscard]] std::int32_t checked_count(std::int64_t count) {
  if (count < std::numeric_limits<std::int32_t>::min() ||
      count > std::numeric_limits<std::int32_t>::max())
    throw std::length_error("formatting count exceeds addressable range");
  return static_cast<std::int32_t>(count);
}

[[nodiscard]] std::string whitespace_text(
    std::int32_t characters, const lsp::FormattingOptions& options) {
  characters = std::max(characters, 0);
  return std::string(static_cast<std::size_t>(characters),
                     options.insertSpaces ? ' ' : '\t');
}

[[nodiscard]] TextSpan span_of(const Document& document, TextOffset begin,
                               TextOffset end) {
  return TextSpan{.begin = begin,
                  .end = end,
                  .range = to_lsp(document.textDocument().rangeAt(begin, end))};
}

// The gap before `node`: from the end of `previous`, or from the start of the
// document when there is none.
[[nodiscard]] TextSpan gap_before(const FormatContext& context, NodeId previous,
                                  NodeId node) {
  return span_of(context.document,
                 previous == kNoNode ? 0u : context.cst[previous].end,
                 context.cst[node].begin);
}

[[nodiscard]] std::int32_t line_count(const lsp::Range& range) {
  return checked_count(static_cast<std::int64_t>(range.end.line) -
                       range.start.line);
}

// Inclusive line-interval overlap, which is what a range format needs: an edit
// that straddles either boundary of the selection is still an edit the user
// asked for, and dropping it would leave the selection half-formatted.
[[nodiscard]] bool edit_inside_range(
    const PendingEdit& edit, const std::optional<lsp::Range>& range) noexcept {
  if (!range.has_value()) return true;
  return edit.span.range.start.line <= range->end.line &&
         range->start.line <= edit.span.range.end.line;
}

// The order a client's edit list is owed. One definition, because the sink
// watches for it and `finalize_edits` restores it.
[[nodiscard]] bool span_before(const TextSpan& left,
                               const TextSpan& right) noexcept {
  return left.begin != right.begin ? left.begin < right.begin
                                   : left.end < right.end;
}

// Where a format's edits land: the window is applied at the push, and the
// order watched there. A rule on a record that has children emits the gap
// past its whole subtree before the subtree, so the order is not a given.
class EditSink {
 public:
  EditSink(std::vector<PendingEdit>& out,
           const std::optional<lsp::Range>& range) noexcept
      : _out(out), _range(range) {}

  void push(PendingEdit edit) {
    if (!edit_inside_range(edit, _range)) return;
    if (!_out.empty() && span_before(edit.span, _out.back().span))
      _sorted = false;
    _out.push_back(std::move(edit));
  }

  [[nodiscard]] bool sorted() const noexcept { return _sorted; }

 private:
  std::vector<PendingEdit>& _out;
  const std::optional<lsp::Range>& _range;
  bool _sorted = true;
};

// The first piece of a line's leading whitespace: one tab, or a whole run of
// spaces. No bytes means the whitespace is over.
struct MarginPiece {
  std::uint32_t bytes = 0;
  bool tab = false;
};

[[nodiscard]] MarginPiece first_margin_piece(std::string_view text) noexcept {
  if (!text.empty() && text.front() == '\t') return {.bytes = 1, .tab = true};
  return {.bytes = static_cast<std::uint32_t>(
              std::min(text.find_first_not_of(' '), text.size()))};
}

// The indentation units a piece counts for. Units are columns when spaces are
// inserted, so a tab counts `tabSize`. They are levels when tabs are, so a tab
// counts one, as does each group of `tabSize` spaces in a run, and each space
// the groups leave over counts one.
[[nodiscard]] std::int64_t piece_units(
    const MarginPiece& piece, const lsp::FormattingOptions& options) noexcept {
  const auto tabSize = effective_tab_size(options);
  if (piece.tab) return options.insertSpaces ? tabSize : 1;
  if (options.insertSpaces) return piece.bytes;
  return piece.bytes / tabSize + piece.bytes % tabSize;
}

// The spaces to drop from a run of `spaces` to take `units` off it, fewer
// than the run counts for. When tabs are inserted, the spaces the groups leave
// over go first, so the run keeps its whole groups.
[[nodiscard]] std::uint32_t spaces_for_units(
    std::uint32_t spaces, std::int64_t units,
    const lsp::FormattingOptions& options) noexcept {
  const auto count = static_cast<std::uint32_t>(units);
  if (options.insertSpaces) return count;
  const auto tabSize = effective_tab_size(options);
  const std::uint32_t leftover = spaces % tabSize;
  return count <= leftover ? count : leftover + (count - leftover) * tabSize;
}

// The indentation already written at the head of a line, in the unit the
// options ask for.
[[nodiscard]] std::int32_t existing_indentation_count(
    std::string_view text, const lsp::FormattingOptions& options) {
  std::int64_t count = 0;
  for (MarginPiece piece = first_margin_piece(text); piece.bytes > 0;
       piece = first_margin_piece(text)) {
    count += piece_units(piece, options);
    text.remove_prefix(piece.bytes);
  }
  return checked_count(count);
}

[[nodiscard]] std::int32_t indentation_count(
    std::int32_t level, const lsp::FormattingOptions& options) {
  return options.insertSpaces ? checked_count(static_cast<std::int64_t>(level) *
                                              effective_tab_size(options))
                              : level;
}

[[nodiscard]] std::int32_t fit_into_options(
    std::int32_t value, std::int32_t existing,
    const FormattingActionOptions& options) noexcept {
  if (options.allowMore) return std::max(existing, value);
  if (options.allowLess) return std::min(existing, value);
  return value;
}

// Which candidate move the gap already looks like: a `fit(oneSpace, newLine)`
// keeps a construction on one line while it is written on one, and does not
// pull a construction the author spread over three back onto one.
[[nodiscard]] const FormattingMove* find_fitting_move(
    const lsp::Range& range, const FormattingAction& action) {
  if (action.moves.empty()) return nullptr;
  if (action.moves.size() == 1u) return &action.moves.front();

  const std::int32_t existingLines = line_count(range);
  for (const FormattingMove& move : action.moves) {
    if (move.lines.has_value() && existingLines <= *move.lines) return &move;
    if (!move.lines.has_value() && existingLines == 0) return &move;
  }
  return &action.moves.back();
}

// The indentation delta a rule's fitting move carries; none for a move
// without tabs, or no rule.
[[nodiscard]] std::int32_t tabs_of(const lsp::Range& gap,
                                   const FormattingAction* action) {
  if (action == nullptr) return 0;
  const FormattingMove* const move = find_fitting_move(gap, *action);
  return move != nullptr && move->tabs.has_value() ? *move->tabs : 0;
}

[[nodiscard]] PendingEdit create_space_edit(
    const TextSpan& gap, std::int32_t spaces,
    const FormattingActionOptions& options) {
  if (gap.range.start.line == gap.range.end.line) {
    const auto existing =
        checked_count(static_cast<std::int64_t>(gap.range.end.character) -
                      gap.range.start.character);
    spaces = fit_into_options(spaces, existing, options);
  }
  return PendingEdit{.span = gap,
                     .newText = std::string(
                         static_cast<std::size_t>(std::max(spaces, 0)), ' ')};
}

[[nodiscard]] std::string repeat_newlines(std::int32_t count,
                                          std::string_view newlineStyle) {
  count = std::max(count, 0);
  std::string result;
  result.reserve(static_cast<std::size_t>(count) * newlineStyle.size());
  for (std::int32_t index = 0; index < count; ++index) result += newlineStyle;
  return result;
}

[[nodiscard]] PendingEdit create_line_edit(
    const TextSpan& gap, std::int32_t lines, const FormatContext& context,
    const FormattingActionOptions& options) {
  lines = fit_into_options(lines, line_count(gap.range), options);
  std::string newText = repeat_newlines(lines, context.newlineStyle);
  newText += indentation_text(context.indentation, context.options);
  return PendingEdit{.span = gap, .newText = std::move(newText)};
}

[[nodiscard]] PendingEdit create_tab_edit(const TextSpan& gap, bool hasPrevious,
                                          const FormatContext& context) {
  const std::int32_t lines =
      std::max(line_count(gap.range), hasPrevious ? 1 : 0);
  std::string newText = repeat_newlines(lines, context.newlineStyle);
  newText += indentation_text(context.indentation, context.options);
  return PendingEdit{.span = gap, .newText = std::move(newText)};
}

// A replacement comment, shifted as a block: the first line keeps whatever the
// margin edit put in front of it and every later line moves by the same delta,
// so a block comment's internal ` * ` alignment survives being indented.
[[nodiscard]] std::string reindent_text(std::string_view text,
                                        std::int32_t characterIncrease,
                                        const lsp::FormattingOptions& options) {
  if (characterIncrease == 0) return std::string(text);
  const std::string prefix = characterIncrease > 0
                                 ? whitespace_text(characterIncrease, options)
                                 : std::string();

  std::string result;
  result.reserve(text.size());
  bool firstLine = true;
  std::size_t cursor = 0;
  while (cursor <= text.size()) {
    const auto terminator = find_next_line_terminator(text, cursor);
    const std::size_t lineEnd =
        terminator.has_value() ? terminator->position : text.size();
    const std::string_view line = text.substr(cursor, lineEnd - cursor);

    if (firstLine) {
      result.append(line);
      firstLine = false;
    } else if (characterIncrease > 0) {
      result += prefix;
      result.append(line);
    } else {
      std::size_t trim = 0;
      auto remaining = static_cast<std::size_t>(
          -static_cast<std::int64_t>(characterIncrease));
      while (trim < line.size() && remaining > 0 &&
             is_horizontal_whitespace(line[trim])) {
        ++trim;
        --remaining;
      }
      result.append(line.substr(trim));
    }

    if (!terminator.has_value()) break;
    // The terminator's own bytes, verbatim: turning a lone '\r' into "\r\n"
    // would insert a byte that was never in the document.
    result.append(text.substr(terminator->position, terminator->length));
    cursor = terminator->position + terminator->length;
  }
  return result;
}

// A stretch of one line from its column 0 whose end column is a count of
// ASCII whitespace bytes, so it needs no conversion.
[[nodiscard]] TextSpan line_head(std::uint32_t line, TextOffset begin,
                                 TextOffset end, std::uint32_t endColumn) {
  return TextSpan{.begin = begin,
                  .end = end,
                  .range = lsp::Range{lsp::Position{line, 0},
                                      lsp::Position{line, endColumn}}};
}

// Only a comment's margin is rewritten, to `level` units, never the gap in
// front of it; a trailing comment keeps its column, the author's alignment.
void emit_hidden_edits(NodeId previous, NodeId hidden, const TextSpan& gap,
                       std::int32_t level, const std::string* replacement,
                       const FormatContext& context, EditSink& sink) {
  const CstNode& node = context.cst[hidden];
  const TextDocument& document = context.document.textDocument();
  const std::uint32_t startLine = gap.range.end.line;
  const bool previousOnSameLine =
      previous != kNoNode && gap.range.start.line == startLine;

  const TextOffset lineStart = document.offsetAt(Position{startLine, 0});
  const std::string_view margin =
      document.getText().substr(lineStart, node.begin - lineStart);

  const std::int32_t existing =
      existing_indentation_count(margin, context.options);
  const std::int32_t expected =
      previousOnSameLine ? existing : indentation_count(level, context.options);
  const std::int32_t increase =
      checked_count(static_cast<std::int64_t>(expected) - existing);

  // A rewritten comment is replaced whole, and its first line's margin travels
  // inside the replacement when the comment starts the line -- a separate
  // margin edit would collide with the node replacement at the same offset.
  if (replacement != nullptr) {
    std::string foldedIndent;
    if (!previousOnSameLine && increase != 0) {
      if (lineStart == node.begin)
        foldedIndent = indentation_text(level, context.options);
      else
        sink.push(PendingEdit{
            .span = TextSpan{.begin = lineStart,
                             .end = node.begin,
                             .range = lsp::Range{lsp::Position{startLine, 0},
                                                 gap.range.end}},
            .newText = indentation_text(level, context.options)});
    }
    std::string text =
        previousOnSameLine
            ? std::string(*replacement)
            : reindent_text(*replacement, expected, context.options);
    if (!foldedIndent.empty()) text.insert(0, foldedIndent);
    sink.push(PendingEdit{
        .span = TextSpan{.begin = node.begin,
                         .end = node.end,
                         .range =
                             lsp::Range{gap.range.end,
                                        to_lsp(document.positionAt(node.end))}},
        .newText = std::move(text)});
    return;
  }

  if (previousOnSameLine || increase == 0) return;

  // Margin only, one column-0 edit per line of the comment. No whole-node
  // replacement, so there is no offset to collide with -- and the comment's own
  // bytes are never rewritten.
  const std::string_view text = context.cst.text(hidden);
  std::uint32_t lineIndex = 0;
  std::size_t cursor = 0;
  // Use the same line boundaries as TextDocument, including a lone '\r'.
  while (cursor <= text.size()) {
    const auto terminator = find_next_line_terminator(text, cursor);
    const std::size_t lineEnd =
        terminator.has_value() ? terminator->position : text.size();
    const std::string_view line = text.substr(cursor, lineEnd - cursor);
    const std::uint32_t at = startLine + lineIndex;
    const TextOffset lineOffset = document.offsetAt(Position{at, 0});
    if (increase > 0) {
      sink.push(PendingEdit{
          .span = line_head(at, lineOffset, lineOffset, 0),
          .newText = whitespace_text(increase, context.options)});
    } else {
      // The first line's whitespace lives before the record, in the margin;
      // every later line's lives in the record's own text. The units come off
      // in the measure that counted them, not one byte each.
      const std::string_view own = lineIndex == 0 ? margin : line;
      std::int64_t left = -static_cast<std::int64_t>(increase);
      std::uint32_t removable = 0;
      std::string kept;
      while (left > 0) {
        const MarginPiece piece = first_margin_piece(own.substr(removable));
        if (piece.bytes == 0) break;
        const std::int64_t units = piece_units(piece, context.options);
        if (units <= left) {
          removable += piece.bytes;
          left -= units;
          continue;
        }
        // Part of the piece stays. A tab worth several columns cannot be cut,
        // so the columns it keeps are written as spaces.
        if (piece.tab) {
          ++removable;
          kept = whitespace_text(checked_count(units - left), context.options);
        } else {
          removable += spaces_for_units(piece.bytes, left, context.options);
        }
        break;
      }
      if (removable > 0)
        sink.push(PendingEdit{
            .span =
                line_head(at, lineOffset, lineOffset + removable, removable),
            .newText = std::move(kept)});
    }
    if (!terminator.has_value()) break;
    ++lineIndex;
    cursor = terminator->position + terminator->length;
  }
}

// The gap between two records, rewritten. `context.indentation` is mutated by a
// move carrying tabs and the caller restores it: that is how an `indent` on a
// composite record reaches everything inside it.
void emit_gap_edits(NodeId previous, NodeId node,
                    const FormattingAction& action, FormatContext& context,
                    EditSink& sink) {
  if (previous != kNoNode &&
      context.cst[previous].end > context.cst[node].begin)
    return;

  const TextSpan gap = gap_before(context, previous, node);
  const FormattingMove* move = find_fitting_move(gap.range, action);
  if (move == nullptr) return;

  if (move->tabs.has_value())
    context.indentation = checked_count(
        static_cast<std::int64_t>(context.indentation) + *move->tabs);

  if (move->characters.has_value()) {
    // The break ending a line comment must not be replaced by spaces.
    if (previous == kNoNode || context.cst[previous].hidden == 0)
      sink.push(create_space_edit(gap, *move->characters, action.options));
  } else if (move->lines.has_value()) {
    sink.push(create_line_edit(gap, *move->lines, context, action.options));
  } else if (move->tabs.has_value()) {
    sink.push(create_tab_edit(gap, previous != kNoNode, context));
  }
}

// The minimality, in one place. Overlapping edits collapse to the later one, an
// exact-range duplicate collapses the same way (two rules over one zero-width
// gap do not overlap by the `<` test), and then the pin: an edit whose new text
// is the text it replaces is not an edit.
[[nodiscard]] std::vector<lsp::TextEdit> finalize_edits(
    const Document& document, std::vector<PendingEdit> edits, bool sorted) {
  if (!sorted)
    std::ranges::stable_sort(
        edits, [](const PendingEdit& left, const PendingEdit& right) {
          return span_before(left.span, right.span);
        });

  std::vector<PendingEdit> filtered;
  for (const PendingEdit& edit : edits) {
    while (!filtered.empty() && edit.span.begin < filtered.back().span.end)
      filtered.pop_back();
    if (!filtered.empty() && filtered.back().span.begin == edit.span.begin &&
        filtered.back().span.end == edit.span.end)
      filtered.pop_back();
    filtered.push_back(edit);
  }

  std::vector<lsp::TextEdit> result;
  result.reserve(filtered.size());
  const std::string_view text = document.textDocument().getText();
  for (const PendingEdit& edit : filtered) {
    // Compared as-is: stripping '\r' here would make a CRLF document's lines
    // differ from the CRLF just produced, turning an already-formatted file
    // into a full-file diff of no-op edits.
    if (edit.newText ==
        text.substr(edit.span.begin, edit.span.end - edit.span.begin))
      continue;
    result.push_back(
        lsp::TextEdit{.range = edit.span.range, .newText = edit.newText});
  }
  return result;
}

// The roots of the physical forest, in document order. Leading trivia can be a
// root of its own (it precedes the entry rule), so neither `entry_root()` nor
// id 0 is a sound place to start: the tape is walked whole.
[[nodiscard]] std::vector<NodeId> forest_roots(const Cst& cst) {
  std::vector<NodeId> roots;
  NodeId next = cst.size();
  while (next > 0) {
    const NodeId root = next - 1;
    roots.push_back(root);
    next = cst.subtree_begin(root);
  }
  std::ranges::reverse(roots);
  return roots;
}

}  // namespace

std::string indentation_text(std::int32_t indentation,
                             const lsp::FormattingOptions& options) {
  indentation = std::max(indentation, 0);
  if (options.insertSpaces)
    return std::string(
        static_cast<std::size_t>(indentation_count(indentation, options)), ' ');
  return std::string(static_cast<std::size_t>(indentation), '\t');
}

void FormattingActions::collect(NodeId node, FormattingMode mode,
                                const FormattingAction& action) {
  const std::size_t k = key(node, mode);
  if (k >= _at.size()) return;
  std::uint32_t& slot = _at[k];
  if (slot == kNoAction) {
    slot = static_cast<std::uint32_t>(_pool.size());
    _pool.push_back(action);
  } else if (_pool[slot].options.priority <= action.options.priority) {
    _pool[slot] = action;
  }
}

void FormattingActions::replaceHidden(NodeId node, std::string text) {
  _replacements.insert_or_assign(node, std::move(text));
}

const FormattingAction* FormattingActions::find(NodeId node,
                                                FormattingMode mode) const {
  const std::size_t k = key(node, mode);
  if (k >= _at.size()) return nullptr;
  const std::uint32_t slot = _at[k];
  return slot == kNoAction ? nullptr : &_pool[slot];
}

const std::string* FormattingActions::replacement(NodeId node) const {
  const auto it = _replacements.find(node);
  return it == _replacements.end() ? nullptr : &it->second;
}

std::vector<lsp::TextEdit> run_formatting(
    const Document& document, const Cst& cst,
    const lsp::FormattingOptions& options,
    const std::optional<lsp::Range>& range, FormattingActions& actions,
    cursiv::function_ref<void(NodeId, std::int32_t, std::string_view)>
        visitHidden,
    const CancellationToken& token) {
  throw_if_cancelled(token);
  if (cst.size() == 0) return {};

  FormatContext context{
      .document = document,
      .cst = cst,
      .options = options,
      .indentation = 0,
      .newlineStyle = detect_newline_style(document.textDocument().getText())};

  struct Frame {
    NodeId id;
    std::int32_t saved;
    bool exit;
  };
  std::vector<Frame> stack;
  {
    const std::vector<NodeId> roots = forest_roots(cst);
    for (unsigned int root : std::views::reverse(roots))
      stack.push_back(Frame{.id = root, .saved = 0, .exit = false});
  }
  // The node after the one just popped, its children not yet pushed: its next
  // sibling, or the nearest ancestor's -- what an `append` targets.
  const auto next_node = [&stack]() {
    for (const Frame& frame : std::views::reverse(stack))
      if (!frame.exit) return frame.id;
    return kNoNode;
  };

  // An `append` whose target is a hidden record shapes that record's margin
  // at the record's own visit, from the level in force where the rule was
  // read; the record's own `prepend` takes precedence.
  struct PendingAppend {
    NodeId target = kNoNode;
    // Hidden callbacks may grow the action pool; retain its key, not a pointer.
    NodeId source = kNoNode;
    std::int32_t indentation = 0;
  };
  PendingAppend pendingAppend;

  std::vector<PendingEdit> edits;
  EditSink sink(edits, range);
  NodeId lastLeaf = kNoNode;
  while (!stack.empty()) {
    throw_if_cancelled(token);
    const Frame frame = stack.back();
    stack.pop_back();
    if (frame.exit) {
      context.indentation = frame.saved;
      continue;
    }

    const NodeId id = frame.id;
    const std::int32_t initial = context.indentation;
    const bool hidden = cst[id].hidden != 0;
    const FormattingAction* prepend = actions.find(id, FormattingMode::Prepend);

    if (hidden) {
      const TextSpan gap = gap_before(context, lastLeaf, id);
      const auto level = [&](const FormattingAction* own) {
        if (own != nullptr)
          return checked_count(static_cast<std::int64_t>(context.indentation) +
                               tabs_of(gap.range, own));
        if (pendingAppend.target == id)
          return checked_count(
              static_cast<std::int64_t>(pendingAppend.indentation) +
              tabs_of(gap.range, actions.find(pendingAppend.source,
                                              FormattingMode::Append)));
        return context.indentation;
      };
      // The margin the comment is about to get, which is what
      // `HiddenNodeFormatter::indentation()` reports.
      visitHidden(id, level(prepend), context.newlineStyle);
      // The callback may have just registered a rule for this very record,
      // which moves the pool: re-read before the uses below.
      prepend = actions.find(id, FormattingMode::Prepend);
      emit_hidden_edits(lastLeaf, id, gap, level(prepend),
                        actions.replacement(id), context, sink);
    }

    // A level lands on the record and travels down its subtree; a childless
    // record has no inside to carry it.
    if (prepend != nullptr && !hidden) {
      const std::int32_t before = context.indentation;
      emit_gap_edits(lastLeaf, id, *prepend, context, sink);
      if (cst.childless(id)) context.indentation = before;
    }

    if (const FormattingAction* append =
            actions.find(id, FormattingMode::Append);
        append != nullptr) {
      if (const NodeId next = next_node(); next == kNoNode) {
        // nothing follows: there is no gap to format
      } else if (cst[next].hidden != 0) {
        pendingAppend = PendingAppend{.target = next,
                                      .source = id,
                                      .indentation = context.indentation};
      } else {
        const std::int32_t before = context.indentation;
        emit_gap_edits(id, next, *append, context, sink);
        if (cst.childless(next)) context.indentation = before;
      }
    }

    if (cst.childless(id)) {
      lastLeaf = id;
      continue;
    }

    stack.push_back(Frame{.id = id, .saved = initial, .exit = true});
    // `reverse_children` yields the last child first, so pushing in that order
    // pops them back in document order.
    for (const NodeId child : cst.reverse_children(id))
      stack.push_back(Frame{.id = child, .saved = 0, .exit = false});
  }

  const bool sorted = sink.sorted();
  return finalize_edits(document, std::move(edits), sorted);
}

}  // namespace cursiv
