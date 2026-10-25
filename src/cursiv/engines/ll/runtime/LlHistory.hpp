// Histories share both prefixes and continuations. Perfect blocks are
// interned by content, and a history is the canonical forest given by the
// binary digits of its length. Equality uses exact identities, not a digest.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cursiv {

class LlHistory {
 public:
  using Node = std::uint32_t;
  static constexpr Node kRoot = 0;

  LlHistory() { clear(); }
  void clear() {
    _nodes.assign(1, Entry{});
    _blocks.assign(1, Block{});
    _table.assign(kMinTable, 0u);
    _blockTable.assign(kMinTable, 0u);
  }
  std::size_t size() const { return _nodes.size(); }
  void trim() {
    if (_nodes.capacity() > kKeep || _blocks.capacity() > kKeep) {
      std::vector<Entry>().swap(_nodes);
      std::vector<Block>().swap(_blocks);
      std::vector<Node>().swap(_table);
      std::vector<Node>().swap(_blockTable);
      clear();
    }
  }

  Node parent(Node n) const {
    if (n == kRoot) return kRoot;
    if (_nodes[n].parent == kUnknown) {
      const auto prefix = ancestor(n, depth(n) - 1u);
      _nodes[n].parent = prefix;
    }
    return _nodes[n].parent;
  }
  std::uint32_t word(Node n) const { return last(n).word; }
  std::uint32_t depth(Node n) const { return _nodes[n].depth; }
  std::uint32_t at(Node n) const { return last(n).at; }
  std::uint32_t lull(Node n) const { return last(n).lull; }

  Node append(Node parent, std::uint32_t word, std::uint32_t at = 0,
              std::uint32_t lull = 0) {
    Block leaf;
    leaf.word = word;
    leaf.at = at;
    leaf.lull = lull;
    const Node made = append_block(parent, intern_block(leaf));
    _nodes[made].parent = parent;
    return made;
  }

  Node common(Node a, Node b) const {
    if (a == b) return a;
    std::array<Node, 64> left{}, right{};
    unsigned nl = chunks(a, left), nr = chunks(b, right);
    std::uint32_t shared = 0;
    while (nl != 0 && nr != 0) {
      const Node x = left[nl - 1u], y = right[nr - 1u];
      if (x == y) {
        shared += length(x);
        --nl;
        --nr;
      } else if (_blocks[x].height > _blocks[y].height) {
        split(left, nl);
      } else if (_blocks[x].height < _blocks[y].height) {
        split(right, nr);
      } else if (_blocks[x].height != 0) {
        split(left, nl);
        split(right, nr);
      } else {
        break;
      }
    }
    return ancestor(a, shared);
  }

  // The words of the first events where two histories differ; false where
  // one runs into the other. Names no prefix, unlike `common`.
  bool parts(Node a, Node b, std::uint32_t& wordA, std::uint32_t& wordB) const {
    if (a == b) return false;
    std::array<Node, 64> left{}, right{};
    unsigned nl = chunks(a, left), nr = chunks(b, right);
    while (nl != 0 && nr != 0) {
      const Node x = left[nl - 1u], y = right[nr - 1u];
      if (x == y) {
        --nl;
        --nr;
      } else if (_blocks[x].height > _blocks[y].height) {
        split(left, nl);
      } else if (_blocks[x].height < _blocks[y].height) {
        split(right, nr);
      } else if (_blocks[x].height != 0) {
        split(left, nl);
        split(right, nr);
      } else {
        wordA = _blocks[x].word;
        wordB = _blocks[y].word;
        return true;
      }
    }
    return false;
  }

  // Positions distinguish histories. At their first differing event only
  // its word ranks them; positions add no tie breaker.
  bool before(Node a, Node b) const {
    std::uint32_t x = 0, y = 0;
    if (parts(a, b, x, y)) return x < y;
    return depth(a) < depth(b);
  }

  Node graft(Node base, Node from, Node to) {
    if (from == to) return base;
    std::uint32_t skip = depth(from);
    if (skip > depth(to) || ancestor(to, skip) != from) skip = 0;
    if (base == from && skip == depth(from)) return to;
    std::array<Node, 64> blocks{};
    unsigned count = chunks(to, blocks);
    while (count != 0) {
      const Node b = blocks[--count];
      const auto n = length(b);
      if (skip >= n) {
        skip -= n;
      } else {
        base = append_suffix(base, b, skip);
        skip = 0;
      }
    }
    return base;
  }

  // A query can intern a previously unnamed prefix. Its identity is
  // canonical too, so callers can still test ancestry by node identity.
  Node ancestor(Node n, std::uint32_t d) const {
    if (depth(n) <= d) return n;
    while (n != kRoot && _nodes[_nodes[n].prev].depth >= d) n = _nodes[n].prev;
    if (n == kRoot) return kRoot;
    Node prefix = _nodes[n].prev;
    std::uint32_t take = d - depth(prefix);
    Node block = _nodes[n].block;
    while (take != 0) {
      const auto width = length(block);
      if (take == width) return intern(prefix, block);
      const Block b = _blocks[block];
      const auto half = width / 2u;
      if (take >= half) {
        prefix = intern(prefix, b.left);
        take -= half;
        block = b.right;
      } else {
        block = b.left;
      }
    }
    return prefix;
  }

