#pragma once

#include <cstddef>
#include <cstdint>

// How far a document has been taken. A state implies every smaller one, so the
// order is total and a build resumes where it stopped.

namespace cursiv {

// Two of these rungs cost a barrier a build could do without -- but they are
// what a `waitUntil` waits on, so dropping them would drop the question.
enum class DocumentState : std::uint8_t {
  // The text changed and needs parsing again; whatever was derived from the
  // previous text is no longer valid.
  Changed = 0,
  // An AST exists. The structure can be walked; references cannot resolve yet.
  Parsed = 1,
  // The index holds this document's exported symbols: another document can
  // resolve to them.
  IndexedContent = 2,
  // The locally reachable symbols are computed, so a reference resolves
  // lazily -- to its target, or to nothing when it is out of scope.
  ComputedScopes = 3,
  // Every outgoing reference has been resolved or marked erroneous.
  Linked = 4,
  // The index holds this document's outgoing references, which is what says
  // which documents a change here affects.
  IndexedReferences = 5,
  // Validation has run; this is the phase a language server publishes.
  Validated = 6,
};

// How many rungs the ladder has, for a table indexed by one. Derived, so a
// rung added above cannot leave a table one short.
inline constexpr std::size_t kDocumentStateCount =
    static_cast<std::size_t>(DocumentState::Validated) + 1;

}  // namespace cursiv
