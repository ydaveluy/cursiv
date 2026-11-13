// What a repair's first insertion could as cheaply have been: where a search
// drops one of two paths of equal key, what it first inserted is kept under
// the other. The terminals are named in the grammar's order.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/shared/runtime/PathRank.hpp"
#include "cursiv/shared/utils/RoundMarks.hpp"

namespace cursiv::engine {

class TiedInserts {
 public:
  // What a link is to the path through it, as an engine's `kind` answers: an
  // arm the drive took is no edit; an edit inserts its terminal, or
  // `kNoElement`.
  static constexpr std::uint32_t kArm = 0x10000u;

  void clear() { _under.clear(); }

  // Path `dropped` joins path `kept`, parted as `p`, at the same key: what the
  // dropped path's own links held, and its first insertion, are kept under
  // `kept`. Returns the number of links walked.
  template <class Edit, class Kind>
  std::uint32_t tie(const std::vector<Edit>& links, std::uint32_t kept,
                    std::uint32_t dropped, const PathRank::Parting& p,
                    bool handed, Kind&& kind) {
    if (p.shared == kNoEditLink) return 0;
    std::uint32_t walked = 0;
    bool sharedEdit = false;
    for (std::uint32_t l = p.shared;
         !sharedEdit && links[l].parent != kNoEditLink;
         l = links[l].parent, ++walked)
      sharedEdit = kind(links[l]) != kArm;
    _moved.clear();
    walked += own(links, dropped, p.shared, !sharedEdit, kind, _moved);
    if (handed) walked += own(links, kept, p.shared, !sharedEdit, kind, _moved);
    if (_moved.empty()) return walked;
    std::vector<ElementId>& at = _under[kept];
    open();
    for (const ElementId term : at) fresh(term);
    for (const ElementId term : _moved)
      if (fresh(term)) at.push_back(term);
    return walked;
  }

  // Adds to `out`, once since `open`, the first insertion of the path ending
  // at `link` and what was kept under its links. Returns the number of links
  // walked.
  template <class Edit, class Kind>
  std::uint32_t name(const std::vector<Edit>& links, std::uint32_t link,
                     Kind&& kind, std::vector<ElementId>& out) {
    std::uint32_t first = kNoElement;
    std::uint32_t walked = 0;
    for (std::uint32_t l = link;
         l != kNoEditLink && links[l].parent != kNoEditLink;
         l = links[l].parent, ++walked) {
      if (const std::uint32_t k = kind(links[l]); k != kArm) first = k;
      if (const auto it = _under.find(l); it != _under.end())
        for (const ElementId term : it->second)
          if (fresh(term)) out.push_back(term);
    }
    if (first != kNoElement && fresh(static_cast<ElementId>(first)))
      out.push_back(static_cast<ElementId>(first));
    return walked;
  }

  // A new set of terminals, each to be named once.
  void open() { _seen.next(); }

  // The terminals named, in the order the grammar declares them.
  static void order(std::vector<ElementId>& terms);

 private:
  // What the links from `from` up to `shared` held, and, where `first`, the
  // insertion of the edit nearest the root among them.
  template <class Edit, class Kind>
  std::uint32_t own(const std::vector<Edit>& links, std::uint32_t from,
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
    return _seen.first(term);
  }

  std::unordered_map<std::uint32_t, std::vector<ElementId>> _under;
  std::vector<ElementId> _moved;
  RoundMarks _seen;
};

}  // namespace cursiv::engine
