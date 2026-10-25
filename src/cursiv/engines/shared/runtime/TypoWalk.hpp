// A misspelling is repaired by re-reading, never by rewriting: a cold walk over
// the mode's DFA -- k faults, no edit distance -- says how far a terminal the
// point admits would have reached, and the author's own span becomes its leaf.
// An engine names its table through an adapter; the search is written once.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

inline constexpr unsigned kTypoBudget = 1;  // faults allowed
inline constexpr unsigned kTypoRun = 4;     // bytes one fault may swallow
inline constexpr unsigned kTypoValid = 2;   // valid bytes each fault owes

// Edges explored at one disagreement, in the order the byte classes are
// numbered: a state with more is read through the first and the rest are
// declared given up. Which survive is the numbering's, not the grammar's, so
// this bounds the search and describes no property of it.
inline constexpr unsigned kTypoEdges = 4;
// Walks held at once, and never more: a head seeds two and pushes nothing;
// the plain walk seeds one and, at its only disagreement, pushes the skipped
// run, the swapped pair and two per edge -- and every point it pushes already
// stands at the budget, so none of them pushes again.
inline constexpr unsigned kTypoPaths = 2 + 1 + 1 + 2 * kTypoEdges;
static_assert(kTypoBudget == 1,
              "cursiv: the queue's room is counted for one fault per walk");

// One point of the re-reading walk: where it stands, and what it has spent to
// get there. `tail` says a fault still owes an unchanged byte after it: a
// skipped run always does, and a substitution does wherever no syntactic
// continuation can be compared.
struct TypoPoint {
  TextOffset pos = 0;
  std::uint16_t state = 0;
  std::uint8_t faults = 0;
  std::uint8_t valid = 0;
  bool tail = false;
};

// A span and the terminal it stands for -- never text: a typo is repaired by
// re-reading, so no corrected spelling exists anywhere.
struct Typo {
  TextOffset end = 0;
  SymbolId term = kInvalidSymbol;
  std::uint8_t faults = 0;
  bool ok = false;
};

// Where the walk stops reading: `reach` bytes past the widest ordinary token
// the point reads, `plain`, or the end `len` of the text.
inline TextOffset typo_horizon(TextOffset at, TextOffset plain, TextOffset len,
                               TextOffset reach) {
  const TextOffset far = plain > at ? plain : at;
  return len - far > reach ? far + reach : len;
}

// Where the walk could have gone, one entry per distinct state: a ghost
// advance stands for a byte never typed, and which byte is the search's
// question. Distinct states, not classes, or classes leading nowhere else would
// each cost a walk.
inline std::span<const std::uint16_t> typo_successors(const std::uint32_t* base,
                                                      const std::uint16_t* succ,
                                                      std::uint16_t s) {
  return {succ + base[s], succ + base[s + 1u]};
}