 private:
  static constexpr std::size_t kMinTable = 16;
  static constexpr std::size_t kKeep = std::size_t{1} << 18;
  static constexpr Node kUnknown = ~Node{0};
  struct Entry {
    Node prev = 0, block = 0;
    std::uint32_t depth = 0;
    Node parent = kUnknown;
  };
  struct Block {
    Node left = 0, right = 0, last = 0;
    std::uint32_t height = 0;
    std::uint32_t word = 0, at = 0, lull = 0;
  };

  const Block& last(Node n) const {
    return _blocks[_blocks[_nodes[n].block].last];
  }
  std::uint32_t length(Node block) const {
    return std::uint32_t{1} << _blocks[block].height;
  }
  unsigned chunks(Node n, std::array<Node, 64>& out) const {
    unsigned count = 0;
    for (; n != kRoot; n = _nodes[n].prev) out[count++] = _nodes[n].block;
    return count;
  }
  void split(std::array<Node, 64>& stack, unsigned& count) const {
    const Block b = _blocks[stack[--count]];
    stack[count++] = b.right;
    stack[count++] = b.left;
  }
  static std::uint64_t hash(Node a, Node b) {
    return ((std::uint64_t{a} << 32) | b) * 0x9E3779B97F4A7C15ull;
  }
  static std::uint64_t hash(const Block& b) {
    if (b.height != 0) return hash(b.left, b.right);
    return (hash(b.word, b.at) ^ b.lull ^ 0xD6E8FEB86659FD93ull) *
           0x9E3779B97F4A7C15ull;
  }
  static std::size_t slot(std::uint64_t h, std::size_t room) {
    return static_cast<std::size_t>(
        h >> (64u - static_cast<unsigned>(std::countr_zero(room))));
  }
  static bool equal(const Block& a, const Block& b) {
    if (a.height != b.height) return false;
    return a.height != 0 ? a.left == b.left && a.right == b.right
                         : a.word == b.word && a.at == b.at && a.lull == b.lull;
  }

  Node intern(Node prev, Node block) const {
    std::size_t at = slot(hash(prev, block), _table.size());
    for (const auto mask = _table.size() - 1u; _table[at] != 0;
         at = (at + 1u) & mask) {
      const Entry& e = _nodes[_table[at]];
      if (e.prev == prev && e.block == block) return _table[at];
    }
    if ((_nodes.size() + 1u) * 2u > _table.size()) {
      _table.assign(_table.size() * 2u, 0u);
      const auto mask = _table.size() - 1u;
      for (Node n = 1; n < _nodes.size(); ++n) {
        std::size_t i =
            slot(hash(_nodes[n].prev, _nodes[n].block), _table.size());
        while (_table[i] != 0) i = (i + 1u) & mask;
        _table[i] = n;
      }
      return intern(prev, block);
    }
    const auto id = static_cast<Node>(_nodes.size());
    _nodes.push_back({prev, block, depth(prev) + length(block), kUnknown});
    _table[at] = id;
    return id;
  }

  Node intern_block(Block b) const {
    std::size_t at = slot(hash(b), _blockTable.size());
    for (const auto mask = _blockTable.size() - 1u; _blockTable[at] != 0;
         at = (at + 1u) & mask)
      if (equal(_blocks[_blockTable[at]], b)) return _blockTable[at];
    if ((_blocks.size() + 1u) * 2u > _blockTable.size()) {
      _blockTable.assign(_blockTable.size() * 2u, 0u);
      const auto mask = _blockTable.size() - 1u;
      for (Node n = 1; n < _blocks.size(); ++n) {
        std::size_t i = slot(hash(_blocks[n]), _blockTable.size());
        while (_blockTable[i] != 0) i = (i + 1u) & mask;
        _blockTable[i] = n;
      }
      return intern_block(b);
    }
    const auto id = static_cast<Node>(_blocks.size());
    if (b.height == 0) b.last = id;
    _blocks.push_back(b);
    _blockTable[at] = id;
    return id;
  }

  Node append_block(Node base, Node block) const {
    if (depth(base) > ~std::uint32_t{0} - length(block))
      throw std::length_error("LL derivation history exceeds its length range");
    if (base != kRoot &&
        _blocks[_nodes[base].block].height < _blocks[block].height) {
      const Block b = _blocks[block];
      return append_block(append_block(base, b.left), b.right);
    }
    while (base != kRoot &&
           _blocks[_nodes[base].block].height == _blocks[block].height) {
      const Node left = _nodes[base].block;
      Block joined;
      joined.left = left;
      joined.right = block;
      joined.last = _blocks[block].last;
      joined.height = _blocks[block].height + 1u;
      block = intern_block(joined);
      base = _nodes[base].prev;
    }
    return intern(base, block);
  }
  Node append_suffix(Node base, Node block, std::uint32_t skip) const {
    if (skip == 0) return append_block(base, block);
    const Block b = _blocks[block];
    const auto half = length(block) / 2u;
    if (skip >= half) return append_suffix(base, b.right, skip - half);
    return append_block(append_suffix(base, b.left, skip), b.right);
  }

  // Const prefix/LCP queries may name canonical prefixes on demand.
  mutable std::vector<Entry> _nodes;
  mutable std::vector<Block> _blocks;
  mutable std::vector<Node> _table, _blockTable;
};

}  // namespace cursiv
