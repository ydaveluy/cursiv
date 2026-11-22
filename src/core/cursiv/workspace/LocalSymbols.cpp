#include "cursiv/workspace/LocalSymbols.hpp"

#include <algorithm>
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
  _groups.clear();
  _names.clear();
  _nameMask = 0;
  _nextSameName.clear();
  _held.reset();
  _shortcuts.reset();
}

void LocalSymbols::reserve(std::size_t entries) { _entries.reserve(entries); }

void LocalSymbols::add(const AstNode* container,
                       AstNodeDescription description) {
  // A null container marks an empty group.
  assert(container != nullptr && "a local symbol needs a container");
  if (_groups.empty() || _groups.back().container != container)
    _groups.push_back(
        {container, static_cast<std::uint32_t>(_entries.size()), 0});
  _entries.push_back(std::move(description));
}

void LocalSymbols::buildIndex() {
  const std::size_t count = _entries.size();
  if (count == 0 || !_names.empty()) return;
  if (count > (std::size_t{1} << 31u) || count > _names.max_size() / 2)
    throw std::length_error("cursiv: too many local symbols in one document");

  // Link disjoint runs of the same scope, preserving declaration order.
  std::vector<std::uint32_t> slots;
  {
    auto runs = std::move(_groups);
    std::vector<std::uint32_t> last(detail::table_size(runs.size()), kNone);
    std::vector<std::uint32_t> next;
    for (std::uint32_t i = 0; i < runs.size(); ++i) {
      Group& run = runs[i];
      run.count = (i + 1 < runs.size() ? runs[i + 1].begin : count) - run.begin;
      std::size_t at = mix(run.container) & (last.size() - 1);
      while (last[at] != kNone && runs[last[at]].container != run.container)
        at = (at + 1) & (last.size() - 1);
      if (last[at] == kNone) {
        slots.push_back(i);
      } else {
        if (next.empty()) next.assign(runs.size(), kNone);
        next[last[at]] = i;
      }
      last[at] = i;
    }

    _groups.assign(detail::table_size(slots.size()), Group{});
    const auto mask = _groups.size() - 1;
    const std::size_t words = _groups.size() / 2;
    _held = std::make_unique<std::uint64_t[]>(1 + words);
    _held[0] = words * 64 - 1;
    std::vector<AstNodeDescription> grouped;
    const bool interleaved = !next.empty();
    if (interleaved) grouped.reserve(count);
    for (auto& slot : slots) {
      const auto first = slot;
      Group group = runs[first];
      if (interleaved) {
        group.begin = static_cast<std::uint32_t>(grouped.size());
        for (auto i = first; i != kNone; i = next[i]) {
          const auto begin = _entries.begin() + runs[i].begin;
          grouped.insert(grouped.end(), begin, begin + runs[i].count);
        }
        group.count = static_cast<std::uint32_t>(grouped.size()) - group.begin;
      }
      std::size_t at = mix(group.container) & mask;
      while (_groups[at].container != nullptr) at = (at + 1) & mask;
      _groups[at] = group;
      slot = static_cast<std::uint32_t>(at);
      const std::size_t bit = heldBit(group.container, _held[0]);
      _held[1 + (bit >> 6u)] |= std::uint64_t{1} << (bit & 63u);
    }
    if (grouped.empty())
      _entries.shrink_to_fit();
    else
      _entries = std::move(grouped);
  }

  _names.resize(detail::table_size(count));
  _nameMask = _names.size() - 1;
  for (const auto slotIndex : slots) {
    const Group& group = _groups[slotIndex];
    const auto scopeHash = mix(group.container);
    for (auto i = group.begin + group.count; i-- > group.begin;) {
      const auto name = _entries[i].name;
      const auto hash = scopeHash ^ hashName(name);
      const auto at = nameSlot(group.container, hash, name).first;
      auto& slot = _names[at];
      if (slot.first != kNone) {
        if (_nextSameName.empty()) _nextSameName.assign(count, kNone);
        _nextSameName[i] = slot.first;
      }
      slot = {group.container, i, static_cast<std::uint32_t>(hash >> 32u)};
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
              [&](const AstNode* at) { return group(at) != nullptr; },
              [&](const AstNode* at, const AstNode* holder) {
                found.push_back(Shortcut{at, holder});
              }))
        found.clear();
      shortcuts->top = document;
      if (!found.empty()) {
        shortcuts->table.assign(detail::table_size(found.size()), Shortcut{});
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

LocalSymbols::NameMatches LocalSymbols::find(
    const AstNode* container, std::string_view name,
    std::uint64_t nameHash) const noexcept {
  if (!mayHold(container)) return {};
  const auto at = nameSlot(container, mix(container) ^ nameHash, name).first;
  return {this, _names[at].first};
}

std::span<const AstNodeDescription> LocalSymbols::forContainer(
    const AstNode* container) const noexcept {
  const Group* g = group(container);
  return g == nullptr ? std::span<const AstNodeDescription>{}
                      : std::span<const AstNodeDescription>(
                            _entries.data() + g->begin, g->count);
}

}  // namespace cursiv
