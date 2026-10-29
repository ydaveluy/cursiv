#include "cursiv/core/workspace/LocalSymbols.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

namespace cursiv {

void LocalSymbols::clear() noexcept {
  _entries.clear();
  _containerOf.clear();
  _nextSameName.clear();
  _groups.clear();
  _names.clear();
  _groupMask = 0;
  _nameMask = 0;
  _held.reset();
}
namespace {

// The smallest power of two that leaves the table at most half full, so a
// linear probe stays short. Never zero: an empty table still has to answer.
[[nodiscard]] std::size_t table_size(std::size_t entries) noexcept {
  std::size_t size = 1;
  while (size < entries * 2) size <<= 1u;
  return size;
}

}  // namespace

void LocalSymbols::reserve(std::size_t entries) {
  _entries.reserve(entries);
  _containerOf.reserve(entries);
}

void LocalSymbols::add(const AstNode* container,
                       AstNodeDescription description) {
  _entries.push_back(std::move(description));
  _containerOf.push_back(container);
}

void LocalSymbols::seal() {
  const std::size_t count = _entries.size();
  if (count == 0) return;

  // 1. The containers, and how many entries each holds.
  //
  // Sized by the entries instead, a document with one container would carry
  // a group table the width of the whole document, and every ancestor level
  // of every reference would miss the cache reaching into it.
  //
  // The containers are met in a probe table as wide as the entries, and filed
  // below in its slot order. It keeps a bit per slot, so an empty one costs a
  // bit to clear and to skip, and a run of entries under one container asks
  // it once.
  struct Met {
    const AstNode* container;
    std::uint32_t count;
    std::uint32_t slot;
    std::uint32_t next;
  };
  const std::size_t wideSize = table_size(count);
  const std::size_t wideMask = wideSize - 1;
  std::vector<std::uint64_t> taken((wideSize + 63) / 64);
  const auto isTaken = [&](std::size_t at) {
    return ((taken[at >> 6u] >> (at & 63u)) & 1u) != 0;
  };
  const auto metAt = std::make_unique_for_overwrite<std::uint32_t[]>(wideSize);
  const auto met = std::make_unique_for_overwrite<Met[]>(count);
  const auto metOf = std::make_unique_for_overwrite<std::uint32_t[]>(count);
  std::uint32_t containers = 0;
  const AstNode* last = nullptr;
  std::uint32_t lastMet = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const AstNode* container = _containerOf[i];
    if (container != last) {
      std::size_t at = static_cast<std::size_t>(mix(container)) & wideMask;
      while (isTaken(at) && met[metAt[at]].container != container)
        at = (at + 1) & wideMask;
      if (!isTaken(at)) {
        taken[at >> 6u] |= std::uint64_t{1} << (at & 63u);
        metAt[at] = containers;
        met[containers++] = Met{container, 0, 0, 0};
      }
      last = container;
      lastMet = metAt[at];
    }
    ++met[lastMet].count;
    metOf[i] = lastMet;
  }
  _groups.assign(table_size(containers), Group{});
  _groupMask = _groups.size() - 1;
  // Sixty-four bits per container at least, so a false "maybe" is rare.
  const std::size_t words = _groups.size() / 2;
  _held = std::make_unique<std::uint64_t[]>(1 + words);
  _held[0] = words * 64 - 1;
  for (std::size_t word = 0; word < taken.size(); ++word)
    for (std::uint64_t bits = taken[word]; bits != 0; bits &= bits - 1) {
      Met& m = met[metAt[word * 64 + std::countr_zero(bits)]];
      std::size_t at = static_cast<std::size_t>(mix(m.container)) & _groupMask;
      while (_groups[at].container != nullptr) at = (at + 1) & _groupMask;
      _groups[at] = Group{m.container, 0, m.count};
      m.slot = static_cast<std::uint32_t>(at);
      const std::size_t bit = held_bit(m.container, _held[0]);
      _held[1 + (bit >> 6u)] |= std::uint64_t{1} << (bit & 63u);
    }

  // 2. Where each group starts.
  //
  // Order of the table, not of the document: a group's position among the
  // others is nobody's business. What must survive is the order inside a group,
  // which the scatter below preserves by walking in declaration order.
  std::uint32_t next = 0;
  for (Group& g : _groups) {
    if (g.container == nullptr) continue;
    g.begin = next;
    next += g.count;
  }

