#pragma once

/// Low-level LSP test primitives — a marker parser and the `*Params`
/// builders. These are the building blocks the
/// high-level `expect…` helpers (see Testing.hpp) are made of, exposed publicly
/// so a downstream language can build custom probes: parse markers out of a
/// source string, turn a cursor offset into an `lsp::*Params`, then call the
/// headless feature API in <pegium/lsp/services/LanguageServerFeatures.hpp>.
///
/// Markers: `<|>` is a cursor index, `<| … |>` marks a range.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/syntax-tree/CstNode.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/core/workspace/TextDocument.hpp>

namespace pegium::testing {

/// The marker spellings used when parsing a source string.
struct Markers {
  std::string indexMarker = "<|>";
  std::string rangeStartMarker = "<|";
  std::string rangeEndMarker = "|>";
};

/// A source string with its markers stripped, plus the resolved cursor offsets
/// (`indices`) and `[begin, end)` ranges, in the order they appeared.
struct MarkedText {
  std::string output;
  std::vector<TextOffset> indices;
  std::vector<std::pair<TextOffset, TextOffset>> ranges;
};

/// Strips the markers from `text` and records cursor offsets and ranges.
[[nodiscard]] inline MarkedText replaceIndices(std::string_view text,
                                               const Markers &markers = {}) {
  MarkedText result;
  result.output.reserve(text.size());
  std::vector<TextOffset> rangeStack;
  for (std::size_t index = 0; index < text.size();) {
    if (!markers.indexMarker.empty() &&
        text.compare(index, markers.indexMarker.size(), markers.indexMarker) ==
            0) {
      result.indices.push_back(static_cast<TextOffset>(result.output.size()));
      index += markers.indexMarker.size();
      continue;
    }
    if (!markers.rangeStartMarker.empty() &&
        text.compare(index, markers.rangeStartMarker.size(),
                     markers.rangeStartMarker) == 0) {
      rangeStack.push_back(static_cast<TextOffset>(result.output.size()));
      index += markers.rangeStartMarker.size();
      continue;
    }
    if (!markers.rangeEndMarker.empty() &&
        text.compare(index, markers.rangeEndMarker.size(),
                     markers.rangeEndMarker) == 0) {
      if (rangeStack.empty()) {
        throw std::logic_error("pegium::testing::replaceIndices: range-end "
                               "marker without a matching range-start marker");
      }
      result.ranges.emplace_back(
          rangeStack.back(), static_cast<TextOffset>(result.output.size()));
      rangeStack.pop_back();
      index += markers.rangeEndMarker.size();
      continue;
    }
    result.output.push_back(text[index++]);
  }
  if (!rangeStack.empty()) {
    throw std::logic_error(
        "pegium::testing::replaceIndices: unclosed range-start marker");
  }
  return result;
}

/// Convenience overload reading the markers off an `Expected…` struct
/// (Testing.hpp). The returned offsets are still validated by the caller.
template <typename Expected>
[[nodiscard]] MarkedText replaceIndicesOf(const Expected &base) {
  return replaceIndices(base.text, Markers{base.indexMarker,
                                           base.rangeStartMarker,
                                           base.rangeEndMarker});
}

// --- params builders -------------------------------------------------------

[[nodiscard]] inline ::lsp::DocumentUri
documentUri(const workspace::Document &document) {
  return ::lsp::DocumentUri(::lsp::Uri::parse(document.uri));
}

/// Fills the `TextDocumentPositionParams` fields every LSP request shares,
/// directly on a value-initialized `Params` (each result type derives from
/// `TextDocumentPositionParams`).
template <typename Params>
[[nodiscard]] Params positionParams(const workspace::Document &document,
                                    TextOffset offset) {
  Params params{};
  params.textDocument.uri = documentUri(document);
  params.position = document.textDocument().positionAt(offset);
  return params;
}

[[nodiscard]] inline ::lsp::TextDocumentPositionParams
textDocumentPositionParams(const workspace::Document &document,
                           TextOffset offset) {
  return positionParams<::lsp::TextDocumentPositionParams>(document, offset);
}

[[nodiscard]] inline ::lsp::CompletionParams
completionParams(const workspace::Document &document, TextOffset offset) {
  return positionParams<::lsp::CompletionParams>(document, offset);
}

[[nodiscard]] inline ::lsp::DefinitionParams
definitionParams(const workspace::Document &document, TextOffset offset) {
  return positionParams<::lsp::DefinitionParams>(document, offset);
}

[[nodiscard]] inline ::lsp::HoverParams
hoverParams(const workspace::Document &document, TextOffset offset) {
  return positionParams<::lsp::HoverParams>(document, offset);
}

[[nodiscard]] inline ::lsp::DocumentHighlightParams
documentHighlightParams(const workspace::Document &document, TextOffset offset) {
  return positionParams<::lsp::DocumentHighlightParams>(document, offset);
}

[[nodiscard]] inline ::lsp::ReferenceParams
referenceParams(const workspace::Document &document, TextOffset offset,
                bool includeDeclaration) {
  auto params = positionParams<::lsp::ReferenceParams>(document, offset);
  params.context.includeDeclaration = includeDeclaration;
  return params;
}

// --- result helpers --------------------------------------------------------

/// Applies `edits` to `document`'s text (edits are sorted last-first so earlier
/// offsets stay valid) and returns the resulting string.
[[nodiscard]] inline std::string
applyTextEdits(const workspace::Document &document,
               std::vector<::lsp::TextEdit> edits) {
  const auto &textDocument = document.textDocument();
  auto text = std::string(textDocument.getText());
  std::ranges::sort(edits, [&textDocument](const auto &left, const auto &right) {
    return textDocument.offsetAt(left.range.start) >
           textDocument.offsetAt(right.range.start);
  });
  for (const auto &edit : edits) {
    const auto begin = textDocument.offsetAt(edit.range.start);
    const auto end = textDocument.offsetAt(edit.range.end);
    // Guard against a reversed range: end - begin would underflow (unsigned),
    // matching the guard the production TextDocument applies.
    const auto count = end >= begin ? end - begin : 0U;
    text.replace(begin, count, edit.newText);
  }
  return text;
}

/// Flattens an `lsp::Hover`'s contents into plain text.
[[nodiscard]] inline std::string hoverText(const ::lsp::Hover &hover) {
  return std::visit(
      []<typename T>(const T &value) -> std::string {
        using Value = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Value, ::lsp::MarkupContent>) {
          return value.value;
        } else if constexpr (std::same_as<Value, ::lsp::MarkedString>) {
          return std::holds_alternative<std::string>(value)
                     ? std::get<std::string>(value)
                     : std::get<::lsp::MarkedString_Language_Value>(value).value;
        } else {
          std::string combined;
          for (const auto &entry : value) {
            if (!combined.empty()) {
              combined += '\n';
            }
            combined += hoverText(::lsp::Hover{.contents = entry});
          }
          return combined;
        }
      },
      hover.contents);
}

