#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv {

// What went wrong, as a value: what the parser did, where, and which grammar
// element it expected. Whoever shows it writes the message; only a conversion
// error carries its own text.
enum class DiagnosticKind : std::uint8_t {
  // A missing construct was assumed: no source text was consumed, and
  // `begin == end` is where an editor puts an insertion caret.
  Inserted,
  // Source text was skipped to reach a point the parse could go on from.
  Deleted,
  // Source text was consumed as the expected construct -- a misspelling.
  Replaced,
  // A construct was given up: the text it read stays as written, and the
  // element is what it still needed after that text.
  Incomplete,
  // The tree parsed; turning a matched text into a value did not.
  ConversionError,
  // Recovery reached its work limit and kept the best repair found.
  Bounded,
};

// Only a range locates it: a caller that knows a node fills the range from it.
struct ParseDiagnostic {
  DiagnosticKind kind = DiagnosticKind::ConversionError;
  // A deletion that starts with a line's indentation whose blanks differ from
  // the open block's in the same place: a tab against a space.
  bool mixedIndent = false;
  // A position of the grammar for most kinds, or, for an insertion, the
  // terminal the reading expected.
  ElementId element = kNoElement;
  // The other tokens that were just as cheap here, in grammar order: naming
  // only one would suggest it is the only fix.
  std::vector<ElementId> also{};
  TextOffset begin = 0;
  TextOffset end = 0;
  // A conversion error's own message; empty for a syntax diagnostic, which the
  // caller words.
  std::string message;

  // Whether the parser found a syntax error in the input.
  [[nodiscard]] constexpr bool isSyntax() const noexcept {
    return kind != DiagnosticKind::ConversionError && kind != DiagnosticKind::Bounded;
  }
};

}  // namespace cursiv
