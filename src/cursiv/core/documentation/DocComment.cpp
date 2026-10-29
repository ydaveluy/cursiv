#include "cursiv/core/documentation/DocComment.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cursiv {

namespace {

// Byte classes, spelled rather than asked of <cctype>: those read the process
// locale, and a host's `setlocale` must not change what parses.
bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
         c == '\v';
}
bool is_alpha(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool is_alnum(char c) noexcept { return is_alpha(c) || (c >= '0' && c <= '9'); }

std::string_view trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Splits `content` into lines on \n, \r\n or a lone \r, as `TextDocument`
// counts them.
std::vector<std::string_view> get_lines(std::string_view content) {
  std::vector<std::string_view> lines;
  std::size_t begin = 0;
  while (true) {
    const auto newline = content.find_first_of("\r\n", begin);
    if (newline == std::string_view::npos) {
      lines.push_back(content.substr(begin));
      break;
    }
    lines.push_back(content.substr(begin, newline - begin));
    begin = newline + 1;
    if (content[newline] == '\r' && begin < content.size() &&
        content[begin] == '\n') {
      ++begin;
    }
  }
  return lines;
}

// Index of the first non-whitespace character at or after `index`, or the line
// length when only whitespace remains.
std::size_t skip_whitespace(std::string_view line, std::size_t index) {
  while (index < line.size() && is_space(line[index])) {
    ++index;
  }
  return index;
}

// Length of `line` once trailing whitespace is removed.
std::size_t last_character(std::string_view line) {
  std::size_t end = line.size();
  while (end > 0 && is_space(line[end - 1])) {
    --end;
  }
  return end;
}

// Returns the index after a leading `marker` (preceded by optional whitespace),
// or std::nullopt when the line does not start with it.
std::optional<std::size_t> match_prefix_marker(std::string_view line,
                                               std::string_view marker) {
  if (const auto begin = skip_whitespace(line, 0);
      line.compare(begin, marker.size(), marker) == 0) {
    return begin + marker.size();
  }
  return std::nullopt;
}

// Returns the index at which a trailing `marker` (with surrounding whitespace)
// begins, or std::nullopt when the line does not end with it.
std::optional<std::size_t> match_suffix_marker(std::string_view line,
                                               std::string_view marker) {
  const auto end = last_character(line);
  if (end < marker.size() ||
      line.compare(end - marker.size(), marker.size(), marker) != 0) {
    return std::nullopt;
  }
  std::size_t begin = end - marker.size();
  while (begin > 0 && is_space(line[begin - 1])) {
    --begin;
  }
  return begin;
}

// Tokenization

struct Token {
  enum class Type : std::uint8_t { Text, Tag, InlineTag, Break };
  Type type = Type::Text;
  std::string content;
  DocCommentRange range;
};

DocCommentPosition position(std::uint32_t line, std::size_t character) {
  return {line, static_cast<std::uint32_t>(character)};
}

// Matches a block `@tag` starting at `index` (after optional whitespace).
struct BlockTagMatch {
  std::size_t fullLength = 0;  // consumed length from `index`
  std::string value;           // "@name"
};

std::optional<BlockTagMatch> match_block_tag(std::string_view line,
                                             std::size_t index) {
  std::size_t cursor = skip_whitespace(line, index);
  if (cursor >= line.size() || line[cursor] != '@') {
    return std::nullopt;
  }
  const std::size_t nameStart = cursor;
  ++cursor;
  if (cursor < line.size() && is_alpha(line[cursor])) {
    ++cursor;
    while (cursor < line.size() && is_alnum(line[cursor])) {
      ++cursor;
    }
  }
  return BlockTagMatch{cursor - index,
                       std::string(line.substr(nameStart, cursor - nameStart))};
}

// One `{@tag ...}` occurrence inside a line fragment.
struct InlineTagMatch {
  std::size_t begin = 0;       // index of `{` in the fragment
  std::size_t fullLength = 0;  // length including `{` and `}`
  std::size_t whitespace = 0;  // whitespace between name and content
  std::string name;            // "@name"
  std::string content;         // content between name and `}` (may be empty)
  bool hasContent = false;
};

std::vector<InlineTagMatch> find_inline_tags(std::string_view fragment) {
  std::vector<InlineTagMatch> matches;
  std::size_t pos = 0;
  std::size_t boundary = 0;
  while (pos < fragment.size()) {
    if (fragment[pos] != '{') {
      ++pos;
      continue;
    }
    std::size_t cursor = pos + 1;
    if (cursor >= fragment.size() || fragment[cursor] != '@') {
      ++pos;
      continue;
    }
    const std::size_t nameStart = cursor;
    ++cursor;
    if (cursor >= fragment.size() || !is_alpha(fragment[cursor])) {
      ++pos;
      continue;
    }
    ++cursor;
    while (cursor < fragment.size() && is_alnum(fragment[cursor])) {
      ++cursor;
    }
    const std::size_t nameEnd = cursor;
    const std::size_t wsStart = cursor;
    cursor = skip_whitespace(fragment, cursor);
    const std::size_t contentStart = cursor;
    // A failed opener scanned up to `boundary` without a stop: the next one
    // resumes there, unless its own blanks already ran past it.
    boundary = std::max(boundary, cursor);
    while (boundary < fragment.size() && fragment[boundary] != '}' &&
           fragment[boundary] != '\n' && fragment[boundary] != '\r') {
      ++boundary;
    }
    cursor = boundary;
    if (cursor >= fragment.size() || fragment[cursor] != '}') {
      ++pos;
      continue;
    }
    InlineTagMatch match;
    match.begin = pos;
    match.fullLength = (cursor + 1) - pos;
    match.name = std::string(fragment.substr(nameStart, nameEnd - nameStart));
    match.whitespace = contentStart - wsStart;
    match.hasContent = cursor > contentStart;
    match.content =
        std::string(fragment.substr(contentStart, cursor - contentStart));
    matches.push_back(std::move(match));
    pos = cursor + 1;
  }
  return matches;
}

void build_inline_tokens(std::string_view fragment, std::uint32_t lineIndex,
                         std::size_t characterBase,
                         std::vector<Token>& tokens) {
  const auto matches = find_inline_tags(fragment);
  if (matches.empty()) {
    tokens.push_back({Token::Type::Text,
                      std::string(fragment),
                      {position(lineIndex, characterBase),
                       position(lineIndex, characterBase + fragment.size())}});
    return;
  }

  std::size_t lastIndex = 0;
  for (const auto& match : matches) {
    if (match.begin > lastIndex) {
      tokens.push_back(
          {Token::Type::Text,
           std::string(fragment.substr(lastIndex, match.begin - lastIndex)),
           {position(lineIndex, characterBase + lastIndex),
            position(lineIndex, characterBase + match.begin)}});
    }
    std::size_t offset = match.begin + 1;  // skip '{'
    tokens.push_back(
        {Token::Type::InlineTag,
         match.name,
         {position(lineIndex, characterBase + offset),
          position(lineIndex, characterBase + offset + match.name.size())}});
    offset += match.name.size();
    if (match.hasContent) {
      offset += match.whitespace;
      tokens.push_back({Token::Type::Text,
                        match.content,
                        {position(lineIndex, characterBase + offset),
                         position(lineIndex, characterBase + offset +
                                                 match.content.size())}});
    } else {
      tokens.push_back({Token::Type::Text,
                        "",
                        {position(lineIndex, characterBase + offset),
                         position(lineIndex, characterBase + offset)}});
    }
    lastIndex = match.begin + match.fullLength;
  }
  if (lastIndex < fragment.size()) {
    tokens.push_back({Token::Type::Text,
                      std::string(fragment.substr(lastIndex)),
                      {position(lineIndex, characterBase + lastIndex),
                       position(lineIndex, characterBase + fragment.size())}});
  }
}

std::vector<Token> tokenize(const std::vector<std::string_view>& lines,
                            DocCommentPosition start,
                            const DocCommentParseOptions& options) {
  std::vector<Token> tokens;
  std::uint32_t currentLine = start.line;
  std::size_t currentCharacter = start.character;

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const bool first = i == 0;
    const bool last = i + 1 == lines.size();
    std::string_view line = lines[i];
    std::size_t index = 0;

    if (first && !options.start.empty()) {
      if (const auto match = match_prefix_marker(line, options.start);
          match.has_value()) {
        index = *match;
      }
    } else if (!options.line.empty()) {
      if (const auto match = match_prefix_marker(line, options.line);
          match.has_value()) {
        index = *match;
      }
    }
    if (last && !options.end.empty()) {
      if (const auto truncate = match_suffix_marker(line, options.end);
          truncate.has_value()) {
        line = line.substr(0, *truncate);
      }
    }

    line = line.substr(0, last_character(line));
    if (const auto whitespaceEnd = skip_whitespace(line, index);
        whitespaceEnd >= line.size()) {
      if (!tokens.empty()) {
        const auto pos = position(currentLine, currentCharacter);
        tokens.push_back({Token::Type::Break, "", {pos, pos}});
      }
    } else {
      if (const auto tag = match_block_tag(line, index)) {
        // fullLength is measured from `index` (which may sit on the whitespace
        // before the '@'), so the begin position must skip that whitespace to
        // anchor on the '@'; the end and the advancement below stay unchanged.
        const auto begin =
            position(currentLine, currentCharacter + whitespaceEnd);
        const auto end =
            position(currentLine, currentCharacter + index + tag->fullLength);
        tokens.push_back({Token::Type::Tag, tag->value, {begin, end}});
        index += tag->fullLength;
        index = skip_whitespace(line, index);
      }
      if (index < line.size()) {
        build_inline_tokens(line.substr(index), currentLine,
                            currentCharacter + index, tokens);
      }
    }

    ++currentLine;
    currentCharacter = 0;
  }

  if (!tokens.empty() && tokens.back().type == Token::Type::Break) {
    tokens.pop_back();
  }
  return tokens;
}

