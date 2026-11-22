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
#include "cursiv/utils/macros.hpp"
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
    bool forEach(Fn&& fn) const {
      if (_at == kNone) return true;
      if (!fn(_symbols->_entries[_at])) return false;
      if (_symbols->_nextSameName.empty()) return true;
      for (auto i = _symbols->_nextSameName[_at]; i != kNone;
           i = _symbols->_nextSameName[i])
        if (!fn(_symbols->_entries[i])) return false;
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

  // Building, in declaration order: a scope computation adds, the builder
  // indexes what it returns. After an `add` or `buildIndex` that throws, only
  // `clear` and destruction are allowed. `reserve` is a hint.
  void reserve(std::size_t entries);
  // `container` is never null: an entry is visible in the scope of a node.
  void add(const AstNode* container, AstNodeDescription description);
  // Precondition for every query below. Groups, indexes, and freezes; a
  // second call does nothing.
  void buildIndex();

  // Reading, after `buildIndex`. Each scope in a walk uses the same hash.
  [[nodiscard]] CURSIV_ALWAYS_INLINE static std::uint64_t hashName(
      std::string_view name) noexcept {
    return foldedHash(name);
  }
  // Every entry at `container` whose name folds like `name`, in declaration
  // order: an exact reader filters them, a folded one takes them all.
  [[nodiscard]] NameMatches find(const AstNode* container,
                                 std::string_view name,
                                 std::uint64_t nameHash) const noexcept;
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
    if (!mayHold(container)) return nullptr;
    const auto [at, match] =
        nameSlot(container, mix(container) ^ query.nameHash, query.name);
    if (match == detail::NameMatch::None) return nullptr;
    const auto first = _names[at].first;
    if ((!query.exact || match == detail::NameMatch::Exact) &&
        take(_entries[first]))
      return &_entries[first];
    if (_nextSameName.empty()) return nullptr;
    for (auto i = _nextSameName[first]; i != kNone; i = _nextSameName[i]) {
      const auto& entry = _entries[i];
      if ((!query.exact || entry.name == query.name) && take(entry))
        return &entry;
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

  struct NameSlot {
    const AstNode* container = nullptr;
    std::uint32_t first = kNone;
    std::uint32_t tag = 0;
  };

  // The matching slot and its spelling, or the first empty slot on a miss.
  [[nodiscard]] std::pair<std::size_t, detail::NameMatch> nameSlot(
      const AstNode* container, std::uint64_t hash,
      std::string_view name) const noexcept {
    const auto tag = static_cast<std::uint32_t>(hash >> 32u);
    for (auto at = static_cast<std::size_t>(hash) & _nameMask;;
         at = (at + 1) & _nameMask) {
      const auto& slot = _names[at];
      if (slot.container == nullptr) return {at, detail::NameMatch::None};
      if (slot.container == container && slot.tag == tag) {
        const auto match = detail::name_match(_entries[slot.first].name, name);
        if (match != detail::NameMatch::None) return {at, match};
      }
    }
  }

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

  [[nodiscard]] const Group* group(const AstNode* container) const noexcept {
    if (!mayHold(container)) return nullptr;
    const auto mask = _groups.size() - 1;
    auto at = static_cast<std::size_t>(mix(container)) & mask;
    while (_groups[at].container != nullptr) {
      if (_groups[at].container == container) return &_groups[at];
      at = (at + 1) & mask;
    }
    return nullptr;
  }
  // The nearest holder at or above `node`, when `node` has a shortcut and the
  // walk ends at `document`. The first call builds the table.
  [[nodiscard]] const Shortcut* shortcut(
      const AstNode* node, const AstNode* document) const noexcept;

  // The high product bits spread aligned addresses across power-of-two tables.
  [[nodiscard]] static std::uint64_t mix(const void* p) noexcept {
    const std::uint64_t address = reinterpret_cast<std::uintptr_t>(p);
    return (address * 0x9e3779b97f4a7c15ull) >> 32u;
  }

  // Inline, and one bit: a walk passes several containers holding nothing for
  // each one that holds something, and this is all those levels pay. A clear
  // bit is a definite no; a set one is confirmed by the tables.
  [[nodiscard]] bool mayHold(const AstNode* container) const noexcept {
    assert((_entries.empty() || !_names.empty()) &&
           "LocalSymbols::buildIndex() has to run before the first lookup");
    const std::uint64_t* const held = _held.get();
    if (held == nullptr) return false;
    const std::size_t bit = heldBit(container, held[0]);
    return ((held[1 + (bit >> 6u)] >> (bit & 63u)) & 1u) != 0;
  }
  [[nodiscard]] static std::size_t heldBit(const AstNode* container,
                                           std::uint64_t mask) noexcept {
    return static_cast<std::size_t>(
        (reinterpret_cast<std::uintptr_t>(container) >> 4u) & mask);
  }

  // Grouped by container after `buildIndex`, each group in declaration order.
  std::vector<AstNodeDescription> _entries;
  // Declaration runs during collection, then a lookup table by container.
  std::vector<Group> _groups;
  // Each slot links one scope's homonyms in declaration order.
  std::vector<NameSlot> _names;
  std::size_t _nameMask = 0;
  std::vector<std::uint32_t> _nextSameName;
  // The containers, one bit each by address, behind the mask that indexes
  // them: what `mayHold` reads. Null is an empty filter, which a moved-from
  // table is too.
  std::unique_ptr<std::uint64_t[]> _held;
  // Null before `buildIndex`, and for a table with no entry.
  std::unique_ptr<Shortcuts> _shortcuts;
};

template <class Visit>
bool LocalSymbols::walkScopes(const AstNode* from, const AstNode* document,
                              Visit&& visit) const {
  std::size_t depth = 0;
  for (const AstNode* at = from; at != nullptr && at != document;
       at = at->parent, ++depth) {
    if (depth >= kStride) [[unlikely]] {
      if (_held == nullptr) return false;
      if (const Shortcut* found = shortcut(at, document)) {
        at = found->holder;
        if (at == nullptr) return false;
      }
    }
    if (visit(at)) return true;
  }
  return false;
}

}  // namespace cursiv
