// The records a repair lays on the tree and the one note a fault makes: the
// run in hand is told when a gesture is made at another cursor, or the parse
// ends.
#pragma once

#include <utility>

#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlRepairRun.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/shared/runtime/RepairFault.hpp"

namespace cursiv {

// A record the parse laid, not the author: the tree stays navigable and the
// bit says which parts to trust.
inline void ll_mark(LlContext& c, NodeId id) {
  c.tape.stream().at(id).recovered = true;
  c.repaired.mark();
}

// A word read as the one it misspells was read for real: its leaf keeps the
// position that read it, and says it was repaired. Ends never decrease, so
// the walk stops below the word.
template <const auto& T>
inline void ll_mark_misspelt(LlContext& c, TextOffset begin, TextOffset end) {
  CstRun& run = c.tape.stream();
  for (NodeCount x = run.records(); x-- > 0;) {
    CstNode& r = run.at(x);
    if (r.end < end) return;
    if (r.subtreeSize != 1 || r.hidden || r.begin != begin || r.end != end)
      continue;
    if (T.stamp.state(r) != 0) r.recovered = true;
    return;
  }
}

// The token a deletion resumes on, as the alphabet it dropped under reads it.
template <const auto& T>
inline ElementId ll_resume_at(const LlContext& c, TextOffset from,
                              std::uint16_t mode) {
  for (TextOffset p = from;;) {
    if (p >= c.len) return kEofSymbol;
    const Scan sc = ll_match_text<T>(c.src, c.len, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p) return kNoElement;
    if (!T.terms[sc.term].trivia) return sc.term;
    p = sc.end;
  }
}

// The run in hand, as the one note its fault makes: what it invented where
// it took nothing, what it re-read or invented over bytes it took, what it
// dropped, or the construct it gave up.
template <const auto& T>
inline void ll_note_run(LlContext& c) {
  LlRepairRun& f = c.run;
  if (!f.open) return;
  f.open = false;
  if (f.speltEnd != f.spelt) ll_mark_misspelt<T>(c, f.spelt, f.speltEnd);
  if (f.del && f.resume == kNoElement)
    f.resume = ll_resume_at<T>(c, f.end, f.dropMode);
  RepairNote n = repair_note(f);
  c.tape.note_repair(n.kind, n.element, n.begin, n.end, std::move(n.also));
}

// A gesture joins the run in hand where nothing was read since its last one.
template <const auto& T>
inline LlRepairRun& ll_run(LlContext& c) {
  if (c.run.open && c.run.at == c.at) return c.run;
  ll_note_run<T>(c);
  c.run = LlRepairRun{};
  c.run.open = true;
  c.run.point = c.at;
  c.run.at = c.at;
  return c.run;
}

}  // namespace cursiv