// Parsing

struct ParseContext {
  const std::vector<Token>& tokens;
  std::size_t index = 0;
  DocCommentPosition start;
};

DocCommentNode make_line(std::string text, DocCommentRange range) {
  DocCommentNode node;
  node.kind = DocCommentNode::Kind::Line;
  node.text = std::move(text);
  node.range = range;
  return node;
}

DocCommentNode parse_line(ParseContext& context) {
  const auto& token = context.tokens[context.index++];
  return make_line(token.content, token.range);
}

DocCommentNode parse_tag(ParseContext& context, bool inlineTag);

DocCommentNode parse_inline(ParseContext& context) {
  if (context.tokens[context.index].type == Token::Type::InlineTag) {
    return parse_tag(context, true);
  }
  return parse_line(context);
}

DocCommentNode parse_text(ParseContext& context) {
  const auto& firstToken = context.tokens[context.index];
  DocCommentNode paragraph;
  paragraph.kind = DocCommentNode::Kind::Paragraph;
  while (context.index < context.tokens.size()) {
    if (const auto type = context.tokens[context.index].type;
        type == Token::Type::Break || type == Token::Type::Tag) {
      break;
    }
    paragraph.children.push_back(parse_inline(context));
  }
  // The range covers the children: a parent that closed on its last inline's
  // head token ended before its own content.
  paragraph.range = {firstToken.range.start,
                     paragraph.children.empty()
                         ? firstToken.range.end
                         : paragraph.children.back().range.end};
  return paragraph;
}

