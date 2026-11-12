// A misspelling is repaired by re-reading, never by rewriting: a cold search
// over the mode's DFA, a bounded edit distance, says how far a terminal would
// have reached and at what price. Each engine plugs its table in as an adapter.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/shared/utils/inside_scalar.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Utf8.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// The most faults a walk makes. `repair_typo_cost` rates a fourth fault below
// a replacement only from 15 bytes, longer than most words a grammar spells,
// so the search does not try it.
inline constexpr unsigned kTypoFaults = 3;
inline constexpr unsigned kTypoRun = 4;    // bytes one fault may swallow
inline constexpr unsigned kTypoValid = 2;  // valid bytes each fault needs

// Edges explored at one disagreement, in byte-class order; the rest are given
// up. Every walk gives up the same ones, so a memo may keep what a walk finds.
inline constexpr unsigned kTypoEdges = 4;

// One point of the re-reading search: where it stands, and what it has spent
// to get there. `tail` says a fault still needs an unchanged byte after it, as
// a skipped run does; `head` that a head fault started it.
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

// A span and the terminal it is re-read as, never text. `cost` is the
// `repair_typo_cost` of its faults over the longer of span and terminal.
struct Typo {
  TextOffset end = 0;
  SymbolId term = kInvalidSymbol;
  std::uint32_t cost = kRepairReread;
};

// Where the walk stops reading: `reach` bytes past the widest ordinary token
// the point reads, `plain`, or the end `len` of the text.
inline TextOffset typo_horizon(TextOffset at, TextOffset plain, TextOffset len,
                               TextOffset reach) {
  const TextOffset far = plain > at ? plain : at;
  return len - far > reach ? far + reach : len;
}

// What a walk keeps of its points while it reads: given by a search that asks
// for many walks, so that one allocates nothing once an earlier one grew it.
struct TypoScratch {
  std::vector<TypoPoint> now;
  std::vector<TypoPoint> next;
  // Per place, the valid and spelt bytes of a point read there.
  FlatMap<std::uint64_t> seen;
  // `seen` holds a point of an earlier walk.
  bool dirty = false;
};

// What the paths of one walk share, for both engines: the text, the horizon,
// the document's end, the bytes read and the parse's stop. An adapter derives
// from it and adds its table.
struct TypoBounds {
  const char* src;
  // Where the search stops, and where the document stops: a token may end at
  // the second only.
  TextOffset extent;
  TextOffset docLen;
  // The bytes the walk stepped over, every path counted: an adapter's
  // `next_byte` adds one for each.
  std::uint64_t* read;
  // Set where one path reached the horizon short of the document, or all of
  // them together read `most`: the candidates found are then incomplete.
  bool* shortOf = nullptr;
  std::uint64_t most = ~std::uint64_t{0};
  // Where the points are kept; when null, the walk keeps its own.
  TypoScratch* scratch = nullptr;
  // Read once per point and per byte a path steps over; never when null.
  const CancellationPoll* stop = nullptr;

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
  void poll() const {
    if (stop != nullptr) stop->poll();
  }
};

// The distinct states a ghost advance, a byte never typed, can lead to.
inline std::span<const std::uint16_t> typo_successors(const std::uint32_t* base,
                                                      const std::uint16_t* succ,
                                                      std::uint16_t s) {
  return {succ + base[s], succ + base[s + 1u]};
}

