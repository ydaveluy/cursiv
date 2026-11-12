#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

// The local scopes of one document: what is visible from inside the tree,
// indexed by the container it is visible under.

namespace cursiv {

// Flat arrays for the whole document rather than a map per container: fewer
// allocations, and a working set that stays in cache.
class LocalSymbols {
 public:
  // A name and its case policy, shared by every level of a scope walk.
  struct NameQuery {
    NameQuery(const std::string_view& text, std::uint64_t hash,
              bool policy) noexcept
        : name(text.data(), text.size()), nameHash(hash), exact(policy) {}

    std::string_view name;
    std::uint64_t nameHash;
    bool exact;
  };

  // One name's entries at one container, in declaration order. Homonyms of
  // different types are all kept: the type filter runs at lookup.
  class NameMatches {
   public:
    NameMatches() = default;
    NameMatches(const LocalSymbols* symbols, std::uint32_t first) noexcept
        : _symbols(symbols), _at(first) {}

    // Calls `fn` on each entry until it answers false. Answers what the last
    // call answered, or true over an empty sequence.
    template <class Fn>
    bool for_each(Fn&& fn) const {
      for (std::uint32_t at = _at; at != kNone;
           at = _symbols->_nextSameName[at])
        if (!fn(_symbols->_entries[at])) return false;
      return true;
    }

   private:
    const LocalSymbols* _symbols = nullptr;
    std::uint32_t _at = kNone;
  };

  LocalSymbols() = default;
  LocalSymbols(const LocalSymbols&) = delete;
  LocalSymbols& operator=(const LocalSymbols&) = delete;
  LocalSymbols(LocalSymbols&&) noexcept = default;
  LocalSymbols& operator=(LocalSymbols&&) noexcept = default;

  // Building, in declaration order. After an `add` or `build_index` that
  // throws, only `clear` and destruction are allowed. `reserve` is a hint.
  void reserve(std::size_t entries);
  // `container` is never null: an entry is visible in the scope of a node.
  void add(const AstNode* container, AstNodeDescription description);
  // Precondition for every query below. Groups, indexes, and freezes.
  void build_index();

  // Reading, after `build_index`. The name is hashed once per walk, on 64 bits
  // on every target, like `mix`.
  [[nodiscard]] static std::uint64_t hash_name(std::string_view name) noexcept {
    return folded_hash(name);
  }
  // Every entry at `container` whose name folds like `name`, in declaration
  // order: an exact reader filters them, a folded one takes them all.
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
    const NameQuery query{name, nameHash, exact};
    return firstNamed(container, query, std::forward<Take>(take));
  }

  template <class Take>
  [[nodiscard]] const AstNodeDescription* firstNamed(
      const AstNode* container, const NameQuery& query, Take&& take) const {
    if (!may_hold(container)) return nullptr;
    const std::string_view& name = query.name;
    const std::uint64_t hash = mix(container) ^ query.nameHash;
    const auto tag = static_cast<std::uint32_t>(hash >> 32u);
    std::uint32_t first = kNone;
    bool spelled = false;
    for (auto at = static_cast<std::size_t>(hash) & _nameMask;;
         at = (at + 1) & _nameMask) {
      const NameSlot& slot = _names[at];
      if (slot.container == nullptr) return nullptr;
      if (slot.container != container || slot.tag != tag) continue;
      const auto match = detail::name_match(_entries[slot.first].name, name);
      if (match != detail::NameMatch::None) {
        spelled = match == detail::NameMatch::Exact;
        first = slot.first;
        break;
      }
    }
    if ((spelled || !query.exact) && take(_entries[first]))
      return &_entries[first];
    for (auto at = _nextSameName[first]; at != kNone; at = _nextSameName[at]) {
      const AstNodeDescription& entry = _entries[at];
      if (query.exact && entry.name != name) continue;
      if (take(entry)) return &entry;
    }
    return nullptr;
  }
  // Everything bound to `container`, in the order it was written. Empty for
  // most nodes of most documents, so the walk up the ancestors skips rather
  // than allocates.
  [[nodiscard]] std::span<const AstNodeDescription> forContainer(
      const AstNode* container) const noexcept;