DocCommentNode parse_tag(ParseContext& context, bool inlineTag) {
  const auto& tagToken = context.tokens[context.index++];
  DocCommentNode tag;
  tag.kind = DocCommentNode::Kind::Tag;
  tag.name = tagToken.content.substr(1);  // drop leading '@'
  tag.inlineTag = inlineTag;

  // An inline tag opening the content belongs to the tag too: `@see {@link A}`
  // and `@see x {@link A}` keep their link the same way.
  if (const bool hasContent =
          context.index < context.tokens.size() &&
          (context.tokens[context.index].type == Token::Type::Text ||
           (!inlineTag &&
            context.tokens[context.index].type == Token::Type::InlineTag));
      !hasContent) {
    tag.range = tagToken.range;
    return tag;
  }
  if (inlineTag) {
    auto line = parse_line(context);
    tag.range = {tagToken.range.start, line.range.end};
    tag.children.push_back(std::move(line));
  } else {
    auto text = parse_text(context);
    tag.range = {tagToken.range.start, text.range.end};
    tag.children = std::move(text.children);
  }
  return tag;
}

void append_empty_line(const Token& token, DocCommentNode* element) {
  if (element != nullptr) {
    element->children.push_back(make_line("", token.range));
    // The parent stays covering: the appended line is its content too.
    element->range.end = token.range.end;
  }
}

