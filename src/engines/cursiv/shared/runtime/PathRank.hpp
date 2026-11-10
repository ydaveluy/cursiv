// The order two paths of a repair search rank in: the one that takes the lower
// choice where they part comes first.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv::engine {

inline constexpr std::uint32_t kNoEditLink = 0xFFFFFFFFu;

class PathRank {
 public:
  // Where two paths part: the link they share, and on each side the link just
  // below it -- `kNoEditLink` for a path that ends there. Paths from two
  // roots share none, and their roots stand for where they part.
  struct Parting {
    std::uint32_t shared = kNoEditLink;
    std::uint32_t a = kNoEditLink;
    std::uint32_t b = kNoEditLink;
  };

  // Where paths `a` and `b` part, walked up from their ends: the cost is the
  // distance to the link they share, never the depth.
  template <class Edit>
  Parting part(const std::vector<Edit>& links, std::uint32_t a,
               std::uint32_t b) {
    if (a == b) return {a, kNoEditLink, kNoEditLink};
    if (_marks.size() < links.size()) _marks.resize(links.size());
    if (++_round == 0x80000000u) {
      for (Mark& m : _marks) m.seen = 0;
      _round = 1;
    }
    const std::uint32_t base = _round << 1;
    std::uint32_t at[2] = {a, b};
    std::uint32_t top[2] = {a, b};
    _marks[a] = {base, kNoEditLink};
    _marks[b] = {base | 1u, kNoEditLink};
    while (at[0] != kNoEditLink || at[1] != kNoEditLink)
      for (std::uint32_t side = 0; side < 2; ++side) {
        if (at[side] == kNoEditLink) continue;
        const std::uint32_t below = at[side];
        const std::uint32_t up = links[below].parent;
        at[side] = up;
        if (up == kNoEditLink) continue;
        if (_marks[up].seen == (base | (side ^ 1u))) {
          const std::uint32_t theirs = _marks[up].via;
          return side == 0 ? Parting{up, below, theirs}
                           : Parting{up, theirs, below};
        }
        _marks[up] = {base | side, below};
        top[side] = up;
      }
    return {kNoEditLink, top[0], top[1]};
  }

  // Whether path `a` ranks before path `b`: the lower choice where they part,
  // and a path that ends there first.
  template <class Edit>
  bool before(const std::vector<Edit>& links, std::uint32_t a,
              std::uint32_t b) {
    return a != b && before(links, part(links, a, b));
  }
  template <class Edit>
  static bool before(const std::vector<Edit>& links, const Parting& p) {
    if (p.a == kNoEditLink || p.b == kNoEditLink) return p.a == kNoEditLink;
    return links[p.a].choice != links[p.b].choice
               ? links[p.a].choice < links[p.b].choice
               : p.a < p.b;
  }

 private:
  // The ranking that last marked a link, and the link its walk came up from.
  struct Mark {
    std::uint32_t seen = 0;
    std::uint32_t via = kNoEditLink;
  };
  std::vector<Mark> _marks;
  std::uint32_t _round = 0;
};

}  // namespace cursiv::engine