// Replays, from `from`, the DFA of the mode the adapter opens with at most
// `kTypoBudget` faults of one cost each: a run of invalid bytes skipped where
// it stands, a ghost advance over a byte never typed, their combination on one
// byte (a substitution), and a transposition for an adapter that `transposes`.
// All are explored at a disagreement, never the first that fits. What a bound
// gives up goes through `refused()`, which an adapter must not drop. The
// terminal and the frontier it must close on are ONE answer: asked apart, the
// walk names a terminal that another one's guard let through.
//
// `Ad` names one walk's table: `len` -- the horizon, the adapter's to set, or a
// file of faults costs its own square -- `byte`, `successors`, `next_byte`,
// `original`, `owed`, `refused`; optionally `alive` (drops a path where nothing
// it could offer is reachable), `spent` (stops the walk once its paths have
// read all it pays for) and `cut` (a horizon stop counts as a refusal).
// Inlined into each engine's one entry point: a frame between them costs the
// marshalling of a walk that most often refuses at once.
//
// Complete substitutions go only to readers that retain candidates: one longest
// answer cannot tell replacing the last byte from swallowing the next token.
template <bool CompleteSubstitution = true, class Ad, class Emit>
CURSIV_ALWAYS_INLINE inline void typo_each(Ad ad, std::uint16_t start,
                                          TextOffset from, TextOffset plain,
                                          Emit emit) {
  const TextOffset len = ad.len();
  // A wrong letter is a code point, never half of one.
  const auto past_letter = [&](TextOffset at) {
    const auto width = static_cast<TextOffset>(
        utf8_width(static_cast<char>(ad.byte(at))));
    return width < len - at ? at + width : len;
  };

  // A WORD MISSING ITS FIRST BYTE DISAGREES WITH NOTHING: it reads whole as
  // another terminal, so the head is the one place a fault is paid without a
  // disagreement, once per state the mode could have entered. It is anchored on
  // `plain`, where the widest alphabet's token there ends: A HEAD FAULT
  // RE-READS THAT SPAN AND NEVER RE-CUTS IT, or the walk swallows the token
  // behind it. Where no token reads (`reads_unread`), a head fault reads on to
  // its terminal's end.
  bool unread = false;
  if constexpr (requires { ad.reads_unread(); }) unread = plain == from;
  const auto heads = ad.successors(start);
  TypoPoint todo[kTypoPaths];
  for (unsigned h = 0; h <= heads.size(); ++h) {
    unsigned n = 0;
    if (h == 0) {
      todo[n++] = TypoPoint{from, start, 0, 0, false};
    } else {
      const std::uint16_t s = heads[h - 1];
      // (b) the byte was never written, and (c) it was written wrong.
      todo[n++] = TypoPoint{from, s, 1, 0, false};
      if (from < len)
        todo[n++] =
            TypoPoint{past_letter(from), s, 1, 0, !CompleteSubstitution};
    }
    while (n != 0) {
      TypoPoint w = todo[--n];
      for (;;) {
        // A leading refusal applies to the source as written. Joining a
        // later fragment must not turn an originally reserved word into a
        // name. Only the unedited path can constrain the candidates this way.
        if (w.faults == 0 && !ad.original(w.state, w.pos)) return;
        if (w.faults != 0 && !w.tail && w.valid >= kTypoValid * w.faults &&
            (h == 0 || w.pos == plain || unread)) {
          const auto offer = [&](SymbolId t) {
            emit(Typo{w.pos, t, w.faults, true});
          };
          if constexpr (requires { ad.owed(w.state, w.pos, offer); }) {
            ad.owed(w.state, w.pos, offer);
          } else {
            const SymbolId t = ad.owed(w.state, w.pos);
            if (t != kInvalidSymbol) offer(t);
          }
        }
        // A head fault offers only at `plain`: past it the path is spent.
        if (h != 0 && !unread && w.pos >= plain) break;
        if (w.pos >= len) {
          if constexpr (requires { ad.cut(); })
            if (ad.cut()) ad.refused();  // the horizon, not the document
          break;
        }
        if constexpr (requires { ad.spent(); })
          if (ad.spent()) {
            ad.refused();
            return;
          }
        const std::uint16_t nx = ad.next_byte(w.state, ad.byte(w.pos));
        if constexpr (requires { ad.alive(nx); })
          if (nx != kDeadState && nx != w.state && !ad.alive(nx)) break;
        if (nx != kDeadState) {
          w.state = nx;
          ++w.pos;
          if (w.valid < 255) ++w.valid;
          w.tail = false;
          continue;
        }
        // A repair is anchored on bytes the terminal really admits: without one
        // behind it, the walk would read a token out of nothing at all.
        if (w.faults >= kTypoBudget || w.valid == 0) break;
        const auto f = static_cast<std::uint8_t>(w.faults + 1);
        const auto open_path = [&](const TypoPoint& p) {
          assert(n < kTypoPaths && "a walk at the budget pushes nothing");
          todo[n++] = p;
        };
        // (a) The invalid bytes, skipped where they stand. Trivia has no
        // special status here: the DFA and the same fault budget govern every
        // byte. A repaired token can span fragments separated in the source.
        TextOffset k = 0;
        while (w.pos + k < len && k < kTypoRun &&
               ad.next_byte(w.state, ad.byte(w.pos + k)) == kDeadState)
          ++k;
        while (k != 0 && w.pos + k < len &&
               (ad.byte(w.pos + k) & 0xC0u) == 0x80u)
          ++k;
        if (k != 0) open_path(TypoPoint{w.pos + k, w.state, f, w.valid, true});
        if constexpr (requires { ad.transposes(); }) {
          if (w.pos + 1 < len) {
            const std::uint16_t one = ad.next_byte(w.state, ad.byte(w.pos + 1));
            const std::uint16_t two = one == kDeadState
                                          ? kDeadState
                                          : ad.next_byte(one, ad.byte(w.pos));
            if (two != kDeadState)
              open_path(TypoPoint{w.pos + 2, two, f, w.valid, false});
          }
        }
        // (b) The ghost advance, over a byte that was never written, and (c)
        // the substitution: the same advance, one wrong byte given up. Every
        // edge the DFA still has, since which one the author meant is exactly
        // what the arbitration above decides.
        const auto succ = ad.successors(w.state);
        if (succ.size() > kTypoEdges) ad.refused();
        for (std::size_t e = 0; e < succ.size() && e < kTypoEdges; ++e) {
          open_path(TypoPoint{w.pos, succ[e], f, w.valid, w.tail});
          if (k != 0)
            open_path(TypoPoint{past_letter(w.pos), succ[e], f, w.valid,
                                !CompleteSubstitution});
        }
        break;
      }
    }
  }
}

// One longest answer: the original ordering and final-substitution guard,
// without a second walk.
template <class Ad>
CURSIV_ALWAYS_INLINE inline Typo typo_walk(Ad ad, std::uint16_t start,
                                          TextOffset from, TextOffset plain) {
  Typo best;
  typo_each<false>(ad, start, from, plain, [&](const Typo& candidate) {
    if (!best.ok || candidate.faults < best.faults ||
        (candidate.faults == best.faults && candidate.end > best.end))
      best = candidate;
  });
  return best;
}

}  // namespace cursiv
