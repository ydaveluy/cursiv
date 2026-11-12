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
  inserted,
  // Source text was skipped to reach a point the parse could go on from.
  deleted,
  // Source text was consumed as the expected construct -- a misspelling.
  replaced,
  // The input ended before the expected construct.
  incomplete,
  // The tree parsed; turning a matched text into a value did not.
  conversion_error,
  // The parser hit a work limit on this span and kept the reading it ranked
  // first, or the cheapest repair found; reported, never silent.
  bounded,
};

// Only a range locates it: a caller that knows a node fills the range from it.
struct ParseDiagnostic {
  DiagnosticKind kind = DiagnosticKind::conversion_error;
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

  // From the parse or a repair, not from a conversion; `bounded` is only a
  // note and repairs nothing.
  [[nodiscard]] constexpr bool isSyntax() const noexcept {
    return kind != DiagnosticKind::conversion_error &&
           kind != DiagnosticKind::bounded;
  }
};

}  // namespace cursiv
