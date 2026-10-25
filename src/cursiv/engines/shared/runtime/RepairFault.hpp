// The gestures a repair makes with no token read between them: one fault, and
// one note once it closes. Both engines state a fault the same way.
#pragma once

#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

struct RepairFault {
  bool open = false;
  bool ins = false, rep = false, del = false, abandon = false;
  // The first terminal invented, a misspelling's terminal, what an abandoned
  // construct owed, and the token a deletion resumes on.
  ElementId first = kNoElement;
  ElementId read = kNoElement;
  ElementId owed = kNoElement;
  ElementId resume = kNoElement;
  // Where the text stopped when the fault opened: an insertion's caret.
  TextOffset point = 0;
  // The author's bytes the fault takes.
  TextOffset begin = 0, end = 0;
  // The terminals the first insertion could as cheaply have been.
  std::vector<ElementId> also;
};

// The columns of a `ParseDiagnostic`, buffered until the parse ends.
struct RepairNote {
  DiagnosticKind kind = DiagnosticKind::deleted;
  ElementId element = kNoElement;
  TextOffset begin = 0;
  TextOffset end = 0;
  std::vector<ElementId> also;
};

inline RepairNote repair_note(RepairFault& f) {
  RepairNote n{DiagnosticKind::deleted, f.resume, f.begin, f.end, {}};
  if (f.abandon) {
    n.kind = DiagnosticKind::incomplete;
    n.element = f.owed;
  } else if (f.rep || (f.ins && f.del)) {
    n.kind = DiagnosticKind::replaced;
    n.element = f.rep ? f.read : f.first;
  } else if (f.ins) {
    n.kind = DiagnosticKind::inserted;
    n.element = f.first;
    n.begin = f.point;
    n.end = f.point;
  }
  if (f.ins) n.also = std::move(f.also);
  return n;
}

}  // namespace cursiv