std::optional<DocCommentNode> parse_element(ParseContext& context,
                                            DocCommentNode* last) {
  using enum Token::Type;
  const auto& next = context.tokens[context.index];
  if (next.type == Tag) {
    return parse_tag(context, false);
  }
  if (next.type == Text || next.type == InlineTag) {
    return parse_text(context);
  }
  append_empty_line(next, last);
  ++context.index;
  return std::nullopt;
}

DocComment parse_comment(ParseContext& context) {
  const DocCommentRange empty{context.start, context.start};
  if (context.tokens.empty()) {
    return DocComment{{}, empty};
  }
  std::vector<DocCommentNode> elements;
  while (context.index < context.tokens.size()) {
    if (auto element = parse_element(
            context, elements.empty() ? nullptr : &elements.back());
        element.has_value()) {
      elements.push_back(std::move(*element));
    }
  }
  const auto start =
      elements.empty() ? context.start : elements.front().range.start;
  const auto end = elements.empty() ? context.start : elements.back().range.end;
  return DocComment{std::move(elements), {start, end}};
}

// Rendering

std::string fill_newlines(std::string_view text) {
  return text.ends_with('\n') ? "\n" : "\n\n";
}

// Concatenates inline children, inserting a newline when the next child starts
// on a later line than the current one.
template <typename Render>
std::string render_inlines(const std::vector<DocCommentNode>& inlines,
                           Render&& render) {
  std::string text;
  for (std::size_t i = 0; i < inlines.size(); ++i) {
    text += render(inlines[i]);
    if (i + 1 < inlines.size() &&
        inlines[i + 1].range.start.line > inlines[i].range.start.line) {
      text += '\n';
    }
  }
  return text;
}

// A URI needs a scheme and cannot start its decoded path with // without
// an authority. Invalid targets remain plain text.
std::string render_link_default(std::string_view target,
                                std::string_view display) {
  const auto colon = target.find(':');
  const bool scheme =
      colon != std::string_view::npos && colon > 0 &&
      (is_alnum(target[0]) || target[0] == '_') &&
      std::ranges::all_of(target.substr(1, colon - 1), [](char c) {
        return is_alnum(c) || c == '_' || c == '+' || c == '.' || c == '-';
      });
  if (!scheme) return std::string(target);
  std::string_view path = target.substr(colon + 1);
  if (path.starts_with("//")) {
    const auto end = path.find_first_of("/?#", 2);
    if ((end == std::string_view::npos ? path.size() : end) > 2)
      return std::format("[{}]({})", display, target);
    path.remove_prefix(2);
  }
  path = path.substr(0, path.find_first_of("?#"));
  const auto slash = [&] {
    if (path.starts_with('/')) {
      path.remove_prefix(1);
      return true;
    }
    if (path.starts_with("%2f") || path.starts_with("%2F")) {
      path.remove_prefix(3);
      return true;
    }
    return false;
  };
  if (slash() && slash()) return std::string(target);
  return std::format("[{}]({})", display, target);
}

std::optional<std::string> render_inline_tag(
    std::string_view name, const std::string& content,
    const DocCommentRenderOptions& options) {
  if (name != "link" && name != "linkplain" && name != "linkcode") {
    return std::nullopt;
  }
  std::string target = content;
  std::string display = content;
  // target|display: split on '|' first, then on the first blank; both sides
  // trimmed, and an empty display falls back to the target.
  auto separator = content.find('|');
  if (separator == std::string::npos) {
    separator = content.find_first_of(" \t");
  }
  if (separator != std::string::npos) {
    target = content.substr(0, separator);
    display = content.substr(separator + 1);
  }
  target = std::string(trim(target));
  display = std::string(trim(display));
  if (display.empty()) {
    display = target;
  }
  if (name == "linkcode" ||
      (name == "link" &&
       options.link == DocCommentRenderOptions::LinkStyle::Code)) {
    display = "`" + display + "`";
  }
  if (options.renderLink) {
    if (auto rendered = options.renderLink(target, display)) {
      return rendered;
    }
  }
  return render_link_default(target, display);
}

