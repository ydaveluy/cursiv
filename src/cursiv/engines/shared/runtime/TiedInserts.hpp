// What a repair's first insertion could as cheaply have been. A search keeps
// one path for the readings that meet at one future and one key, and the path
// it drops there was as cheap: what that path first inserted is kept under the
// one that stays, and every path through it names it too. Which readings meet
// depends on an engine's own states, and so does the order it tries them in;
// the terminals named, in the order the grammar declares them, do not.
#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"

namespace cursiv {

class TiedInserts {
 public:
  // What a link is to the path through it, as an engine's `kind` answers: an
  // arm the drive took is no gesture; a gesture inserts its terminal, or
  // `kNoElement`.
  static constexpr std::uint32_t kArm = 0x10000u;

  void clear() { _under.clear(); }

  // Path `dropped` meets path `kept`, parted as `p`, at the same key. Only
  // what the dropped path does not share is walked: what its own links held,
  // and its first insertion where the shared links make no gesture -- else
  // both paths first insert the same. Where `kept` takes the dropped path,
  // what its own links held is kept too, and so is its first insertion on the
  // same terms. Paths from two roots never meet. Answers the links walked.
  template <class Gesture, class Kind>
  std::uint32_t tie(const std::vector<Gesture>& links, std::uint32_t kept,
                    std::uint32_t dropped, const GestureRank::Parting& p,
                    bool handed, Kind&& kind) {
    if (p.shared == kNoGestureLink) return 0;
    std::uint32_t walked = 0;
    bool sharedGesture = false;
    for (std::uint32_t l = p.shared;
         !sharedGesture && links[l].parent != kNoGestureLink;
         l = links[l].parent, ++walked)
      sharedGesture = kind(links[l]) != kArm;
    _moved.clear();
    walked += own(links, dropped, p.shared, !sharedGesture, kind, _moved);
    if (handed)
      walked += own(links, kept, p.shared, !sharedGesture, kind, _moved);
    if (_moved.empty()) return walked;
    std::vector<ElementId>& at = _under[kept];
    open();
    for (const ElementId term : at) fresh(term);
    for (const ElementId term : _moved)
      if (fresh(term)) at.push_back(term);
    return walked;
  }

  // The first insertion of the path ending at `link`, and what was kept
  // under its links, each added to `out` once since `open`: one walk of the
  // path, as the election's own reads of it.
  template <class Gesture, class Kind>
  void name(const std::vector<Gesture>& links, std::uint32_t link, Kind&& kind,
            std::vector<ElementId>& out) {
    std::uint32_t first = kNoElement;
    for (std::uint32_t l = link;
         l != kNoGestureLink && links[l].parent != kNoGestureLink;
         l = links[l].parent) {
      if (const std::uint32_t k = kind(links[l]); k != kArm) first = k;
      if (const auto it = _under.find(l); it != _under.end())
        for (const ElementId term : it->second)
          if (fresh(term)) out.push_back(term);
    }
    if (first != kNoElement && fresh(static_cast<ElementId>(first)))
      out.push_back(static_cast<ElementId>(first));
  }

  // A new set of terminals, each to be named once.
  void open() {
    if (++_stamp == 0) {
      std::ranges::fill(_seen, 0u);
      _stamp = 1;
    }
  }

  // The terminals named, in the order the grammar declares them.
  static void order(std::vector<ElementId>& terms) { std::ranges::sort(terms); }

 private:
  // What the links from `from` up to `shared` held, and, where `first`, the
  // insertion of the gesture nearest the root among them.
  template <class Gesture, class Kind>
  std::uint32_t own(const std::vector<Gesture>& links, std::uint32_t from,
                    std::uint32_t shared, bool first, Kind&& kind,
                    std::vector<ElementId>& out) const {
    std::uint32_t walked = 0;
    std::uint32_t top = kNoElement;
    for (std::uint32_t l = from; l != shared; l = links[l].parent, ++walked) {
      if (const std::uint32_t k = kind(links[l]); k != kArm) top = k;
      if (const auto it = _under.find(l); it != _under.end())
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
    if (first && top != kNoElement) out.push_back(static_cast<ElementId>(top));
    return walked;
  }

  // Whether `term` is new to the set opened last; asking makes it old.
  bool fresh(ElementId term) {
    if (_seen.size() <= term) _seen.resize(term + 1u, 0u);
    if (_seen[term] == _stamp) return false;
    _seen[term] = _stamp;
    return true;
  }

  std::unordered_map<std::uint32_t, std::vector<ElementId>> _under;
  std::vector<ElementId> _moved;
  std::vector<std::uint32_t> _seen;
  std::uint32_t _stamp = 0;
};

}  // namespace cursiv
