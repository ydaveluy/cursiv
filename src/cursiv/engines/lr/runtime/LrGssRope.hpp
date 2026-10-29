// Persistent sequences of 32-bit values under concatenation, balanced by
// height. A node exists once: sequences joined alike share it, so identical
// nodes are told by their ids, and two sequences meet at the first value that
// differs without a hash concluding anything.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "cursiv/engines/lr/runtime/LrGssPool.hpp"

namespace cursiv {

namespace detail {
// Lengths describe shared sequences, not the number of stored nodes. A tiny
// DAG can outgrow this range; wrapping or saturating would change its order.
inline std::uint64_t gss_length_add(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw std::length_error("cursiv: GSS sequence exceeds its length range");
  return a + b;
}
}  // namespace detail

// Nothing aggregated.
struct LrGssNoAgg {
  static LrGssNoAgg join(const LrGssNoAgg&, const LrGssNoAgg&) { return {}; }
  bool operator==(const LrGssNoAgg&) const = default;
};

// The furthest position any value of a sequence names.
struct LrGssForkAgg {
  std::uint32_t at = 0;

  static LrGssForkAgg join(const LrGssForkAgg& left,
                           const LrGssForkAgg& right) {
    return {std::max(left.at, right.at)};
  }
  bool operator==(const LrGssForkAgg&) const = default;
};

template <class Agg>
class LrGssRope {
 public:
  using Id = std::uint32_t;
  using Length = std::uint64_t;
  static constexpr Id none = ~Id{0};

 private:
  // An AVL tree gains at least a factor of two in leaves every two levels.
  // This also leaves room for the final leaf on a frontier's stack.
  static constexpr std::size_t kMaxDepth =
      2 * std::numeric_limits<Length>::digits;

 public:
  // A position in a sequence, read forward.
  class Cursor {
   public:
    void seek(const LrGssRope& rope, Id root, Length index) {
      _rope = &rope;
      _depth = 0;
      _leaf = none;
      if (root == none || index >= rope._nodes[root].len) return;
      Id n = root;
      while (rope._nodes[n].right != none) {
        const Node& x = rope._nodes[n];
        const Length before = rope._nodes[x.left].len;
        if (index < before) {
          push(x.right);
          n = x.left;
        } else {
          index -= before;
          n = x.right;
        }
      }
      _leaf = n;
    }
    [[nodiscard]] bool done() const { return _leaf == none; }
    [[nodiscard]] std::uint32_t value() const {
      return _rope->_nodes[_leaf].left;
    }
    [[nodiscard]] const Agg& agg() const { return _rope->_nodes[_leaf].agg; }
    void next() {
      if (_depth == 0) {
        _leaf = none;
        return;
      }
      Id n = _stack[--_depth];
      while (_rope->_nodes[n].right != none) {
        push(_rope->_nodes[n].right);
        n = _rope->_nodes[n].left;
      }
      _leaf = n;
    }

   private:
    void push(Id n) {
      assert(_depth != _stack.size());
      _stack[_depth++] = n;
    }
    const LrGssRope* _rope = nullptr;
    std::array<Id, kMaxDepth> _stack;
    std::size_t _depth = 0;
    Id _leaf = none;
  };

  void clear() {
    _nodes.clear();
    _slots.clear();
  }
  [[nodiscard]] std::size_t nodes() const { return _nodes.size(); }
  [[nodiscard]] Length length(Id r) const {
    return r == none ? 0 : _nodes[r].len;
  }
  [[nodiscard]] std::uint32_t depth(Id r) const {
    return r == none ? 0 : _nodes[r].height;
  }

  Id leaf(std::uint32_t value, const Agg& agg = {}) {
    return intern(Node{value, none, 1, 0, agg});
  }

  Id concat(Id a, Id b) {
    if (a == none) return b;
    if (b == none) return a;
    const int ha = height(a), hb = height(b);
    if (ha > hb + 1) {
      const Node n = _nodes[a];
      return balanced(n.left, concat(n.right, b));
    }
    if (hb > ha + 1) {
      const Node n = _nodes[b];
      return balanced(concat(a, n.left), n.right);
    }
    return join(a, b);
  }

