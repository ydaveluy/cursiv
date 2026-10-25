// The stacks a repair search reads, as a cactus: a node is a state over a
// parent, interned, so one stack is one index and two readings on the same
// states compare in a word, whichever frozen stack they set out from. Beside
// it runs a chain of where each frame began, in tokens: no future depends on
// it, so it is laid, never interned.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/runtime/LrStack.hpp"

namespace cursiv {

inline constexpr std::uint32_t kLrNoNode = 0xFFFFFFFFu;

class LrCactus {
 public:
  // A frame of a frozen stack: which of them, and its depth there.
  struct Frozen {
    std::uint32_t source;
    std::uint32_t frame;
  };
  // Where a frozen stack stands on the cactus.
  struct Top {
    std::uint32_t node;
    std::uint32_t begin;
    std::uint32_t depth;
  };

  void reset() {
    _nodes.clear();
    _roots = kLrNoNode;
    _begins.clear();
    _frozen.clear();
  }

  // Where the laid pools stand, and back to it: what an attempt laid since is
  // given up with the reading that laid it. No survivor names a node past the
  // mark, so a node leaves its parent's children and the tails are popped.
  struct Mark {
    std::uint32_t nodes;
    std::uint32_t begins;
  };
  [[nodiscard]] Mark mark() const {
    return Mark{static_cast<std::uint32_t>(_nodes.size()),
                static_cast<std::uint32_t>(_begins.size())};
  }
  void rewind(const Mark& m) {
    while (_nodes.size() > m.nodes) {
      const auto n = static_cast<std::uint32_t>(_nodes.size() - 1);
      const std::uint32_t parent = _nodes[n].parent;
      std::uint32_t* link =
          parent == kLrNoNode ? &_roots : &_nodes[parent].child;
      while (*link != n) link = &_nodes[*link].sibling;
      *link = _nodes[n].sibling;
      _nodes.pop_back();
    }
    _begins.resize(m.begins);
  }

  // A frozen stack, laid bottom up: its frames are nodes like any other, and
  // its begins name the frames themselves, as `-1 - f` for frozen frame `f`.
  Top add(const LrStack& src, std::uint32_t source) {
    Top t{kLrNoNode, kLrNoNode, src.size()};
    for (std::uint32_t i = 0; i < src.size(); ++i) {
      t.node = intern(src[i].state, t.node);
      const auto f = static_cast<std::int64_t>(_frozen.size());
      _frozen.push_back(Frozen{source, i});
      t.begin = begin_push(-1 - f, t.begin);
    }
    return t;
  }
  [[nodiscard]] const Frozen& frozen(std::uint32_t f) const {
    return _frozen[f];
  }
  [[nodiscard]] std::uint32_t frozen_count() const {
    return static_cast<std::uint32_t>(_frozen.size());
  }

  [[nodiscard]] std::uint16_t state(std::uint32_t n) const {
    return _nodes[n].state;
  }
  [[nodiscard]] std::uint32_t parent(std::uint32_t n) const {
    return _nodes[n].parent;
  }
  [[nodiscard]] std::uint32_t depth(std::uint32_t n) const {
    return _nodes[n].depth;
  }
  // A node's children are few; the one met last goes first, as readings
  // reduce into the same state again and again.
  std::uint32_t intern(std::uint16_t state, std::uint32_t parent) {
    std::uint32_t& first = parent == kLrNoNode ? _roots : _nodes[parent].child;
    for (std::uint32_t prev = kLrNoNode, n = first; n != kLrNoNode;
         prev = n, n = _nodes[n].sibling) {
      if (_nodes[n].state != state) continue;
      if (prev != kLrNoNode) {
        _nodes[prev].sibling = _nodes[n].sibling;
        _nodes[n].sibling = first;
        first = n;
      }
      return n;
    }
    const auto n = static_cast<std::uint32_t>(_nodes.size());
    const std::uint32_t d = parent == kLrNoNode ? 1u : depth(parent) + 1;
    const std::uint32_t next = first;
    first = n;
    _nodes.push_back(Node{state, parent, d, kLrNoNode, next});
    return n;
  }

  std::uint32_t begin_push(std::int64_t at, std::uint32_t parent) {
    _begins.push_back(Begin{at, parent});
    return static_cast<std::uint32_t>(_begins.size()) - 1;
  }
  [[nodiscard]] std::int64_t begin_of(std::uint32_t b) const {
    return _begins[b].at;
  }
  [[nodiscard]] std::uint32_t begin_parent(std::uint32_t b) const {
    return _begins[b].parent;
  }

 private:
  struct Node {
    std::uint16_t state;
    std::uint32_t parent;
    std::uint32_t depth;
    std::uint32_t child;
    std::uint32_t sibling;
  };
  struct Begin {
    std::int64_t at;
    std::uint32_t parent;
  };
  std::vector<Node> _nodes;
  std::uint32_t _roots = kLrNoNode;
  std::vector<Begin> _begins;
  std::vector<Frozen> _frozen;
};

}  // namespace cursiv
