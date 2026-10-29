#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/text/TextRange.hpp"

// Text a language rewrites before its tokens are read -- Java's Unicode
// escapes, C's line splices -- and the map back to the document. A
// translator fills one; the parser reads its text, and the tree, the
// diagnostics and completion keep the document's offsets.

namespace cursiv {

class SourceTranslation {
 public:
  explicit SourceTranslation(std::string_view source) noexcept
      : _source(source) {}

  // What a translator writes. `replace` calls follow the source in order and
  // do not overlap; the text between them is kept as it is.
  void replace(std::size_t begin, std::size_t end, std::string_view spelling);
  // A fault of the source itself, such as a malformed escape.
  void error(std::size_t begin, std::size_t end, std::string message);

  // One replaced piece: its bytes in the document and in the text.
  struct Piece {
    TextRange source;
    TextRange text;
  };

  // The text the parser reads, once `finish` has kept the source's tail.
  [[nodiscard]] std::string_view text() const noexcept { return _text; }
  [[nodiscard]] std::span<const ParseDiagnostic> errors() const noexcept {
    return _errors;
  }
  // Whether the translator wrote anything: when not, the document is parsed
  // as it is.
  [[nodiscard]] bool changed() const noexcept {
    return !_pieces.empty() || !_errors.empty();
  }
  void finish();

  // A range of the text in the document. A bound strictly inside a
  // replacement widens to the whole replaced source.
  [[nodiscard]] TextRange original(TextRange range) const noexcept;
  // A document offset in the text; none strictly inside a replaced piece.
  [[nodiscard]] std::optional<TextOffset> translated(
      TextOffset offset) const noexcept;
  // The spans a parse of the text wrote, moved to the document's offsets, or
  // back to the text's.
  void to_document(std::span<CstNode> records) const noexcept;
  void to_document(std::span<ParseDiagnostic> diagnostics) const noexcept;
  void to_text(std::span<CstNode> records) const noexcept;
  void to_text(std::span<ParseDiagnostic> diagnostics) const noexcept;

 private:
  std::string_view _source;
  // How much of the source the pieces have consumed.
  TextOffset _kept = 0;
  std::string _text;
  std::vector<Piece> _pieces;
  std::vector<ParseDiagnostic> _errors;
};

// What a language writes: fill `out`, or leave it untouched when `source`
// needs no change.
using SourceTranslator = void (*)(std::string_view source,
                                  SourceTranslation& out);

}  // namespace cursiv