  // The join of `parts` in order. A few meet small ones first, so that a
  // large one is copied once; many meet by halves.
  Id concat_all(std::vector<Id>& parts) {
    std::erase(parts, none);
    if (parts.size() > kFew) return halves(parts, 0, parts.size());
    while (parts.size() > 1) {
      std::size_t at = 0;
      for (std::size_t i = 1; i + 1 < parts.size(); ++i)
        if (std::max(height(parts[i]), height(parts[i + 1])) <
            std::max(height(parts[at]), height(parts[at + 1])))
          at = i;
      parts[at] = concat(parts[at], parts[at + 1]);
      parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(at) + 1);
    }
    return parts.empty() ? none : parts[0];
  }

  [[nodiscard]] std::uint32_t at(Id r, Length index) const {
    while (_nodes[r].right != none) {
      const Node& x = _nodes[r];
      const Length before = _nodes[x.left].len;
      if (index < before) {
        r = x.left;
      } else {
        index -= before;
        r = x.right;
      }
    }
    return _nodes[r].left;
  }

  // The first index whose value, with all those before it, satisfies `holds`
  // -- a predicate prefixes only turn true --, or the length where none does.
  template <class Holds>
  [[nodiscard]] Length first_where(Id r, Holds&& holds) const {
    if (r == none) return 0;
    Prefix acc;
    const auto with = [&](const Agg& next) {
      Prefix p = acc;
      p.add(next);
      return p;
    };
    if (!holds(with(_nodes[r].agg).agg)) return _nodes[r].len;
    Length index = 0;
    while (_nodes[r].right != none) {
      const Node& x = _nodes[r];
      const Length before = _nodes[x.left].len;
      if (holds(with(_nodes[x.left].agg).agg)) {
        r = x.left;
      } else {
        acc.add(_nodes[x.left].agg);
        index += before;
        r = x.right;
      }
    }
    return index;
  }

  // How many values the sequences `a` from `oa` and `b` from `ob` share, within
  // `n`, each the roots joined in order. Identical nodes are skipped whole; the
  // rest is read down to its values.
  [[nodiscard]] Length common(std::span<const Id> a, Length oa,
                              std::span<const Id> b, Length ob,
                              Length n) const {
    if (n == 0) return 0;
    Front fa, fb;
    open(fa, a, oa);
    open(fb, b, ob);
    Length l = 0;
    while (l < n && more(fa) && more(fb)) {
      const Id x = fa.top(), y = fb.top();
      const Node& nx = _nodes[x];
      const Node& ny = _nodes[y];
      if (x == y) {
        if (n - l <= nx.len) return n;
        l += nx.len;
        fa.pop();
        fb.pop();
        continue;
      }
      if (nx.right == none && ny.right == none) {
        if (nx.left != ny.left) return l;
        ++l;
        fa.pop();
        fb.pop();
        continue;
      }
      Front& taller = nx.height >= ny.height ? fa : fb;
      const Node m = _nodes[taller.top()];
      taller.pop();
      taller.push(m.right);
      taller.push(m.left);
    }
    return l;
  }
  [[nodiscard]] Length common(Id a, Length oa, Id b, Length ob,
                              Length n) const {
    return common(std::span(&a, 1), oa, std::span(&b, 1), ob, n);
  }

 private:
  // The aggregate of a prefix: `any` is false for the empty one.
  struct Prefix {
    Agg agg{};
    bool any = false;
    void add(const Agg& next) {
      agg = any ? Agg::join(agg, next) : next;
      any = true;
    }
  };

  struct Node {
    // The value of a leaf, the left child of a node.
    Id left;
    Id right;
    Length len;
    std::uint8_t height;
    Agg agg;
  };

  // The subtrees of the root being read still to read, the next on top, and
  // the roots after it.
  struct Front {
    std::array<Id, kMaxDepth> stack;
    std::size_t size = 0;
    std::span<const Id> rest;
    [[nodiscard]] Id top() const { return stack[size - 1]; }
    void pop() { --size; }
    void push(Id n) {
      assert(size != stack.size());
      stack[size++] = n;
    }
  };