  // Calls `visit` on `from` and its ancestors below `document` until it
  // returns true, and returns whether it did. Past the first levels, it skips
  // the ancestors that hold no entry.
  template <class Visit>
  bool walkScopes(const AstNode* from, const AstNode* document,
                  Visit&& visit) const;

  void clear() noexcept;

  [[nodiscard]] bool empty() const noexcept { return _entries.empty(); }
  // The number of descriptions, not of containers.
  [[nodiscard]] std::size_t size() const noexcept { return _entries.size(); }

 private:
  static constexpr std::uint32_t kNone = ~std::uint32_t{0};
  // The levels a walk goes up one by one, and the depth spacing of shortcuts.
  static constexpr std::uint32_t kStride = 32;

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

  // A node at a depth that is a multiple of `kStride`, with at least `kStride`
  // levels below it, and the nearest node at or above it that holds entries.
  struct Shortcut {
    const AstNode* node = nullptr;
    const AstNode* holder = nullptr;
  };
  // Built from the tree by the first walk that passes `kStride` levels, so
  // a shallow tree pays nothing. Read-only once `state` is `kRead`.
  struct Shortcuts {
    // A full word, which a wait can block on on every target.
    enum State : std::uint32_t { kUnread, kReading, kRead };

    std::atomic<std::uint32_t> state = kUnread;
    // The document whose tree was read: the only walk end the table serves.
    const AstNode* top = nullptr;
    // Open addressing by node, power of two size, linear probing.
    std::vector<Shortcut> table;
    std::size_t mask = 0;
  };

  [[nodiscard]] const Group* group(const AstNode* container) const noexcept;
  [[nodiscard]] NameMatches find_held(const AstNode* container,
                                      std::string_view name,
                                      std::uint64_t nameHash) const noexcept;
  // The nearest holder at or above `node`, when `node` has a shortcut and the
  // walk ends at `document`. The first call builds the table.
  [[nodiscard]] const Shortcut* shortcut(
      const AstNode* node, const AstNode* document) const noexcept;

  // 64 bits on every target, as the tag is the high half. The pointer's low
  // bits are alignment, so they are shifted away before spreading.
  [[nodiscard]] static std::uint64_t mix(const void* p) noexcept {
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(p) >> 4u;
    h *= 0x9e3779b97f4a7c15ull;
    return h ^ (h >> 29u);
  }

  // Inline, and one bit: a walk passes several containers holding nothing for
  // each one that holds something, and this is all those levels pay. A clear
  // bit is a definite no; a set one is confirmed by the tables.
  [[nodiscard]] bool may_hold(const AstNode* container) const noexcept {
    assert((_entries.empty() || !_names.empty()) &&
           "LocalSymbols::build_index() has to run before the first lookup");
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

  // Grouped by container after `build_index`, each group in declaration order.
  std::vector<AstNodeDescription> _entries;
  // The container of `_entries[i]`, while building only: `build_index` groups
  // the entries by it and drops it, since each group then names its container.
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
  // Null before `build_index`, and for a table with no entry.
  std::unique_ptr<Shortcuts> _shortcuts;
};

template <class Visit>
bool LocalSymbols::walkScopes(const AstNode* from, const AstNode* document,
                              Visit&& visit) const {
  const AstNode* at = from;
  for (std::uint32_t left = kStride; left != 0; --left) {
    if (at == nullptr || at == document) return false;
    if (visit(at)) return true;
    at = at->parent;
  }
  // Nothing holds an entry anywhere.
  if (_held == nullptr) return false;
  while (at != nullptr && at != document) {
    if (const Shortcut* found = shortcut(at, document)) {
      at = found->holder;
      if (at == nullptr) return false;
    }
    if (visit(at)) return true;
    at = at->parent;
  }
  return false;
}

}  // namespace cursiv