std::string tag_marker(DocCommentRenderOptions::TagStyle style) {
  using enum DocCommentRenderOptions::TagStyle;
  switch (style) {
    case Italic:
      return "*";
    case Bold:
      return "**";
    case BoldItalic:
      return "***";
    case Plain:
      break;
  }
  return "";
}

}  // namespace

namespace {

[[nodiscard]] auto tags_named(const std::vector<DocCommentNode>& elements,
                              std::string_view name) {
  return elements | std::views::filter([name](const DocCommentNode& element) {
           return element.kind == DocCommentNode::Kind::Tag &&
                  element.name == name;
         });
}

}  // namespace

const DocCommentNode* DocComment::getTag(std::string_view name) const {
  for (const DocCommentNode& element : tags_named(elements, name))
    return &element;
  return nullptr;
}

std::vector<const DocCommentNode*> DocComment::getTags(
    std::string_view name) const {
  std::vector<const DocCommentNode*> tags;
  for (const DocCommentNode& element : tags_named(elements, name))
    tags.push_back(&element);
  return tags;
}

std::string DocCommentNode::toString() const {
  switch (kind) {
    case Kind::Line:
      return text;
    case Kind::Paragraph:
      return render_inlines(children, [](const DocCommentNode& child) {
        return child.toString();
      });
    case Kind::Tag:
      break;
  }

  std::string content = render_inlines(
      children, [](const DocCommentNode& child) { return child.toString(); });
  std::string result = "@" + name;
  if (children.size() == 1) {
    result += " " + content;
  } else if (children.size() > 1) {
    result += "\n" + content;
  }
  return inlineTag ? "{" + result + "}" : result;
}

std::string DocCommentNode::toMarkdown(
    const DocCommentRenderOptions& options) const {
  switch (kind) {
    case Kind::Line:
      return text;
    case Kind::Paragraph:
      return render_inlines(children, [&options](const DocCommentNode& child) {
        return child.toMarkdown(options);
      });
    case Kind::Tag:
      break;
  }

  std::string content =
      render_inlines(children, [&options](const DocCommentNode& child) {
        return child.toMarkdown(options);
      });

  if (options.renderTag) {
    if (auto rendered = options.renderTag(name, content, inlineTag)) {
      return *rendered;
    }
  }
  if (inlineTag) {
    if (auto rendered = render_inline_tag(name, content, options)) {
      return *rendered;
    }
  }

  const auto marker = tag_marker(options.tag);
  std::string result = marker + "@" + name + marker;
  if (children.size() == 1) {
    result += " — " + content;
  } else if (children.size() > 1) {
    result += "\n" + content;
  }
  return inlineTag ? "{" + result + "}" : result;
}

std::string DocComment::toString() const {
  std::string value;
  for (const auto& element : elements) {
    if (value.empty()) {
      value = element.toString();
    } else {
      value += fill_newlines(value) + element.toString();
    }
  }
  return std::string(trim(value));
}

std::string DocComment::toMarkdown(
    const DocCommentRenderOptions& options) const {
  std::string value;
  for (const auto& element : elements) {
    if (value.empty()) {
      value = element.toMarkdown(options);
    } else {
      value += fill_newlines(value) + element.toMarkdown(options);
    }
  }
  return std::string(trim(value));
}

bool is_doc_comment(std::string_view comment,
                    const DocCommentParseOptions& options) {
  const auto lines = get_lines(comment);
  if (lines.empty()) {
    return false;
  }
  const bool startsOk =
      options.start.empty() ||
      match_prefix_marker(lines.front(), options.start).has_value();
  const bool endsOk =
      options.end.empty() ||
      match_suffix_marker(lines.back(), options.end).has_value();
  return startsOk && endsOk;
}

DocComment parse_doc_comment(std::string_view comment, DocCommentPosition start,
                             const DocCommentParseOptions& options) {
  const auto lines = get_lines(comment);
  const auto tokens = tokenize(lines, start, options);
  ParseContext context{tokens, 0, start};
  return parse_comment(context);
}

}  // namespace cursiv