// A best-first search from `from` over (position, state) with up to
// `kTypoFaults` faults: a skipped run, a ghost byte, a substitution or a
// transposition, made only where the author's byte disagrees with the DFA.
template <class Ad, class Emit>
CURSIV_ALWAYS_INLINE inline void typo_each(Ad ad, std::uint16_t start,
                                          TextOffset from, TextOffset plain,
                                          Emit emit) {
  const TextOffset len = ad.len();
  // A wrong letter is a code point, never half of one.
  const auto past_letter = [&](TextOffset at) {
    const auto width =
        static_cast<TextOffset>(utf8_width(static_cast<char>(ad.byte(at))));
    return width < len - at ? at + width : len;
  };

  // A word missing its first byte reads whole as another terminal, so a head
  // fault is allowed with no disagreement. It re-reads the span up to `plain`
  // and never re-cuts it; where no token reads, it reads to its terminal's end.
  const bool unread = plain == from;
  TypoScratch own;
  TypoScratch& x = ad.scratch != nullptr ? *ad.scratch : own;
  x.now.clear();
  x.next.clear();
  if (x.dirty) x.seen.reset();
  x.dirty = false;
  // A point of two faults or more is dropped where one read before it at the
  // same place, plain over head and free over tail, has read no fewer valid
  // and spelt bytes.
  const auto key = [](const TypoPoint& p, bool head, bool tail) {
    return std::uint64_t{p.pos} << 32 | std::uint64_t{p.state} << 2 |
           (head ? 2u : 0u) | (tail ? 1u : 0u);
  };
  const auto covers = [](std::uint64_t held, const TypoPoint& p) {
    return (held & 0xFFu) >= p.valid && held >> 8 >= p.spelt;
  };
  const auto first = [&](const TypoPoint& p) {
    if (p.faults < 2 || unread || p.pos > plain) return true;
    x.dirty = true;
    const auto beaten = [&](bool head, bool tail) {
      const std::uint64_t* held = x.seen.find(key(p, head, tail));
      return held != nullptr && covers(*held, p);
    };
    if (beaten(p.head, p.tail) || (p.head && beaten(false, p.tail)) ||
        (p.tail && beaten(p.head, false)) ||
        (p.head && p.tail && beaten(false, false)))
      return false;
    // The place keeps the point that covers the other, or the first.
    const auto [held, made] = x.seen.emplace(key(p, p.head, p.tail));
    if (made || (p.valid >= (held & 0xFFu) && p.spelt >= held >> 8))
      held = std::uint64_t{p.spelt} << 8 | p.valid;
    return true;
  };
  // One fault count after another: the plain walk, then the points its faults
  // make, then the head faults. A point at `kTypoFaults` pushes nothing.
  x.now.push_back(TypoPoint{from, start, 0, 0, 0, false, false});
  for (bool headed = false;;) {
    while (!x.now.empty()) {
      TypoPoint w = x.now.back();
      x.now.pop_back();
      if (!first(w)) continue;
      for (;;) {
        ad.poll();
        const bool word = !unread && w.pos <= plain;
        // A leading refusal applies to the source as written. Joining a later
        // fragment must not turn an originally reserved word into a name. Only
        // the unedited path can constrain the candidates this way.
        if (w.faults == 0 && !ad.original(w.state, w.pos)) return;
        // Past its first fault, a reading is the word in hand misspelt: it
        // reads no further than `plain`.
        if (w.faults > 1 && !word) break;
        if (w.faults != 0 && !w.tail && w.valid >= kTypoValid * w.faults &&
            (!w.head || w.pos == plain || unread)) {
          const TextOffset span = w.pos - from;
          const std::uint32_t cost =
              repair_typo_cost(w.faults, span > w.spelt ? span : w.spelt);
          if (cost < kRepairReplace) {
            const auto offer = [&](SymbolId t) {
              emit(Typo{w.pos, t, cost});
            };
            ad.accepted(w.state, w.pos, offer);
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
        // (a) The invalid bytes from `w.pos`, skipped where they stand, up to
        // the end of the letter they cut into.
        TextOffset k = 1;
        while (w.pos + k < len && k < kTypoRun &&
               ad.next_byte(w.state, ad.byte(w.pos + k)) == kDeadState)
          ++k;
        while (w.pos + k < len && inside_scalar(ad.src, w.pos + k, ad.docLen))
          ++k;
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
        // (b) A ghost advance over a byte never written, and (c) a
        // substitution: the same advance over one wrong byte. Every edge is
        // tried.
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

}  // namespace cursiv::engine