  // 3. The counting sort.
  for (std::uint32_t k = 0; k < containers; ++k)
    met[k].next = _groups[met[k].slot].begin;
  std::vector<AstNodeDescription> grouped(count);
  for (std::size_t i = 0; i < count; ++i)
    grouped[met[metOf[i]].next++] = _entries[i];
  _entries = std::move(grouped);

  // 4. The name table.
  //
  // After the last move and not before: the keys are views of the entries' own
  // names, so indexing an array still to be permuted would leave every key
  // pointing at a description that has since moved away.
  //
  // Read group by group: a group knows its container and its run of entries.
  // The groups are walked from the last and each run backwards, which meets
  // the entries from the last to the first.
  _names.assign(table_size(count), NameSlot{});
  _nameMask = _names.size() - 1;
  _nextSameName.assign(count, kNone);
  for (std::size_t g = _groups.size(); g-- > 0;) {
    const AstNode* container = _groups[g].container;
    if (container == nullptr) continue;
    const std::uint64_t containerHash = mix(container);
    const std::uint32_t begin = _groups[g].begin;
    for (std::uint32_t i = begin + _groups[g].count; i-- > begin;) {
      const std::string_view name = _entries[i].name;
      const std::uint64_t hash = containerHash ^ hash_name(name);
      const auto tag = static_cast<std::uint32_t>(hash >> 32u);
      std::size_t at = static_cast<std::size_t>(hash) & _nameMask;
      for (;;) {
        NameSlot& slot = _names[at];
        if (slot.container == nullptr) {
          slot = NameSlot{container, i, tag};
          break;
        }
        if (slot.container == container && slot.tag == tag &&
            folded_equal(_entries[slot.first].name, name)) {
          // Walking backwards lets us prepend in constant time while keeping
          // declaration order, even for thousands of overloads of one name.
          _nextSameName[i] = slot.first;
          slot.first = i;
          break;
        }
        at = (at + 1) & _nameMask;
      }
    }
  }
  // Build scaffolding, and it goes: the containers a lookup needs are in the
  // slots, and the grouping they served is in `_groups`.
  _containerOf.clear();
  _containerOf.shrink_to_fit();
}

LocalSymbols::NameMatches LocalSymbols::find_held(
    const AstNode* container, std::string_view name,
    std::uint64_t nameHash) const noexcept {
  assert((_entries.empty() || !_names.empty()) &&
         "LocalSymbols::seal() has to run before the first lookup");
  const std::uint64_t hash = mix(container) ^ nameHash;
  const auto tag = static_cast<std::uint32_t>(hash >> 32u);
  std::size_t at = static_cast<std::size_t>(hash) & _nameMask;
  for (;;) {
    const NameSlot& slot = _names[at];
    if (slot.container == nullptr) return {};
    if (slot.container == container && slot.tag == tag) {
      // Exact first: it implies the folded match, and it is what nearly every
      // query spells.
      const std::string_view held = _entries[slot.first].name;
      if (held == name || folded_equal(held, name))
        return NameMatches{this, slot.first};
    }
    at = (at + 1) & _nameMask;
  }
}

const LocalSymbols::Group* LocalSymbols::group(
    const AstNode* container) const noexcept {
  assert((_entries.empty() || !_groups.empty()) &&
         "LocalSymbols::seal() has to run before the first lookup");
  if (_groups.empty()) return nullptr;
  std::size_t at = static_cast<std::size_t>(mix(container)) & _groupMask;
  for (;;) {
    const Group& g = _groups[at];
    if (g.container == nullptr) return nullptr;
    if (g.container == container) return &g;
    at = (at + 1) & _groupMask;
  }
}

std::span<const AstNodeDescription> LocalSymbols::forContainer(
    const AstNode* container) const noexcept {
  const Group* g = may_hold(container) ? group(container) : nullptr;
  return g == nullptr ? std::span<const AstNodeDescription>{}
                      : std::span<const AstNodeDescription>(
                            _entries.data() + g->begin, g->count);
}

}  // namespace cursiv
