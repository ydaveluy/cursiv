// Histories share both prefixes and continuations. A history is a balanced
// tree of runs -- one event repeated -- followed by a short queue. Complete
// histories are interned by their events, even when concatenation gave their
// trees different shapes: equality is identity, never a digest. Most appends
// cost a link; a full queue goes into the tree once, for every history that
// goes on from it. Concatenation copies only paths in the balanced trees.
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

  struct Event {
    std::uint32_t word = 0, at = 0, lull = 0;
    bool operator==(const Event&) const = default;
  };

  LlHistory() { clear(); }
  void clear() {
    _trees.clear();
    _heads.clear();
  }
  std::size_t size() const { return _trees.rows.size() + _heads.rows.size(); }
  void trim() {
    if (_trees.rows.capacity() > kKeep || _heads.rows.capacity() > kKeep) {
      _trees.release();
      _heads.release();
      clear();
    }
  }

  std::uint32_t depth(Node n) const { return _heads.rows[n].size; }
  // The `i`th event of `n`, the first at zero.
  Event event(Node n, std::uint32_t i) const {
    const Head& h = _heads.rows[n];
    if (i < weight(h.tree)) return tree_event(h.tree, i);
    for (std::uint32_t up = h.size - 1u - i; up != 0; --up)
      n = _heads.rows[n].up;
    return _heads.rows[n].event;
  }
  Event last(Node n) const { return _heads.rows[n].event; }

  Node append(Node n, std::uint32_t word, std::uint32_t at = 0,
              std::uint32_t lull = 0) {
    const Event e{word, at, lull};
    const Head h = _heads.rows[n];
    if (h.size == ~std::uint32_t{0}) throw too_long();
    if (h.queued < kTail)
      return intern_head({h.tree, n, e, h.size + 1u, h.queued + 1u});
    const Node made = intern_head({whole(n), n, e, h.size + 1u, 1u});
    _heads.rows[made].up = n;
    return made;
  }

  // The first `d` events of `n`. A query can intern a prefix not named yet;
  // its identity is canonical too.
  Node ancestor(Node n, std::uint32_t d) const {
    if (d == 0) return kRoot;
    const Head& h = _heads.rows[n];
    if (d >= h.size) return n;
    if (d >= weight(h.tree)) {
      for (std::uint32_t up = h.size - d; up != 0; --up) n = _heads.rows[n].up;
      if (n != kRoot) return n;
    }
    return settle(h.tree, d);
  }

  bool starts_with(Node n, Node prefix) const {
    if (prefix == kRoot || prefix == n) return true;
    if (depth(prefix) > depth(n)) return false;
    std::uint32_t shared = 0;
    Event a, b;
    diverge(n, prefix, shared, a, b);
    return shared == depth(prefix);
  }

  Node common(Node a, Node b) const {
    if (a == b) return a;
    std::uint32_t shared = 0;
    Event x, y;
    diverge(a, b, shared, x, y);
    return shared == depth(b) ? b : ancestor(a, shared);
  }

  // The words of the first events where two histories differ; false where
  // one runs into the other. Names no prefix, unlike `common`.
  bool parts(Node a, Node b, std::uint32_t& wordA, std::uint32_t& wordB) const {
    if (a == b) return false;
    std::uint32_t shared = 0;
    Event x, y;
    if (!diverge(a, b, shared, x, y)) return false;
    wordA = x.word;
    wordB = y.word;
    return true;
  }

  // Positions distinguish histories. At their first differing event only
  // its word ranks them; positions add no tie breaker.
  bool before(Node a, Node b) const {
    std::uint32_t x = 0, y = 0;
    if (parts(a, b, x, y)) return x < y;
    return depth(a) < depth(b);
  }

  // `base` followed by what `to` took after `from`; all of `to` where `from`
  // does not start it.
  Node graft(Node base, Node from, Node to) {
    if (from == to) return base;
    const std::uint32_t skip = starts_with(to, from) ? depth(from) : 0;
    if (base == from && skip == depth(from)) return to;
    // Keep the queued suffix out of the tree: materializing it before dropping
    // the prefix would copy long paths only to discard them immediately.
    std::array<Event, kTail> tail{};
    const std::uint32_t queued = tail_events(to, tail);
    const Id tree = _heads.rows[to].tree;
    const std::uint32_t treed = weight(tree);
    if (skip < treed) {
      // Where `base` and `from` part only near their ends, what `to` shares
      // with both stays in place and only the rest is replaced.
      std::uint32_t shared = 0;
      Event x, y;
      if (skip != 0) diverge(base, from, shared, x, y);
      Id kept = 0;
      if (depth(base) - shared <= kTail) {
        std::array<Event, kTail> own{};
        std::uint32_t k = 0;
        each(base, shared, [&](const Event& e) {
          own[k++] = e;
          return true;
        });
        kept = tree_splice(tree, shared, skip, own.data(), k);
      } else {
        kept = tree_concat(whole(base), tree_drop(tree, skip));
      }
      return settle(kept, weight(kept), tail.data(), queued);
    }
    Node result = base;
    for (std::uint32_t k = skip - treed; k < queued; ++k)
      result = append(result, tail[k].word, tail[k].at, tail[k].lull);
    return result;
  }

  // The events of `n` from the `from`th on, in order, while `visit` answers
  // true.
  template <class Visit>
  void each(Node n, std::uint32_t from, Visit&& visit) const {
    const Head& h = _heads.rows[n];
    const std::uint32_t treed = weight(h.tree);
    if (from < treed && !tree_each(h.tree, from, visit)) return;
    std::array<Event, kTail> tail{};
    const std::uint32_t queued = tail_events(n, tail);
    for (std::uint32_t k = from > treed ? from - treed : 0; k < queued; ++k)
      if (!visit(tail[k])) return;
  }

 private:
  using Id = std::uint32_t;
  static constexpr std::uint32_t kTail = 8;
  static constexpr std::size_t kMinTable = 16;
  static constexpr std::size_t kKeep = std::size_t{1} << 18;

  static std::uint64_t mix(std::uint64_t h) {
    return h * 0x9E3779B97F4A7C15ull;
  }
  static std::uint64_t hash(const Event& e) {
    auto h = (std::uint64_t{e.word} << 32 | e.at) ^ mix(e.lull);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
  }
  static std::length_error too_long() {
    return std::length_error("LL derivation history exceeds its length range");
  }

  // A run of `count` times `event` between two subtrees; `size` counts the
  // events below. No two neighbouring runs repeat one event.
  struct Tree {
    Id left = 0, right = 0;
    Event event;
    std::uint32_t count = 0, size = 0;
    std::uint32_t height = 0;
    std::uint64_t digest = 0, power = 1;
    bool same(const Tree& o) const {
      return left == o.left && right == o.right && event == o.event &&
             count == o.count;
    }
    std::uint64_t hash() const {
      return mix(mix(std::uint64_t{left} << 32 | right) ^
                 LlHistory::hash(event) ^ count);
    }
  };
  // A history: its tree, then one to `kTail` events chained, the last one
  // here and the others `up`. `up` is the history without its last event --
  // for the first one queued, where that is known, else the root. `whole`
  // holds all its events as one tree once asked.
  struct Head {
    Id tree = 0;
    Node up = kRoot;
    Event event;
    std::uint32_t size = 0, queued = 0;
    Id whole = 0;
    std::uint64_t digest = 0;
    std::uint64_t hash() const {
      return mix(digest ^ mix(size));
    }
  };

  // Rows interned by content; row zero is the empty one.
  template <class Row>
  struct Pool {
    std::vector<Row> rows;
    std::vector<Id> table;
    void clear() {
      rows.assign(1, Row{});
      table.assign(kMinTable, 0u);
    }
    void release() {
      std::vector<Row>().swap(rows);
      std::vector<Id>().swap(table);
    }
    std::size_t slot(std::uint64_t h) const {
      return static_cast<std::size_t>(
          h >> (64u - static_cast<unsigned>(std::countr_zero(table.size()))));
    }
    template <class Same>
    Id intern(const Row& row, Same&& same) {
      std::size_t at = slot(row.hash());
      for (const auto mask = table.size() - 1u; table[at] != 0;
           at = (at + 1u) & mask)
        if (same(rows[table[at]], row)) return table[at];
      if ((rows.size() + 1u) * 2u > table.size()) {
        table.assign(table.size() * 2u, 0u);
        const auto mask = table.size() - 1u;
        for (Id n = 1; n < rows.size(); ++n) {
          std::size_t i = slot(rows[n].hash());
          while (table[i] != 0) i = (i + 1u) & mask;
          table[i] = n;
        }
        return intern(row, same);
      }
      const auto id = static_cast<Id>(rows.size());
      rows.push_back(row);
      table[at] = id;
      return id;
    }
    Id intern(const Row& row) {
      return intern(row, [](const Row& a, const Row& b) { return a.same(b); });
    }
  };

  // What is left to walk: a subtree, or what is left of a run.
  struct Piece {
    Id tree;
    std::uint32_t count;
    Event event;
  };

  // The history of the first `d` events of `tree`, then the `k` events of
  // `more` where `d` takes them all: one to `kTail` last ones queued.
  Node settle(Id tree, std::uint32_t d, const Event* more = nullptr,
              std::uint32_t k = 0) const {
    if (d > ~std::uint32_t{0} - k) throw too_long();
    const std::uint32_t size = d + k;
    if (size == 0) return kRoot;
    const std::uint32_t base = size - ((size - 1u) % kTail + 1u);
    std::array<Event, kTail> queue{};
    std::uint32_t n = 0;
    Id t = 0;
    if (base < d) {
      t = tree_prefix(tree, base);
      tree_each(tree, base, [&](const Event& e) {
        queue[n++] = e;
        return n != d - base;
      });
    } else {
      t = tree_insert(tree, more, base - d);
      more += base - d;
      k -= base - d;
    }
    for (std::uint32_t i = 0; i < k; ++i) queue[n++] = more[i];
    Node h = kRoot;
    for (std::uint32_t i = 0; i < n; ++i)
      h = intern_head({t, h, queue[i], base + i + 1u, i + 1u});
    return h;
  }
  // All the events of `n` as one tree, made once.
  Id whole(Node n) const {
    if (_heads.rows[n].whole == 0) {
      std::array<Event, kTail> tail{};
      const std::uint32_t queued = tail_events(n, tail);
      const Id all = tree_insert(_heads.rows[n].tree, tail.data(), queued);
      _heads.rows[n].whole = all;
    }
    return _heads.rows[n].whole;
  }
  // The queued events of `n`, in order.
  std::uint32_t tail_events(Node n, std::array<Event, kTail>& out) const {
    const std::uint32_t queued = _heads.rows[n].queued;
    for (std::uint32_t k = queued; k-- > 0; n = _heads.rows[n].up)
      out[k] = _heads.rows[n].event;
    return queued;
  }

  // A sequence hash only selects candidates for exact comparison. Its power
  // makes concatenation independent of the trees' shapes, including long runs.
  static constexpr std::uint64_t kBase = 0xD6E8FEB86659FD93ull;
  struct Digest {
    std::uint64_t value = 0, power = 1;
  };
  static Digest repeated(const Event& event, std::uint32_t count) {
    Digest result, block{hash(event), kBase};
    for (; count != 0; count >>= 1) {
      if ((count & 1u) != 0)
        result = {result.value * block.power + block.value,
                  result.power * block.power};
      block = {block.value * block.power + block.value,
               block.power * block.power};
    }
    return result;
  }

  Node intern_head(Head h) const {
    h.digest = (h.queued == 1 ? _trees.rows[h.tree].digest
                              : _heads.rows[h.up].digest) *
                   kBase +
               hash(h.event);
    return _heads.intern(h, [this](const Head& a, const Head& b) {
      if (a.size != b.size || a.queued != b.queued) return false;
      if (a.tree != b.tree && !equal_trees(a.tree, b.tree)) return false;
      Head x = a, y = b;
      for (auto k = a.queued; k != 0; --k) {
        if (!(x.event == y.event)) return false;
        if (x.up == y.up) return true;
        x = _heads.rows[x.up];
        y = _heads.rows[y.up];
      }
      return true;
    });
  }

  // AVL runs: equal events at different positions cannot skew the tree, as
  // they would in a treap whose priority depended only on the event's value.
  std::uint32_t weight(Id t) const { return _trees.rows[t].size; }
  std::uint32_t height(Id t) const { return _trees.rows[t].height; }
  Id make(Id left, const Event& event, std::uint32_t count, Id right) const {
    const Digest run = repeated(event, count);
    return _trees.intern({left, right, event, count,
                          weight(left) + count + weight(right),
                          1u + (height(left) > height(right) ? height(left)
                                                           : height(right)),
                          (_trees.rows[left].digest * run.power + run.value) *
                                  _trees.rows[right].power +
                              _trees.rows[right].digest,
                          _trees.rows[left].power * run.power *
                              _trees.rows[right].power});
  }
  Event tree_event(Id t, std::uint32_t i) const {
    for (;;) {
      const Tree& x = _trees.rows[t];
      const std::uint32_t left = weight(x.left);
      if (i < left) {
        t = x.left;
      } else if (i - left < x.count) {
        return x.event;
      } else {
        i -= left + x.count;
        t = x.right;
      }
    }
  }
  // The `k` events of `e`, no more than `kTail`, as a balanced tree.
  Id tree_of(const Event* e, std::uint32_t k) const {
    std::array<Event, kTail> runs{};
    std::array<std::uint32_t, kTail> counts{};
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < k; ++i)
      if (n != 0 && runs[n - 1u] == e[i]) {
        ++counts[n - 1u];
      } else {
        runs[n] = e[i];
        counts[n++] = 1;
      }
    return balanced(runs.data(), counts.data(), n);
  }
  Id balanced(const Event* runs, const std::uint32_t* counts,
              std::uint32_t n) const {
    if (n == 0) return 0;
    const std::uint32_t mid = n / 2u;
    return make(balanced(runs, counts, mid), runs[mid], counts[mid],
                balanced(runs + mid + 1u, counts + mid + 1u, n - mid - 1u));
  }
  // `a`, the `k` events of `e`, no more than `kTail`, then `b`: the first
  // run of `e` joins the two sides, unless one of them repeats it.
  Id tree_insert(Id a, const Event* e, std::uint32_t k, Id b = 0) const {
    if (k == 0) return tree_concat(a, b);
    if (std::uint64_t{weight(a)} + k + weight(b) > ~std::uint32_t{0})
      throw too_long();
    std::uint32_t run = 1;
    while (run < k && e[run] == e[0]) ++run;
    const Id rest = tree_concat(tree_of(e + run, k - run), b);
    const Id last = end_run(a, true), first = end_run(rest, false);
    if ((last != 0 && _trees.rows[last].event == e[0]) ||
        (first != 0 && _trees.rows[first].event == e[0]))
      return tree_concat(tree_concat(a, tree_of(e, k)), b);
    return join(a, e[0], run, rest);
  }
  // `t` with its events from the `lo`th to before the `hi`th replaced by the
  // `k` events of `e`, no more than `kTail`. Only the path down to where the
  // range parts is copied.
  Id tree_splice(Id t, std::uint32_t lo, std::uint32_t hi, const Event* e,
                 std::uint32_t k) const {
    const Tree x = _trees.rows[t];
    const std::uint32_t left = weight(x.left), right = left + x.count;
    if (hi < left)
      return join(tree_splice(x.left, lo, hi, e, k), x.event, x.count, x.right);
    if (lo > right)
      return join(x.left, x.event, x.count,
                  tree_splice(x.right, lo - right, hi - right, e, k));
    return tree_insert(tree_prefix(t, lo), e, k, tree_drop(t, hi));
  }

  // The last run of `t`, or its first; none where `t` is empty.
  Id end_run(Id t, bool last) const {
    while (t != 0) {
      const Id next = last ? _trees.rows[t].right : _trees.rows[t].left;
      if (next == 0) break;
      t = next;
    }
    return t;
  }

  // Rebalance two subtrees whose heights differ by at most two.
  Id balance(Id left, const Event& event, std::uint32_t count, Id right) const {
    if (height(left) > height(right) + 1u) {
      const Tree x = _trees.rows[left];
      if (height(x.left) >= height(x.right))
        return make(x.left, x.event, x.count,
                    make(x.right, event, count, right));
      const Tree middle = _trees.rows[x.right];
      return make(make(x.left, x.event, x.count, middle.left), middle.event,
                  middle.count, make(middle.right, event, count, right));
    }
    if (height(right) > height(left) + 1u) {
      const Tree x = _trees.rows[right];
      if (height(x.right) >= height(x.left))
        return make(make(left, event, count, x.left), x.event, x.count, x.right);
      const Tree middle = _trees.rows[x.left];
      return make(make(left, event, count, middle.left), middle.event,
                  middle.count, make(middle.right, x.event, x.count, x.right));
    }
    return make(left, event, count, right);
  }
  Id join(Id left, const Event& event, std::uint32_t count, Id right) const {
    if (height(left) > height(right) + 1u) {
      const Tree x = _trees.rows[left];
      return balance(x.left, x.event, x.count,
                     join(x.right, event, count, right));
    }
    if (height(right) > height(left) + 1u) {
      const Tree x = _trees.rows[right];
      return balance(join(left, event, count, x.left), x.event, x.count, x.right);
    }
    return make(left, event, count, right);
  }
  Id tree_prefix(Id t, std::uint32_t d) const {
    if (d == 0) return 0;
    if (d >= weight(t)) return t;
    const Tree x = _trees.rows[t];
    const std::uint32_t left = weight(x.left);
    if (d <= left) return tree_prefix(x.left, d);
    if (d - left < x.count) return join(x.left, x.event, d - left, 0);
    return join(x.left, x.event, x.count,
                tree_prefix(x.right, d - left - x.count));
  }
  // `t` without its first `k` events.
  Id tree_drop(Id t, std::uint32_t k) const {
    if (k == 0) return t;
    if (k >= weight(t)) return 0;
    const Tree x = _trees.rows[t];
    const std::uint32_t left = weight(x.left);
    if (k < left) return join(tree_drop(x.left, k), x.event, x.count, x.right);
    if (k - left < x.count)
      return join(0, x.event, x.count - (k - left), x.right);
    return tree_drop(x.right, k - left - x.count);
  }
  Id tree_concat(Id a, Id b) const {
    if (a == 0) return b;
    if (b == 0) return a;
    if (weight(a) > ~std::uint32_t{0} - weight(b)) throw too_long();
    const Tree x = _trees.rows[end_run(a, true)],
               y = _trees.rows[end_run(b, false)];
    if (!(x.event == y.event))
      return join(tree_prefix(a, weight(a) - x.count), x.event, x.count, b);
    return join(tree_prefix(a, weight(a) - x.count), x.event, x.count + y.count,
                tree_drop(b, y.count));
  }
  void open(std::vector<Piece>& stack, Id t) const {
    const Tree& x = _trees.rows[t];
    if (x.right != 0) stack.push_back({x.right, 0, {}});
    stack.push_back({0, x.count, x.event});
    if (x.left != 0) stack.push_back({x.left, 0, {}});
  }
  // The events of `t` from the `from`th on, while `visit` answers true:
  // false where it stopped them. `visit` walks no other tree.
  template <class Visit>
  bool tree_each(Id t, std::uint32_t from, Visit&& visit) const {
    std::vector<Piece>& stack = _walkEach;
    stack.clear();
    while (t != 0) {
      const Tree& x = _trees.rows[t];
      const std::uint32_t left = weight(x.left);
      if (from < left + x.count) {
        if (x.right != 0) stack.push_back({x.right, 0, {}});
        stack.push_back(
            {0, x.count - (from > left ? from - left : 0), x.event});
        if (from >= left) break;
        t = x.left;
      } else {
        from -= left + x.count;
        t = x.right;
      }
    }
    while (!stack.empty()) {
      const Piece p = stack.back();
      stack.pop_back();
      if (p.tree != 0) {
        open(stack, p.tree);
        continue;
      }
      for (std::uint32_t k = 0; k < p.count; ++k)
        if (!visit(p.event)) return false;
    }
    return true;
  }

  // The pieces of `n` to walk, its first on top.
  void lay(std::vector<Piece>& stack, Node n) const {
    stack.clear();
    const Id tree = _heads.rows[n].tree;
    for (std::uint32_t k = _heads.rows[n].queued; k-- > 0;
         n = _heads.rows[n].up)
      stack.push_back({0, 1, _heads.rows[n].event});
    if (tree != 0) stack.push_back({tree, 0, {}});
  }

  bool equal_trees(Id a, Id b) const {
    if (weight(a) != weight(b) ||
        _trees.rows[a].digest != _trees.rows[b].digest)
      return false;
    auto& x = _walkA;
    auto& y = _walkB;
    x.clear();
    y.clear();
    if (a != 0) x.push_back({a, 0, {}});
    if (b != 0) y.push_back({b, 0, {}});
    while (!x.empty() && !y.empty()) {
      Piece& p = x.back();
      Piece& q = y.back();
      if (p.tree != 0 && p.tree == q.tree) {
        x.pop_back();
        y.pop_back();
      } else if (p.tree != 0 &&
                 (q.tree == 0 || weight(p.tree) >= weight(q.tree))) {
        const Id t = p.tree;
        x.pop_back();
        open(x, t);
      } else if (q.tree != 0) {
        const Id t = q.tree;
        y.pop_back();
        open(y, t);
      } else {
        if (!(p.event == q.event)) return false;
        const auto both = p.count < q.count ? p.count : q.count;
        p.count -= both;
        q.count -= both;
        if (p.count == 0) x.pop_back();
        if (q.count == 0) y.pop_back();
      }
    }
    return x.empty() && y.empty();
  }
  // How many events `a` and `b` share first, and where they part the event
  // each has there: false where one runs into the other. Identical shared
  // subtrees are passed over whole; other shapes are compared by their runs.
  bool diverge(Node a, Node b, std::uint32_t& shared, Event& eventA,
               Event& eventB) const {
    if (_heads.rows[a].tree == _heads.rows[b].tree) {
      // Siblings: one tree, and the few events past it.
      std::array<Event, kTail> ta{}, tb{};
      const std::uint32_t na = tail_events(a, ta), nb = tail_events(b, tb);
      shared = weight(_heads.rows[a].tree);
      for (std::uint32_t k = 0; k < na && k < nb; ++k, ++shared)
        if (!(ta[k] == tb[k])) {
          eventA = ta[k];
          eventB = tb[k];
          return true;
        }
      return false;
    }
    std::vector<Piece>& x = _walkA;
    std::vector<Piece>& y = _walkB;
    lay(x, a);
    lay(y, b);
    shared = 0;
    while (!x.empty() && !y.empty()) {
      Piece& p = x.back();
      Piece& q = y.back();
      if (p.tree != 0 && p.tree == q.tree) {
        shared += weight(p.tree);
        x.pop_back();
        y.pop_back();
      } else if (p.tree != 0 &&
                 (q.tree == 0 || weight(p.tree) >= weight(q.tree))) {
        const Id t = p.tree;
        x.pop_back();
        open(x, t);
      } else if (q.tree != 0) {
        const Id t = q.tree;
        y.pop_back();
        open(y, t);
      } else if (!(p.event == q.event)) {
        eventA = p.event;
        eventB = q.event;
        return true;
      } else {
        const std::uint32_t both = p.count < q.count ? p.count : q.count;
        shared += both;
        p.count -= both;
        q.count -= both;
        if (p.count == 0) x.pop_back();
        if (q.count == 0) y.pop_back();
      }
    }
    return false;
  }

  // Const prefix/LCP queries may name canonical prefixes on demand.
  mutable Pool<Tree> _trees;
  mutable Pool<Head> _heads;
  mutable std::vector<Piece> _walkA, _walkB, _walkEach;
};

}  // namespace cursiv
