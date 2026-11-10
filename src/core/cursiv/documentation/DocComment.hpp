#pragma once

// Structured documentation comments: tokenizer, parser and renderers. A
// comment parses into paragraphs, tags and lines, each with its source range.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv {

/// Zero-based position in the source document. `character` counts bytes of
/// the comment's text, not UTF-16 units; `offset` is a byte offset.
struct DocCommentPosition {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  std::uint32_t offset = 0;
};

/// Half-open source range `[start, end)`.
struct DocCommentRange {
  DocCommentPosition start;
  DocCommentPosition end;
};

/// How a comment is delimited. Markers are literal text: `start` and `line`
/// after leading whitespace, `end` at the end of the last line.
struct DocCommentParseOptions {
  std::string start = "/**";
  std::string line = "*";
  std::string end = "*/";
};

struct DocCommentRenderOptions {
  /// Emphasis of a `@tag` marker.
  enum class TagStyle : std::uint8_t { Plain, Italic, Bold, BoldItalic };
  TagStyle tag = TagStyle::Italic;

  /// Rendering of a `{@link}` target when `renderLink` gives none.
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

/// One node of a parsed comment: a `Paragraph` of inline nodes, a `Tag` whose
/// children hold its content (an inline tag holds at most one line), or a
/// `Line` of plain text.
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

struct DocComment {
  /// Top-level nodes only: paragraphs and block tags. Inline tags are in their
  /// paragraph's or tag's children, so `getTag` and `getTags` do not see them.
  std::vector<DocCommentNode> elements;
  /// From the first element's start to the last one's end, delimiters
  /// excluded; an empty comment gives its start position.
  DocCommentRange range;

  /// Returns the first top-level block tag named `name` in document order, or
  /// `nullptr`. Exact match, case sensitive, `name` without its `@`. The
  /// pointer stays valid until `elements` is next modified.
  [[nodiscard]] const DocCommentNode* getTag(std::string_view name) const;
  /// Every top-level block tag named `name`, in document order; the pointers
  /// stay valid until `elements` is next modified.
  [[nodiscard]] std::vector<const DocCommentNode*> getTags(
      std::string_view name) const;

  [[nodiscard]] std::string toString() const;
  [[nodiscard]] std::string toMarkdown(
      const DocCommentRenderOptions& options = {}) const;
};

/// Whether `comment` starts with `options.start` and ends with `options.end`,
/// whitespace tolerated. An empty marker disables its half of the test.
[[nodiscard]] bool is_doc_comment(std::string_view comment,
                                  const DocCommentParseOptions& options = {});

/// Parses `comment`; an empty marker in `options` disables its role. `start`
/// is where the comment sits in the document, and anchors every node range.
[[nodiscard]] DocComment parse_doc_comment(
    std::string_view comment, DocCommentPosition start = {},
    const DocCommentParseOptions& options = {});

}  // namespace cursiv
