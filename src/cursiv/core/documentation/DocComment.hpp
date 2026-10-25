#pragma once

// Structured documentation comment model: tokenizer, parser and renderers.
// A comment parses into a tree of paragraphs, tags and lines, each carrying
// its source range; the markers (`/**`, `*`, `*/`) are configurable through
// `DocCommentParseOptions`.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv {

/// Zero-based line/character position inside the source document.
/// `character` counts BYTES of the comment's text, not UTF-16 units.
struct DocCommentPosition {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
};

/// Half-open source range `[start, end)`.
struct DocCommentRange {
  DocCommentPosition start;
  DocCommentPosition end;
};

/// Options controlling how a comment is delimited.
///
/// Markers are matched as literal text: `start`/`line` after leading
/// whitespace at the beginning of a line, `end` (with surrounding whitespace)
/// at the end of the last line.
struct DocCommentParseOptions {
  std::string start = "/**";
  std::string line = "*";
  std::string end = "*/";
};

/// Options controlling how a parsed comment is rendered to Markdown.
struct DocCommentRenderOptions {
  /// Emphasis applied to a `@tag` marker. Defaults to italic.
  enum class TagStyle : std::uint8_t { Plain, Italic, Bold, BoldItalic };
  TagStyle tag = TagStyle::Italic;

  /// Default rendering for `{@link}` targets. Defaults to plain.
  enum class LinkStyle : std::uint8_t { Plain, Code };
  LinkStyle link = LinkStyle::Plain;

  /// Custom tag renderer. Receives the tag name, its rendered content and
  /// whether it is inline. Return the Markdown to use, or `std::nullopt` to
  /// fall back to the default rendering.
  std::function<std::optional<std::string>(
      std::string_view name, std::string_view content, bool inlineTag)>
      renderTag;

  /// Custom link renderer for inline `{@link target display}` tags. Return the
  /// Markdown link, or `std::nullopt` to fall back to the default rendering.
  std::function<std::optional<std::string>(std::string_view target,
                                           std::string_view display)>
      renderLink;
};

/// One node of a parsed documentation comment.
///
/// A node is one of:
/// - `Paragraph`: a block of text whose `children` are the inline nodes (lines
///   and inline tags) it is composed of.
/// - `Tag`: a `@tag`. A block tag's `children` hold its content, lines and
///   inline tags; an inline tag (`{@link ...}`, `inlineTag == true`) holds at
///   most one line.
/// - `Line`: a single line of plain text in `text`.
struct DocCommentNode {
  enum class Kind : std::uint8_t { Paragraph, Tag, Line };

  Kind kind = Kind::Line;
  /// Covers the node's own content, children included; an inline tag's `{`
  /// and `}` stay outside. A field a `Kind` does not use keeps its default.
  DocCommentRange range;

  /// Plain text content of a `Line` node: never a newline, the leading blank
  /// after a paragraph's line marker kept (a tag's content drops it), the
  /// line's trailing blank removed -- an inline fragment keeps its own.
  std::string text;

  /// Tag name (without the leading `@`) of a `Tag` node.
  std::string name;
  /// Whether a `Tag` node is an inline `{@tag ...}` rather than a block `@tag`.
  bool inlineTag = false;

  /// Inline nodes of a `Paragraph`, or the content of a `Tag`.
  std::vector<DocCommentNode> children;

  [[nodiscard]] std::string toString() const;
  [[nodiscard]] std::string toMarkdown(
      const DocCommentRenderOptions& options = {}) const;
};

/// A fully parsed documentation comment.
struct DocComment {
  /// Top-level nodes only: paragraphs and block tags. Inline tags live inside
  /// their paragraph's (or tag's) children -- which is why the tag queries
  /// below never see them.
  std::vector<DocCommentNode> elements;
  /// From the first element's start to the last one's end -- the delimiters
  /// are outside, and an empty comment degenerates to its start position.
  /// `character` counts BYTES of the comment's own text, not UTF-16 units.
  DocCommentRange range;

  /// Returns the first top-level block tag named `name` in document order, or
  /// `nullptr`. Exact match, case sensitive, `name` without its `@`. The
  /// pointer stays valid until `elements` is next modified.
  [[nodiscard]] const DocCommentNode* getTag(std::string_view name) const;
  /// Every top-level block tag named `name`, in document order; the pointers
  /// stay valid until `elements` is next modified.
  [[nodiscard]] std::vector<const DocCommentNode*> getTags(
      std::string_view name) const;

  /// Renders the comment to plain text.
  [[nodiscard]] std::string toString() const;
  /// Renders the comment to Markdown.
  [[nodiscard]] std::string toMarkdown(
      const DocCommentRenderOptions& options = {}) const;
};

/// Returns whether `comment` is delimited as a documentation comment under
/// `options`: its first line starts with `start` and its last line ends with
/// `end`, leading and trailing whitespace tolerated. Middle lines are never
/// examined and `options.line` is not read. An empty marker disables its half
/// of the test.
[[nodiscard]] bool is_doc_comment(std::string_view comment,
                                  const DocCommentParseOptions& options = {});

/// Parses `comment` into a structured documentation model. An empty marker in
/// `options` disables its role, as it does for `is_doc_comment`.
///
/// `start` is the position the comment occupies in the source document, used to
/// anchor every node range; it defaults to the document origin.
[[nodiscard]] DocComment parse_doc_comment(
    std::string_view comment, DocCommentPosition start = {},
    const DocCommentParseOptions& options = {});

}  // namespace cursiv
