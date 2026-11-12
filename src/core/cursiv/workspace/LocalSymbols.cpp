#include "cursiv/workspace/LocalSymbols.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/AtomicWait.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"

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
  _shortcuts.reset();
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
  // Null is the empty slot of every table `build_index` builds.
  assert(container != nullptr && "a local symbol needs a container");
  _entries.push_back(std::move(description));
  _containerOf.push_back(container);
}

void LocalSymbols::build_index() {
  const std::size_t count = _entries.size();
  if (count == 0) return;
  // Positions, counts and slots are 32 bits, and a table is at least twice its
  // entries: more than 2^31 entries is refused before anything changes.
  if (count > std::size_t{1} << 31u)
    throw std::length_error("cursiv: too many local symbols in one document");

  // The groups' slots in the order their containers were first met: the order
  // the entries lie in and the name table is filled in, whatever the addresses.
  std::vector<std::uint32_t> met;
  // One container already defines the only group: no counting or source
  // permutation is needed, whatever names or declarations it contains.
  const AstNode* const firstContainer = _containerOf.front();
  if (std::all_of(_containerOf.begin() + 1, _containerOf.end(),
                  [firstContainer](const AstNode* at) {
                    return at == firstContainer;
                  })) {
    _groups.assign(table_size(1), Group{});
    _groupMask = _groups.size() - 1;
    met.push_back(static_cast<std::uint32_t>(
        static_cast<std::size_t>(mix(firstContainer)) & _groupMask));
    _groups[met.front()] =
        Group{firstContainer, 0, static_cast<std::uint32_t>(count)};
    const std::size_t words = _groups.size() / 2;
    _held = std::make_unique<std::uint64_t[]>(1 + words);
    _held[0] = words * 64 - 1;
    const std::size_t bit = held_bit(firstContainer, _held[0]);
    _held[1 + (bit >> 6u)] |= std::uint64_t{1} << (bit & 63u);
    if (_entries.capacity() != count)
      std::vector<AstNodeDescription>(_entries).swap(_entries);
    _containerOf.clear();
    _containerOf.shrink_to_fit();
  } else {
    // 1. The containers, and how many entries each holds. They are met in a
    // probe table as wide as the entries, with one bit per slot, then filed in
    // slot order.
    struct Holder {
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
    const auto holderAt =
        std::make_unique_for_overwrite<std::uint32_t[]>(wideSize);
    const auto holders = std::make_unique_for_overwrite<Holder[]>(count);
    const auto holderOf =
        std::make_unique_for_overwrite<std::uint32_t[]>(count);
    std::uint32_t containers = 0;
    const AstNode* last = nullptr;
    std::uint32_t lastHolder = 0;
    bool alreadyGrouped = true;
    for (std::size_t i = 0; i < count; ++i) {
      if (const AstNode* container = _containerOf[i]; container != last) {
        std::size_t at = static_cast<std::size_t>(mix(container)) & wideMask;
        while (isTaken(at) && holders[holderAt[at]].container != container)
          at = (at + 1) & wideMask;
        if (!isTaken(at)) {
          taken[at >> 6u] |= std::uint64_t{1} << (at & 63u);
          holderAt[at] = containers;
          // The first source position is also the group's beginning when
          // no container returns in a later run. The scatter reuses it below.
          holders[containers++] =
              Holder{container, 0, 0, static_cast<std::uint32_t>(i)};
        } else {
          alreadyGrouped = false;
        }
        last = container;
        lastHolder = holderAt[at];
      }
      ++holders[lastHolder].count;
      holderOf[i] = lastHolder;
    }
    _groups.assign(table_size(containers), Group{});
    _groupMask = _groups.size() - 1;
    // Sixty-four bits per container at least, so a false "maybe" is rare.
    const std::size_t words = _groups.size() / 2;
    _held = std::make_unique<std::uint64_t[]>(1 + words);
    _held[0] = words * 64 - 1;
    for (std::size_t word = 0; word < taken.size(); ++word)
      for (std::uint64_t bits = taken[word]; bits != 0; bits &= bits - 1) {
        Holder& m = holders[holderAt[word * 64 + std::countr_zero(bits)]];
        std::size_t at = static_cast<std::size_t>(mix(m.container)) & _groupMask;
        while (_groups[at].container != nullptr) at = (at + 1) & _groupMask;
        _groups[at] = Group{m.container, m.next, m.count};
        m.slot = static_cast<std::uint32_t>(at);
        const std::size_t bit = held_bit(m.container, _held[0]);
        _held[1 + (bit >> 6u)] |= std::uint64_t{1} << (bit & 63u);
      }
    met.reserve(containers);
    for (std::uint32_t k = 0; k < containers; ++k)
      met.push_back(holders[k].slot);

    // A group already in one source run needs no permutation. Group order
    // is private; declaration order inside every group stays exactly as read.
    if (alreadyGrouped) {
      // Keep the grouped descriptions compact without rebuilding their order.
      if (_entries.capacity() != count)
        std::vector<AstNodeDescription>(_entries).swap(_entries);
    } else {
      // 2. Where each group starts, in the order the containers were met. The
      // scatter below keeps the declaration order inside each group.
      std::uint32_t next = 0;
      for (std::uint32_t k = 0; k < containers; ++k) {
        Group& g = _groups[met[k]];
        g.begin = next;
        holders[k].next = next;
        next += g.count;
      }

      // 3. The counting sort, by destination: each position learns which entry
      // it takes, then the entries are copied in position order, so the new array
      // is written once and never cleared first.
      const auto from = std::make_unique_for_overwrite<std::uint32_t[]>(count);
      for (std::size_t i = 0; i < count; ++i)
        from[holders[holderOf[i]].next++] = static_cast<std::uint32_t>(i);
      std::vector<AstNodeDescription> grouped;
      grouped.reserve(count);
      for (std::size_t at = 0; at < count; ++at)
        grouped.push_back(_entries[from[at]]);
      _entries = std::move(grouped);
    }

    // The groups now carry every container and range the name table needs.
    // Release the build scaffolding before allocating that table.
    _containerOf.clear();
    _containerOf.shrink_to_fit();
  }

  // 4. The name table, after the last move: its keys are views of the entries'
  // names. The groups go in the order they were met, the first keeping its
  // slots; each run is walked backwards so homonyms are prepended in
  // declaration order.
  _names.assign(table_size(count), NameSlot{});
  _nameMask = _names.size() - 1;
  _nextSameName.assign(count, kNone);
  for (const std::uint32_t g : met) {
    const AstNode* container = _groups[g].container;
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

  _shortcuts = std::make_unique<Shortcuts>();
}

namespace {

// Hands `keep` every node at a depth that is a multiple of `stride` with at
// least `stride` levels below it, with the nearest node at or above it that
// `holds`. False when the nodes are not in postorder below `top`.
template <class Holds, class Keep>
bool each_shortcut(std::span<AstNode* const> nodes, const AstNode* top,
                   std::uint32_t stride, Holds&& holds, Keep&& keep) {
  // The path from the top to the node being read, with each node's nearest
  // holder, depth, and deepest level below.
  struct Step {
    const AstNode* node;
    const AstNode* holder;
    std::uint32_t depth;
    std::uint32_t deepest;
  };
  std::vector<Step> path;
  const auto leave = [&] {
    const Step step = path.back();
    path.pop_back();
    if (step.depth % stride == 0 && step.deepest - step.depth >= stride)
      keep(step.node, step.holder);
    if (!path.empty())
      path.back().deepest = std::max(path.back().deepest, step.deepest);
  };
  // Postorder read backwards: each node comes after its parent, and a subtree
  // is read whole before the next one.
  for (auto at = nodes.rbegin(); at != nodes.rend(); ++at) {
    const AstNode* const node = *at;
    while (!path.empty() && path.back().node != node->parent) leave();
    if (path.empty() && node->parent != top && node->parent != nullptr)
      return false;
    const Step* const above = path.empty() ? nullptr : &path.back();
    const AstNode* const holder = holds(node)        ? node
                                  : above != nullptr ? above->holder
                                                     : nullptr;
    const std::uint32_t depth = above != nullptr ? above->depth + 1 : 0;
    path.push_back(Step{node, holder, depth, depth});
  }
  while (!path.empty()) leave();
  return true;
}

}  // namespace

const LocalSymbols::Shortcut* LocalSymbols::shortcut(
    const AstNode* node, const AstNode* document) const noexcept {
  Shortcuts* const shortcuts = _shortcuts.get();
  if (shortcuts == nullptr) return nullptr;
  std::uint32_t state = shortcuts->state.load(std::memory_order_acquire);
  // Only the document the walk ends at is read; a walk ending elsewhere uses no
  // shortcut.
  const Document* const tree =
      state == Shortcuts::kUnread ? ast_cast<Document>(document) : nullptr;
  if (state == Shortcuts::kUnread && tree == nullptr) return nullptr;
  // One walk builds the table in one pass; the others wait for it.
  if (state == Shortcuts::kUnread &&
      shortcuts->state.compare_exchange_strong(state, Shortcuts::kReading,
                                               std::memory_order_acquire)) {
    try {
      std::vector<Shortcut> found;
      if (!each_shortcut(
              tree->parseResult.nodes(), document, kStride,
              [&](const AstNode* at) {
                return may_hold(at) && group(at) != nullptr;
              },
              [&](const AstNode* at, const AstNode* holder) {
                found.push_back(Shortcut{at, holder});
              }))
        found.clear();
      shortcuts->top = document;
      if (!found.empty()) {
        shortcuts->table.assign(table_size(found.size()), Shortcut{});
        shortcuts->mask = shortcuts->table.size() - 1;
        for (const Shortcut& entry : found) {
          std::size_t at =
              static_cast<std::size_t>(mix(entry.node)) & shortcuts->mask;
          while (shortcuts->table[at].node != nullptr)
            at = (at + 1) & shortcuts->mask;
          shortcuts->table[at] = entry;
        }
      }
    } catch (...) {
      // Memory refused: the table stays empty, and walks go up level by level.
    }
    shortcuts->state.store(Shortcuts::kRead, std::memory_order_release);
    detail::wake_all(shortcuts->state);
  } else if (state == Shortcuts::kReading) {
    detail::wait_while_equal(shortcuts->state,
                             std::uint32_t{Shortcuts::kReading},
                             std::memory_order_acquire);
  }
  if (document != shortcuts->top || shortcuts->table.empty()) return nullptr;
  std::size_t at = static_cast<std::size_t>(mix(node)) & shortcuts->mask;
  for (;;) {
    const Shortcut& entry = shortcuts->table[at];
    if (entry.node == nullptr) return nullptr;
    if (entry.node == node) return &entry;
    at = (at + 1) & shortcuts->mask;
  }
}

LocalSymbols::NameMatches LocalSymbols::find_held(
    const AstNode* container, std::string_view name,
    std::uint64_t nameHash) const noexcept {
  assert((_entries.empty() || !_names.empty()) &&
         "LocalSymbols::build_index() has to run before the first lookup");
  const std::uint64_t hash = mix(container) ^ nameHash;
  const auto tag = static_cast<std::uint32_t>(hash >> 32u);
  std::size_t at = static_cast<std::size_t>(hash) & _nameMask;
  for (;;) {
    const NameSlot& slot = _names[at];
    if (slot.container == nullptr) return {};
    if (slot.container == container && slot.tag == tag) {
      if (folded_equal(_entries[slot.first].name, name))
        return NameMatches{this, slot.first};
    }
    at = (at + 1) & _nameMask;
  }
}

const LocalSymbols::Group* LocalSymbols::group(
    const AstNode* container) const noexcept {
  assert((_entries.empty() || !_groups.empty()) &&
         "LocalSymbols::build_index() has to run before the first lookup");
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
