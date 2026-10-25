#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

// The local scopes of one document: what is visible from inside the tree,
// indexed by the container it is visible under.

namespace cursiv {

// Flat arrays for the whole document, rather than a per-container map and
// deque: those bill several allocations before holding anything, plus a
// hash node per distinct name, taking the working set out of cache before
// the walk finishes.
class LocalSymbols {
 public:
  // One name's entries at one container, in declaration order, read as a
  // sequence. Two same-named declarations of different types are both kept:
  // the type filter runs at lookup, so hiding the second here would make an
  // assignable declaration invisible behind an unassignable homonym written
  // before it.
  class NameMatches {
   public:
    NameMatches() = default;
    NameMatches(const LocalSymbols* home, std::uint32_t first) noexcept
        : _home(home), _at(first) {}

    // Calls `fn` on each entry until it answers false. Answers what the last
    // call answered, or true over an empty sequence.
    template <class Fn>
    bool for_each(Fn&& fn) const {
      for (std::uint32_t at = _at; at != kNone; at = _home->_nextSameName[at])
        if (!fn(_home->_entries[at])) return false;
      return true;
    }

   private:
    const LocalSymbols* _home = nullptr;
    std::uint32_t _at = kNone;
  };

  LocalSymbols() = default;
  LocalSymbols(const LocalSymbols&) = delete;
  LocalSymbols& operator=(const LocalSymbols&) = delete;
  LocalSymbols(LocalSymbols&&) noexcept = default;
  LocalSymbols& operator=(LocalSymbols&&) noexcept = default;

  // Building, in declaration order.
  //
  // A hint and not a capacity: over-reserving for a document whose nodes are
  // mostly nameless is the one waste this shape can still commit.
  void reserve(std::size_t entries);
  void add(const AstNode* container, AstNodeDescription description);
  // Precondition for every query below. Groups, indexes, and freezes.
  void seal();

  // Reading, after `seal`.
  //
  // Hashed once by the caller and handed to every level of one walk.
  // 64 bits like `mix`, and for the same reason: the two are xored and the
  // tag is the high half, which `std::size_t` cannot hold on a 32-bit target.
  [[nodiscard]] static std::uint64_t hash_name(std::string_view name) noexcept {
    return folded_hash(name);
  }
  // The chain of `name`'s folded class at `container`, every spelling in
  // declaration order: an exact reader filters, a folded one takes it whole.
  [[nodiscard]] NameMatches find(const AstNode* container,
                                 std::string_view name,
                                 std::uint64_t nameHash) const noexcept {
    return may_hold(container) ? find_held(container, name, nameHash)
                               : NameMatches{};
  }
  // The first entry at `container` named `name` that `take` accepts, or null.
  template <class Take>
  [[nodiscard]] const AstNodeDescription* firstNamed(
      const AstNode* container, std::string_view name, std::uint64_t nameHash,
      bool exact, Take&& take) const {
    if (!may_hold(container)) return nullptr;
    const std::uint64_t hash = mix(container) ^ nameHash;
    const auto tag = static_cast<std::uint32_t>(hash >> 32u);
    std::uint32_t first = kNone;
    bool spelled = false;
    for (auto at = static_cast<std::size_t>(hash) & _nameMask;;
         at = (at + 1) & _nameMask) {
      const NameSlot& slot = _names[at];
      if (slot.container == nullptr) return nullptr;
      if (slot.container != container || slot.tag != tag) continue;
      const std::string_view held = _entries[slot.first].name;
      spelled = held == name;
      if (spelled || folded_equal(held, name)) {
        first = slot.first;
        break;
      }
    }
    if ((spelled || !exact) && take(_entries[first])) return &_entries[first];
    for (auto at = _nextSameName[first]; at != kNone; at = _nextSameName[at]) {
      const AstNodeDescription& entry = _entries[at];
      if (exact && entry.name != name) continue;
      if (take(entry)) return &entry;
    }
    return nullptr;
  }
  // Everything bound to `container`, in the order it was written. Empty for
  // most nodes of most documents, so the walk up the ancestors skips rather
  // than allocates.
  [[nodiscard]] std::span<const AstNodeDescription> forContainer(
      const AstNode* container) const noexcept;

  // A rebuild drops what the previous one derived, this among it.
  void clear() noexcept;

  [[nodiscard]] bool empty() const noexcept { return _entries.empty(); }
  // The number of descriptions, not of containers.
  [[nodiscard]] std::size_t size() const noexcept { return _entries.size(); }

 private:
  static constexpr std::uint32_t kNone = ~std::uint32_t{0};

  // One container's group: where it starts and how long it is.
  struct Group {
    const AstNode* container = nullptr;
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
  };

  // The key is in the slot, and the tag with it, so a probe that misses reads
  // this cache line and no other: the entry array is touched only on a hit.
  struct NameSlot {
    const AstNode* container = nullptr;
    std::uint32_t first = kNone;
    std::uint32_t tag = 0;
  };

  [[nodiscard]] const Group* group(const AstNode* container) const noexcept;
  [[nodiscard]] NameMatches find_held(const AstNode* container,
                                      std::string_view name,
                                      std::uint64_t nameHash) const noexcept;

  // Sixty-four bits by declaration, not by the width `std::size_t` happens to
  // have: the tag is the hash's high half, so on a 32-bit target `hash >> 32`
  // would be undefined. The pointer's low bits are the allocator's alignment,
  // the same for every node: shifted away, then spread.
  [[nodiscard]] static std::uint64_t mix(const void* p) noexcept {
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(p) >> 4u;
    h *= 0x9e3779b97f4a7c15ull;
    return h ^ (h >> 29u);
  }

  // Inline, and one bit: a climb meets several containers holding nothing for
  // each one that holds something, and this is all those levels pay. A clear
  // bit is a definite no; a set one is confirmed by the tables.
  [[nodiscard]] bool may_hold(const AstNode* container) const noexcept {
    assert((_entries.empty() || !_names.empty()) &&
           "LocalSymbols::seal() has to run before the first lookup");
    const std::uint64_t* const held = _held.get();
    if (held == nullptr) return false;
    const std::size_t bit = held_bit(container, held[0]);
    return ((held[1 + (bit >> 6u)] >> (bit & 63u)) & 1u) != 0;
  }
  [[nodiscard]] static std::size_t held_bit(const AstNode* container,
                                            std::uint64_t mask) noexcept {
    return static_cast<std::size_t>(
        (reinterpret_cast<std::uintptr_t>(container) >> 4u) & mask);
  }

  // Grouped by container after `seal`, each group in declaration order.
  std::vector<AstNodeDescription> _entries;
  // The container of `_entries[i]`, while building only: `seal` permutes it
  // with the entries, copies what the name table needs into the slots, and
  // drops it.
  std::vector<const AstNode*> _containerOf;
  // The next entry of the same container carrying the same name, or `kNone`.
  std::vector<std::uint32_t> _nextSameName;
  // Open-addressed, power of two, linear probing.
  std::vector<Group> _groups;
  std::vector<NameSlot> _names;
  std::size_t _groupMask = 0;
  std::size_t _nameMask = 0;
  // The containers, one bit each by address, behind the mask that indexes
  // them: what `may_hold` reads. Null is an empty filter, which a moved-from
  // table is too.
  std::unique_ptr<std::uint64_t[]> _held;
};

}  // namespace cursiv
