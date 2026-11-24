#include "cursiv/engine/recovery/Typos.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>

#include "cursiv/engine/recovery/ModeLexer.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

namespace {

// The most faults a walk makes, the bytes one may swallow, the valid bytes
// each needs, and the edges tried where the text disagrees.
constexpr unsigned kFaults = 3;
constexpr unsigned kFaultRun = 4;
constexpr unsigned kFaultValid = 2;
constexpr std::size_t kFaultEdges = 4;
// The bytes of a word each fault past its first takes.
constexpr std::uint32_t kFaultBytes = 5;

// What reading a word of `length` bytes as a terminal costs with `faults`
// faults, up to `kReplaceCost`: one fault up to 4 bytes, two from 5, three
// from 10.
constexpr std::uint32_t typo_cost(std::uint32_t faults, std::uint32_t length) {
  const std::uint64_t extra = std::uint64_t{faults > 1 ? faults - 1 : 0} * kFaultBytes;
  const std::uint64_t share = std::uint64_t{length} + 1;
  if (extra >= share) return kReplaceCost;
  return kRereadCost + static_cast<std::uint32_t>((kReplaceCost - kRereadCost) * extra / share);
}

// One point of the walk: where it stands and what it spent to get there.
// `tail` says a fault still needs an unchanged byte after it; `head` that a
// fault on the first byte began it; `clean` where its token may end at the
// earliest: what the lexer reads past that end is the author's, and so is a
// byte after a fault that needs one.
struct Point {
  TextOffset pos = 0;
  TextOffset clean = 0;
  std::uint16_t state = 0;
  std::uint8_t faults = 0;
  std::uint8_t valid = 0;
  // The terminal's bytes read so far: the author's valid ones, and those a
  // fault read in place of a byte written or never written.
  TextOffset spelt = 0;
  bool tail = false;
  bool head = false;
};

}  // namespace

