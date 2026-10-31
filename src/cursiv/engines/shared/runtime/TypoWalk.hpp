// A misspelling is repaired by re-reading, never by rewriting: a cold search
// over the mode's DFA -- a bounded edit distance between the author's bytes
// and what the DFA spells -- says how far a terminal the point admits would
// have reached, at what price, and the author's own span becomes its leaf.
// An engine names its table through an adapter; the search is written once.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The most faults a walk makes. `repair_typo_cost` prices a fourth under a
// different word's only from 15 bytes, longer than the words a grammar spells
// as a rule: the work it would take is not paid for.
inline constexpr unsigned kTypoFaults = 3;
inline constexpr unsigned kTypoRun = 4;    // bytes one fault may swallow
inline constexpr unsigned kTypoValid = 2;  // valid bytes each fault owes

// Edges explored at one disagreement, in the order the byte classes are
// numbered: a state with more is read through the first and the rest are
// declared given up. Which survive is the numbering's, not the grammar's, so
// this bounds the search and describes no property of it.
inline constexpr unsigned kTypoEdges = 4;

// One point of the re-reading search: where it stands, and what it has spent
// to get there. `tail` says a fault still owes an unchanged byte after it, as
// a skipped run does; `head` that a head fault set it out.
struct TypoPoint {
  TextOffset pos = 0;
  std::uint16_t state = 0;
  std::uint8_t faults = 0;
  std::uint8_t valid = 0;
  // The terminal's bytes read so far: the author's valid ones, and those a
  // fault read in place of a byte written or never written.
  TextOffset spelt = 0;
  bool tail = false;
  bool head = false;
};

// A span and the terminal it stands for -- never text: a typo is repaired by
// re-reading, so no corrected spelling exists anywhere. `cost` is what the
// re-reading pays, `repair_typo_cost` of its faults over the longer of its
// span and the terminal's bytes.
struct Typo {
  TextOffset end = 0;
  SymbolId term = kInvalidSymbol;
  std::uint8_t faults = 0;
  std::uint32_t cost = kRepairReread;
};

// Where the walk stops reading: `reach` bytes past the widest ordinary token
// the point reads, `plain`, or the end `len` of the text.
inline TextOffset typo_horizon(TextOffset at, TextOffset plain, TextOffset len,
                               TextOffset reach) {
  const TextOffset far = plain > at ? plain : at;
  return len - far > reach ? far + reach : len;
}

// What a walk keeps of its points while it reads: lent by a search that asks
// for many walks, so that one allocates nothing once an earlier one grew it.
struct TypoScratch {
  std::vector<TypoPoint> now;
  std::vector<TypoPoint> next;
  FlatMap<std::uint8_t> seen;
  // `seen` holds a point of an earlier walk.
  bool dirty = false;
};

// What the paths of one walk share, said once for both engines: the text, the
// horizon and the document's end, and what the paths together have read
// against what they are paid for. An adapter derives from it and adds its
// table; `typo_each` reads `len`, `byte`, `cut` and `spent` from here.
struct TypoBounds {
  const char* src;
  // Where the search stops, and where the DOCUMENT stops. They are two facts:
  // a token may end at the second, and at the first the walk has only run out
  // of horizon.
  TextOffset extent;
  TextOffset docLen;
  // The bytes the walk stepped over, every path counted: an adapter's
  // `next_byte` adds one for each.
  std::uint64_t* read;
  // Set where one path reached the horizon short of the document, or all of
  // them together read `most`: the candidates found are then incomplete.
  bool* shortOf = nullptr;
  std::uint64_t most = ~std::uint64_t{0};
  // Where the points are kept; null, the walk keeps its own.
  TypoScratch* scratch = nullptr;

  TextOffset len() const { return extent; }
  // Asked where a path reaches `extent`.
  bool cut() const {
    if (extent < docLen && shortOf != nullptr) *shortOf = true;
    return extent < docLen;
  }
  bool spent() const {
    if (*read <= most) return false;
    if (shortOf != nullptr) *shortOf = true;
    return true;
  }
  unsigned char byte(TextOffset p) const {
    return static_cast<unsigned char>(src[p]);
  }
};