[[nodiscard]] inline bool samePosition(const ::lsp::Position &actual,
                                       const text::Position &expected) {
  return actual.line == expected.line && actual.character == expected.character;
}

/// Resolves a range selector (none = all marked ranges in order, a single
/// index, or an explicit list) into a concrete list of marked-range indices.
[[nodiscard]] inline std::vector<std::size_t> resolveRangeIndices(
    const std::variant<std::monostate, std::size_t, std::vector<std::size_t>>
        &rangeIndex,
    std::size_t markedRangeCount) {
  std::vector<std::size_t> ranges;
  if (std::holds_alternative<std::vector<std::size_t>>(rangeIndex)) {
    // Drop out-of-range selectors: a caller typo must not become an
    // out-of-bounds index into the marked-range vector at the call site (the
    // resulting count mismatch then surfaces as a clean assertion failure).
    for (const auto index : std::get<std::vector<std::size_t>>(rangeIndex)) {
      if (index < markedRangeCount) {
        ranges.push_back(index);
      }
    }
  } else if (std::holds_alternative<std::size_t>(rangeIndex)) {
    if (const auto index = std::get<std::size_t>(rangeIndex);
        index < markedRangeCount) {
      ranges.push_back(index);
    }
  } else {
    ranges.resize(markedRangeCount);
    for (std::size_t index = 0; index < markedRangeCount; ++index) {
      ranges[index] = index;
    }
  }
  return ranges;
}

// --- semantic tokens -------------------------------------------------------

/// One decoded semantic token (the LSP delta encoding expanded), with the
/// token type resolved against the provider legend and absolute text offsets.
struct DecodedSemanticToken {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  std::uint32_t length = 0;
  std::string tokenType; ///< legend name, or "" if the index is unknown
  std::uint32_t tokenModifiers = 0;
  TextOffset begin = 0; ///< absolute start offset in `document`
  TextOffset end = 0;   ///< absolute end offset in `document`
};

/// Expands an `lsp::SemanticTokens` packed array (5 ints per token:
/// deltaLine, deltaChar, length, tokenType, tokenModifiers) into absolute
/// tokens, mapping the type index through `tokenTypeLegend` (index → name).
[[nodiscard]] inline std::vector<DecodedSemanticToken>
decodeSemanticTokens(const ::lsp::SemanticTokens &tokens,
                     const std::vector<std::string> &tokenTypeLegend,
                     const workspace::Document &document) {
  std::vector<DecodedSemanticToken> decoded;
  const auto &data = tokens.data;
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  for (std::size_t index = 0; index + 5 <= data.size(); index += 5) {
    const auto deltaLine = static_cast<std::uint32_t>(data[index]);
    const auto deltaChar = static_cast<std::uint32_t>(data[index + 1]);
    if (deltaLine == 0) {
      character += deltaChar;
    } else {
      line += deltaLine;
      character = deltaChar;
    }
    DecodedSemanticToken token;
    token.line = line;
    token.character = character;
    token.length = static_cast<std::uint32_t>(data[index + 2]);
    const auto typeIndex = static_cast<std::size_t>(data[index + 3]);
    token.tokenType =
        typeIndex < tokenTypeLegend.size() ? tokenTypeLegend[typeIndex] : "";
    token.tokenModifiers = static_cast<std::uint32_t>(data[index + 4]);
    token.begin = document.textDocument().offsetAt(line, character);
    token.end = document.textDocument().offsetAt(line, character + token.length);
    decoded.push_back(std::move(token));
  }
  return decoded;
}

} // namespace pegium::testing
