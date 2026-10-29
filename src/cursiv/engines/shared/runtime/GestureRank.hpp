// The order two paths of a repair search rank in. A path is a chain of
// gestures, each linked to the one before it and carrying the order of its
// choice among those it was taken from; of two paths, the one that takes the
// lower choice where they part comes first.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

inline constexpr std::uint32_t kNoGestureLink = 0xFFFFFFFFu;

class GestureRank {
 public:
  // Where two paths part: the link they share, and on each side the link just
  // below it -- `kNoGestureLink` for a path that ends there. Paths from two
  // roots share none, and their roots stand for where they part.
  struct Parting {
    std::uint32_t shared = kNoGestureLink;
    std::uint32_t a = kNoGestureLink;
    std::uint32_t b = kNoGestureLink;
  };

  // Paths `a` and `b`, the links ending them, walked up from their ends, each
  // marking what it crosses, until one steps on what the other crossed: the
  // cost is the distance to the link they share, never the depth. A `Gesture`
  // has a `parent` and a `choice`.
  template <class Gesture>
  Parting part(const std::vector<Gesture>& links, std::uint32_t a,
               std::uint32_t b) {
    if (a == b) return {a, kNoGestureLink, kNoGestureLink};
    if (_marks.size() < links.size()) _marks.resize(links.size());
    if (++_stamp == 0x80000000u) {
      for (Mark& m : _marks) m.seen = 0;
      _stamp = 1;
    }
    const std::uint32_t base = _stamp << 1;
    std::uint32_t at[2] = {a, b};
    std::uint32_t top[2] = {a, b};
    _marks[a] = {base, kNoGestureLink};
    _marks[b] = {base | 1u, kNoGestureLink};
    while (at[0] != kNoGestureLink || at[1] != kNoGestureLink)
      for (std::uint32_t side = 0; side < 2; ++side) {
        if (at[side] == kNoGestureLink) continue;
        const std::uint32_t below = at[side];
        const std::uint32_t up = links[below].parent;
        at[side] = up;
        if (up == kNoGestureLink) continue;
        if (_marks[up].seen == (base | (side ^ 1u))) {
          const std::uint32_t theirs = _marks[up].via;
          return side == 0 ? Parting{up, below, theirs}
                           : Parting{up, theirs, below};
        }
        _marks[up] = {base | side, below};
        top[side] = up;
      }
    return {kNoGestureLink, top[0], top[1]};
  }

  // Whether path `a` ranks before path `b`: the lower choice where they part,
  // and a path that ends there first.
  template <class Gesture>
  bool before(const std::vector<Gesture>& links, std::uint32_t a,
              std::uint32_t b) {
    return a != b && before(links, part(links, a, b));
  }
  template <class Gesture>
  static bool before(const std::vector<Gesture>& links, const Parting& p) {
    if (p.a == kNoGestureLink || p.b == kNoGestureLink)
      return p.a == kNoGestureLink;
    return links[p.a].choice != links[p.b].choice
               ? links[p.a].choice < links[p.b].choice
               : p.a < p.b;
  }

 private:
  // The stamp of the ranking that marked a link, and the link its walk came up
  // from.
  struct Mark {
    std::uint32_t seen = 0;
    std::uint32_t via = kNoGestureLink;
  };
  std::vector<Mark> _marks;
  std::uint32_t _stamp = 0;
};

}  // namespace cursiv