// Where the walk could have gone, one entry per distinct state: a ghost
// advance stands for a byte never typed, and which byte is the search's
// question. Distinct states, not classes, or classes leading nowhere else would
// each cost a walk.
inline std::span<const std::uint16_t> typo_successors(const std::uint32_t* base,
                                                      const std::uint16_t* succ,
                                                      std::uint16_t s) {
  return {succ + base[s], succ + base[s + 1u]};
}

// Replays, from `from`, the DFA of the mode the adapter opens with, as a
// best-first search over (position, state) with up to `kTypoFaults` faults of
// one each: a run of invalid bytes skipped where it stands, a ghost advance
// over a byte never typed, their combination on one byte (a substitution), and
// a transposition. Points are read by fault count, a point at its fewest, so
// the search costs the points it reaches, not the paths to them. A byte the
// DFA reads is read: faults are made where the author's byte disagrees, as an
// edit distance along one spelling only ever needs, and there all of them are
// explored, never the first that fits. A reading is offered at
// `repair_typo_cost` of its faults, and not at all where that is a different
// word's price.
// What a bound gives up goes through `refused()`, which an adapter must not
// drop. The terminal and the frontier it must close on are ONE answer: asked
// apart, the walk names a terminal that another one's guard let through.
//
// `Ad` names one walk's table: `len` -- the horizon, the adapter's to set --
// `byte`, `successors`, `next_byte`, `original`, `owed` (hands each terminal
// owed at a point to a callback), `refused`, `spent` (stops the walk once its
// paths have read all it pays for) and `cut` (a horizon stop counts as a
// refusal); optionally `alive` (drops a path where nothing it could offer is
// reachable). `TypoBounds` gives `len`, `byte`, `spent` and `cut`.
// Inlined into each engine's one entry point: a frame between them costs the
// marshalling of a walk that most often refuses at once.
template <class Ad, class Emit>
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
  // behind it. Where no token reads (`plain == from`), a head fault reads on to
  // its terminal's end.
  const bool unread = plain == from;
  TypoScratch own;
  TypoScratch& x = ad.scratch != nullptr ? *ad.scratch : own;
  x.now.clear();
  x.next.clear();
  if (x.dirty) x.seen.reset();
  x.dirty = false;
  // A point two faults or more make in the word is dominated by one made
  // before it -- with no more faults -- at the same place, which owes no
  // more: plain over head, free over tail. From a point the DFA reads one
  // path, so the points faults make are the ones to tell apart. Those of one
  // fault are apart already, each a different gesture at the plain walk's one
  // disagreement or at the head; past the word a path has one fault and
  // makes no other.
  const auto key = [](const TypoPoint& p, bool head, bool tail) {
    return std::uint64_t{p.pos} << 32 | std::uint64_t{p.state} << 2 |
           (head ? 2u : 0u) | (tail ? 1u : 0u);
  };
  const auto first = [&](const TypoPoint& p) {
    if (p.faults < 2 || unread || p.pos > plain) return true;
    x.dirty = true;
    if (!x.seen.emplace(key(p, p.head, p.tail)).second) return false;
    return !(p.head && x.seen.find(key(p, false, p.tail)) != nullptr) &&
           !(p.tail && x.seen.find(key(p, p.head, false)) != nullptr) &&
           !(p.head && p.tail && x.seen.find(key(p, false, false)) != nullptr);
  };
  // One fault count after another, each stack read last pushed first: the
  // plain walk alone, then the points its faults make, then the head faults
  // -- (b) the first byte never written, and (c) written wrong -- the first
  // head first. A point at `kTypoFaults` pushes nothing.
  x.now.push_back(TypoPoint{from, start, 0, 0, 0, false, false});
  for (bool headed = false;;) {
    while (!x.now.empty()) {
      TypoPoint w = x.now.back();
      x.now.pop_back();
      if (!first(w)) continue;
      for (;;) {
        const bool word = !unread && w.pos <= plain;
        // A leading refusal applies to the source as written. Joining a later
        // fragment must not turn an originally reserved word into a name. Only
        // the unedited path can constrain the candidates this way.
        if (w.faults == 0 && !ad.original(w.state, w.pos)) return;
        // PAST ITS FIRST FAULT, A READING IS THE WORD IN HAND MISSPELT: it
        // reads no further than `plain`, and where no token reads there is no
        // word. More faults joining fragments would read a long token out of
        // the text behind it at a word's price.
        if (w.faults > 1 && !word) break;
        if (w.faults != 0 && !w.tail && w.valid >= kTypoValid * w.faults &&
            (!w.head || w.pos == plain || unread)) {
          const TextOffset span = w.pos - from;
          const std::uint32_t cost =
              repair_typo_cost(w.faults, span > w.spelt ? span : w.spelt);
          if (cost < kRepairReplace) {
            const auto offer = [&](SymbolId t) {
              emit(Typo{w.pos, t, w.faults, cost});
            };
            ad.owed(w.state, w.pos, offer);
          }
        }
        // A head fault offers only at `plain`: past it the path is spent.
        if (w.head && !unread && w.pos >= plain) break;
        if (w.pos >= len) {
          if (ad.cut()) ad.refused();  // the horizon, not the document
          break;
        }
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
          ++w.spelt;
          w.tail = false;
          continue;
        }
        // A repair is anchored on bytes the terminal really admits: without
        // one behind it, the walk would read a token out of nothing at all.
        if (w.faults >= kTypoFaults || w.valid == 0 || (w.faults != 0 && !word))
          break;
        const auto f = static_cast<std::uint8_t>(w.faults + 1);
        const TextOffset spelt = w.spelt + 1;
        // (a) The invalid bytes, skipped where they stand. Trivia has no
        // special status here: the DFA and the same faults govern every byte.
        // A repaired token can span fragments separated in the source. The
        // byte at `w.pos` is the first: the step above refused it.
        TextOffset k = 1;
        while (w.pos + k < len && k < kTypoRun &&
               ad.next_byte(w.state, ad.byte(w.pos + k)) == kDeadState)
          ++k;
        while (w.pos + k < len && (ad.byte(w.pos + k) & 0xC0u) == 0x80u) ++k;
        x.next.push_back(
            TypoPoint{w.pos + k, w.state, f, w.valid, w.spelt, true, w.head});
        if (w.pos + 1 < len) {
          const std::uint16_t one = ad.next_byte(w.state, ad.byte(w.pos + 1));
          const std::uint16_t two = one == kDeadState
                                        ? kDeadState
                                        : ad.next_byte(one, ad.byte(w.pos));
          if (two != kDeadState)
            x.next.push_back(TypoPoint{w.pos + 2, two, f, w.valid, spelt + 1,
                                       false, w.head});
        }
        // (b) The ghost advance, over a byte that was never written, and (c)
        // the substitution: the same advance, one wrong byte given up. Every
        // edge the DFA still has, since which one the author meant is exactly
        // what the arbitration above decides.
        const auto succ = ad.successors(w.state);
        if (succ.size() > kTypoEdges) ad.refused();
        for (std::size_t e = 0; e < succ.size() && e < kTypoEdges; ++e) {
          x.next.push_back(
              TypoPoint{w.pos, succ[e], f, w.valid, spelt, w.tail, w.head});
          x.next.push_back(TypoPoint{past_letter(w.pos), succ[e], f, w.valid,
                                     spelt, false, w.head});
        }
        break;
      }
    }
    if (!headed) {
      headed = true;
      const auto heads = ad.successors(start);
      for (std::size_t h = heads.size(); h-- > 0;) {
        x.now.push_back(TypoPoint{from, heads[h], 1, 0, 1, false, true});
        if (from < len)
          x.now.push_back(
              TypoPoint{past_letter(from), heads[h], 1, 0, 1, false, true});
      }
      x.now.insert(x.now.end(), x.next.begin(), x.next.end());
      x.next.clear();
      continue;
    }
    if (x.next.empty()) return;
    std::swap(x.now, x.next);
  }
}

}  // namespace cursiv