  void open(Front& f, std::span<const Id> roots, Length offset) const {
    std::size_t i = 0;
    while (i != roots.size() && offset >= _nodes[roots[i]].len) {
      offset -= _nodes[roots[i]].len;
      ++i;
    }
    if (i == roots.size()) return;
    f.rest = roots.subspan(i + 1);
    descend(f, roots[i], offset);
  }

  void descend(Front& f, Id n, Length offset) const {
    while (_nodes[n].right != none) {
      const Node& x = _nodes[n];
      const Length before = _nodes[x.left].len;
      if (offset < before) {
        f.push(x.right);
        n = x.left;
      } else {
        offset -= before;
        n = x.right;
      }
    }
    f.push(n);
  }

  // Whether `f` has a value to read, moving on to the next root where the
  // one it read is spent.
  bool more(Front& f) const {
    if (f.size == 0 && !f.rest.empty()) {
      descend(f, f.rest.front(), 0);
      f.rest = f.rest.subspan(1);
    }
    return f.size != 0;
  }

  [[nodiscard]] int height(Id r) const {
    return static_cast<int>(_nodes[r].height);
  }

  static constexpr std::size_t kFew = 8;

  Id halves(const std::vector<Id>& parts, std::size_t lo, std::size_t hi) {
    if (hi - lo == 1) return parts[lo];
    const std::size_t mid = lo + (hi - lo) / 2;
    return concat(halves(parts, lo, mid), halves(parts, mid, hi));
  }

  Id join(Id l, Id r) {
    const Node& a = _nodes[l];
    const Node& b = _nodes[r];
    return intern(
        Node{l, r, detail::gss_length_add(a.len, b.len),
             static_cast<std::uint8_t>(std::max(a.height, b.height) + 1),
             Agg::join(a.agg, b.agg)});
  }

  // The join of two balanced trees whose heights differ by two at most.
  Id balanced(Id l, Id r) {
    const int hl = height(l), hr = height(r);
    if (hl > hr + 1) {
      const Node n = _nodes[l];
      if (height(n.left) >= height(n.right))
        return join(n.left, join(n.right, r));
      const Node m = _nodes[n.right];
      return join(join(n.left, m.left), join(m.right, r));
    }
    if (hr > hl + 1) {
      const Node n = _nodes[r];
      if (height(n.right) >= height(n.left))
        return join(join(l, n.left), n.right);
      const Node m = _nodes[n.left];
      return join(join(l, m.left), join(m.right, n.right));
    }
    return join(l, r);
  }

  static std::size_t slot_of(Id left, Id right, std::size_t mask) {
    std::uint64_t h =
        (std::uint64_t{left} << 32 | right) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 32;
    return static_cast<std::size_t>(h) & mask;
  }

  // The node equal to `n`, made where none is.
  Id intern(const Node& n) {
    if (2 * (_nodes.size() + 1) > _slots.size()) grow();
    const std::size_t mask = _slots.size() - 1;
    std::size_t at = slot_of(n.left, n.right, mask);
    for (; _slots[at] != none; at = (at + 1) & mask) {
      const Node& m = _nodes[_slots[at]];
      if (m.left == n.left && m.right == n.right && m.agg == n.agg)
        return _slots[at];
    }
    const Id id = static_cast<Id>(_nodes.push_back(n));
    _slots[at] = id;
    return id;
  }

  void grow() {
    std::size_t size = 16;
    while (size < 4 * (_nodes.size() + 1)) size *= 2;
    std::vector<Id> next(size, none);
    const std::size_t mask = size - 1;
    for (std::size_t id = 0; id != _nodes.size(); ++id) {
      const Node& m = _nodes[id];
      std::size_t at = slot_of(m.left, m.right, mask);
      while (next[at] != none) at = (at + 1) & mask;
      next[at] = static_cast<Id>(id);
    }
    _slots.swap(next);
  }

  LrGssPool<Node> _nodes;
  std::vector<Id> _slots;
};

}  // namespace cursiv