std::vector<Typo> typos(const ModeLexer& mode, std::vector<std::uint64_t> want, TextOffset at,
                        TextOffset plain, LexWork& work) {
  const auto reach = work.limit;
  Machine& m = mode.m;
  const Tables& t = m.t;
  const auto len = static_cast<TextOffset>(m.end - m.src);
  const std::string_view text(m.src, len);
  want[0] &= ~std::uint64_t{1};
  const auto empty = [&] { return std::ranges::all_of(want, [](auto word) { return word == 0; }); };
  if (empty()) return {};
  // The state a byte leads to, none where no wanted terminal ends there or
  // past it.
  const auto step = [&](std::uint16_t state, TextOffset at) {
    if (!work.spend()) return std::uint16_t{0};
    const std::uint16_t to = mode.step(state, byte_at(m.src + at));
    return to == state || to == 0 || mode.reaches(to, want) ? to : std::uint16_t{0};
  };
  const auto successors = [&](std::uint16_t state) {
    const auto* list = t.lexLeads + t.lexLeadAt[state];
    return std::span(list + 1, *list) |
           std::views::filter([&](auto to) { return mode.lives(to) && mode.reaches(to, want); });
  };

  // Faults on the first byte must end at `plain`, the widest token the text
  // holds there; one inside may join what follows it.
  if (plain - at >= reach && plain < len) {
    work.cut = true;
    return {};
  }
  const TextOffset far = plain > at ? plain : at;
  const TextOffset end = len - far > reach ? far + static_cast<TextOffset>(reach) : len;

  // Each terminal and end once, at its least cost, with when it was found.
  struct Found {
    Typo typo;
    std::size_t when;
  };
  std::vector<Found> found;
  std::size_t clock = 0;
  std::unordered_map<std::uint64_t, std::size_t> places;
  const auto offer = [&](const Point& w, std::uint16_t cost) {
    const Ending e = mode.ending(w.state, w.pos, work);
    // What the lexer reads past the end to end it there is the author's.
    if (e.terminal == kNoToken || e.end < w.clean ||
        ((want[e.terminal / 64] >> (e.terminal % 64)) & 1) == 0) return;
    // A token read again ends at a word's edge, unless its own boundary says.
    if (e.end < len &&
        (detail::scalar_start(text, e.end) != e.end || (!e.bounded && m.word_at(m.src + e.end))))
      return;
    const auto [place, fresh] =
        places.try_emplace(std::uint64_t{e.terminal} << 32 | e.end, found.size());
    if (fresh)
      found.push_back({{e.end, e.terminal, cost}, clock++});
    else if (cost < found[place->second].typo.cost)
      found[place->second] = {{e.end, e.terminal, cost}, clock++};
  };
  // A wrong letter is a scalar, never half of one.
  const auto past_letter = [&](TextOffset p) {
    std::size_t width;
    scalarAt(m.src + p, len - p, width);
    return p + std::min<TextOffset>(end - p, width ? width : 1);
  };

  // The state past the two bytes at `p` read in each other's place, or none.
  const auto swapped = [&](std::uint16_t state, TextOffset p) {
    const std::uint16_t one = p + 1 < end ? step(state, p + 1) : std::uint16_t{0};
    return one == 0 ? std::uint16_t{0} : step(one, p);
  };

  // A word missing its first byte reads whole as another terminal, so a
  // fault on the first byte needs no disagreement. It reads the span up to
  // `plain`, or where no token reads, to its terminal's end.
  const bool unread = plain == at;
  std::vector<Point> now, later;
  // Per place, the valid and spelt bytes of a point of two faults or more
  // read there: a later one that reads no more of either is dropped.
  std::unordered_map<std::uint64_t, std::uint64_t> seen;
  const auto place_of = [](const Point& p, bool head, bool tail) {
    return std::uint64_t{p.pos} << 32 | std::uint64_t{p.state} << 2 | (head ? 2u : 0u) |
           (tail ? 1u : 0u);
  };
  const auto first = [&](const Point& p) {
    if (p.faults < 2 || unread || p.pos > plain) return true;
    const auto beaten = [&](bool head, bool tail) {
      const auto held = seen.find(place_of(p, head, tail));
      return held != seen.end() && (held->second & 0xFFu) >= p.valid &&
             held->second >> 8 >= p.spelt;
    };
    if (beaten(p.head, p.tail) || (p.head && beaten(false, p.tail)) ||
        (p.tail && beaten(p.head, false)) || (p.head && p.tail && beaten(false, false)))
      return false;
    const auto [held, made] = seen.try_emplace(place_of(p, p.head, p.tail), 0);
    if (made || (p.valid >= (held->second & 0xFFu) && p.spelt >= held->second >> 8))
      held->second = std::uint64_t{p.spelt} << 8 | p.valid;
    return true;
  };
  // One fault count after another: the walk of the text as written, then the
  // points its faults make and the faults on the first byte.
  now.push_back({at, at, 1, 0, 0, 0, false, false});
  for (bool headed = false;;) {
    while (!now.empty()) {
      throwIfCancelled(m.stop);
      Point w = now.back();
      now.pop_back();
      if (!first(w)) continue;
      for (;;) {
        if (m.src + w.pos >= m.mark) m.watch(m.src + w.pos);
        const bool word = !unread && w.pos <= plain;
        // A keyword the text spells as written keeps its spelling from every
        // reading through it.
        if (w.faults == 0) {
          if (const auto kept = mode.ending(w.state, w.pos, work).reserved; kept != kNoToken) {
            want[kept / 64] &= ~(std::uint64_t{1} << (kept % 64));
            if (empty()) goto done;
          }
        }
        // Past its first fault, a reading is the word in hand misspelt: it
        // reads no further than `plain`.
        if (w.faults > 1 && !word) break;
        if (w.faults != 0 && !w.tail && w.valid >= kFaultValid * w.faults &&
            (!w.head || w.pos == plain || unread)) {
          const TextOffset span = w.pos - at;
          if (const std::uint32_t cost = typo_cost(w.faults, span > w.spelt ? span : w.spelt);
              cost < kReplaceCost)
            offer(w, cost);
        }
        // A fault on the first byte offers only at `plain`.
        if (w.head && !unread && w.pos >= plain) break;
        if (w.pos >= end) {
          if (end < len) work.cut = true;
          break;
        }
        if (work.cut) goto done;
        const std::uint16_t to = step(w.state, w.pos);
        if (to != 0) {
          w.state = to;
          ++w.pos;
          if (w.valid < 255) ++w.valid;
          ++w.spelt;
          w.tail = false;
          continue;
        }
        // A fault is anchored on bytes the terminal admits: with none behind
        // it, the walk would read a token out of nothing.
        if (w.faults >= kFaults || w.valid == 0 || (w.faults != 0 && !word)) break;
        const auto faults = static_cast<std::uint8_t>(w.faults + 1);
        // One more is made only where the word is long enough for a reading
        // of that many to be offered.
        if (const TextOffset rest = plain - w.pos;
            faults > 1 && (w.valid + rest < kFaultValid * faults ||
                           typo_cost(faults, std::max(plain - at, w.spelt + rest) + kFaults) >=
                               kReplaceCost))
          break;
        const TextOffset spelt = w.spelt + 1;
        // The bytes the state refuses, skipped where they stand, up to the
        // end of the letter they cut into.
        TextOffset k = 1;
        while (w.pos + k < end && k < kFaultRun && step(w.state, w.pos + k) == 0) ++k;
        while (w.pos + k < end && detail::scalar_start(text, w.pos + k) != w.pos + k) ++k;
        later.push_back({w.pos + k, w.pos + k + 1, w.state, faults, w.valid, w.spelt, true, w.head});
        if (const std::uint16_t two = swapped(w.state, w.pos); two != 0)
          later.push_back({w.pos + 2, w.pos + 2, two, faults, w.valid, spelt + 1, false, w.head});
        // A byte never written, and one written for another: the edges the
        // text reads on from, then the others.
        const TextOffset nextLetter = past_letter(w.pos);
        const auto fault = [&](std::uint16_t edge) {
          later.push_back({w.pos, w.tail ? w.clean : w.pos, edge, faults, w.valid, spelt, w.tail, w.head});
          later.push_back(
              {nextLetter, nextLetter, edge, faults, w.valid, spelt, false, w.head});
        };
        std::array<std::uint16_t, kFaultEdges> others{};
        std::size_t tried = 0;
        std::size_t kept = 0;
        for (const auto edge : successors(w.state)) {
          if (tried == kFaultEdges) break;
          if (step(edge, w.pos) != 0 || (nextLetter < end && step(edge, nextLetter) != 0)) {
            fault(edge);
            ++tried;
          } else if (kept != kFaultEdges) {
            others[kept++] = edge;
          }
        }
        for (std::size_t i = 0; i != kept && tried != kFaultEdges; ++i, ++tried) fault(others[i]);
        break;
      }
    }
    if (!headed) {
      headed = true;
      const auto nextLetter = at < end ? past_letter(at) : at;
      for (const auto edge : successors(1) | std::views::reverse) {
        now.push_back({at, at, edge, 1, 0, 1, false, true});
        if (at < end) now.push_back({nextLetter, nextLetter, edge, 1, 0, 1, false, true});
      }
      // A byte written before the word, and its first two bytes written in
      // each other's place.
      if (at < end) now.push_back({nextLetter, nextLetter, 1, 1, 0, 0, false, true});
      if (const std::uint16_t two = swapped(1, at); two != 0)
        now.push_back({at + 2, at + 2, two, 1, 0, 2, false, true});
      now.insert(now.end(), later.begin(), later.end());
      later.clear();
      continue;
    }
    if (later.empty()) break;
    std::swap(now, later);
  }
done:
  // The cheapest first, then the longest, then the first found.
  std::ranges::sort(found, [](const Found& a, const Found& b) {
    if (a.typo.cost != b.typo.cost) return a.typo.cost < b.typo.cost;
    return a.typo.end != b.typo.end ? a.typo.end > b.typo.end : a.when < b.when;
  });
  std::vector<Typo> ranked;
  ranked.reserve(found.size());
  for (const Found& f : found) ranked.push_back(f.typo);
  return ranked;
}

}  // namespace cursiv::engine
