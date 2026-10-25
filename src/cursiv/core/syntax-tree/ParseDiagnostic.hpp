#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

// What went wrong, as a value. The parser states the fact: what it did,
// where, and which grammar element it was owed. The words belong to whoever
// shows them, composed from those three, so a second locale composes another
// sentence from the same row. Only a conversion carries text of its own,
// because what it has to say is about a value and no table holds it.
enum class DiagnosticKind : std::uint8_t {
  // The construct was admitted absent: no source text was consumed, and
  // `begin == end` is where an editor puts an insertion caret.
  inserted,
  // Source text was skipped to reach a point the parse could go on from.
  deleted,
  // Source text was consumed as the expected construct -- a misspelling.
  replaced,
  // The input ended while the construct was still owed.
  incomplete,
  // The tree parsed; turning a matched text into a value did not.
  conversion_error,
  // The span was settled by the repair's bound on work -- the cheapest
  // reading it reached -- not by the reading itself: told, never silent.
  bounded,
};

// A range and nothing else to situate it: the site that knows a node fills
// the range from it, and a document too broken to repair leaves no node to
// point at anyway.
struct ParseDiagnostic {
  DiagnosticKind kind = DiagnosticKind::conversion_error;
  // A position of the grammar (not a name, not a spelling) for most kinds --
  // the grammar navigates from it to whatever a reader shows -- but for an
  // insertion it is instead the terminal the reading was owed, since a
  // repair inserts bytes a token would match, not a grammar site.
  ElementId element = kNoElement;
  // The other tokens the reading could as cheaply have been given, cheapest
  // first: a state expects a set, and naming one of it alone reads as the
  // only way out of it.
  std::vector<ElementId> also{};
  TextOffset begin = 0;
  TextOffset end = 0;
  // A conversion's own sentence; empty for every syntax diagnostic, whose
  // words are the reader's to choose.
  std::string message;

  // From the parse or from a repair, as opposed to from a conversion; a bound
  // is the note of how far the repairs can be trusted, and repairs nothing.
  [[nodiscard]] constexpr bool isSyntax() const noexcept {
    return kind != DiagnosticKind::conversion_error &&
           kind != DiagnosticKind::bounded;
  }
};

}  // namespace cursiv
