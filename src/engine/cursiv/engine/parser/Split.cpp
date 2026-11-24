#include "cursiv/engine/parser/Split.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cursiv::engine {

// A fork names its last choice. Siblings share their prefix in the arena
// and in the path already prepared for replay.
struct Routes {
  struct Pin { std::uint32_t node, parent, depth; };
  std::vector<Pin> pins;
  std::vector<std::uint32_t> path, forks;
  std::unordered_map<std::uint32_t, std::uint32_t> inspected;
  std::uint32_t used = 0;

  std::uint32_t pin(std::uint32_t node) {
    pins.push_back({node, path.empty() ? kNoStack : path.back(), used + 1});
    return static_cast<std::uint32_t>(pins.size() - 1);
  }

  CURSIV_ALWAYS_INLINE void replay(std::uint32_t pin) {
    if (pin == kNoStack && path.empty()) return;
    path.resize(pin == kNoStack ? 0 : pins[pin].depth, kNoStack);
    for (auto i = path.size(); i && path[i - 1] != pin; pin = pins[pin].parent)
      path[--i] = pin;
    used = 0;
    forks.clear();
    if (!inspected.empty()) inspected.clear();
  }
};

bool Explorer::visited(std::uint32_t production, const char* p, Token held) const {
  // A selected route still distinguishes this reading.
  if (options.routes && (options.routes->used < options.routes->path.size() ||
                         !options.routes->inspected.empty())) return false;
  const auto closure = options.closure;
  return !closure->seen.insert({node, options.routes ? invented : fresh, production,
      closure->guards ? *closure->guards : 0, static_cast<TextOffset>(p - text.src),
      held.end ? static_cast<TextOffset>(held.end - text.src) : kNoStack, held.head,
      closure->context, indented}).second;
}

namespace {

std::uint64_t join_hash(std::uint32_t state, std::uint32_t parent, TextOffset end,
                        std::uint64_t held, TextOffset heldEnd) {
  std::uint64_t key = (std::uint64_t{parent} << 32 | state) * 0x9E3779B97F4A7C15ull;
  key ^= held ^ (std::uint64_t{end} << 32 | heldEnd);
  return std::rotl(key * 0xC2B2AE3D27D4EB4Full, 32);
}

}  // namespace

void Cactus::grow_join() {
  _joined = std::vector<std::uint32_t>(2 * _joined.size(), kNoStack);
  _joinBase = 0;
  const std::size_t mask = _joined.size() - 1;
  for (std::uint32_t i = 0; i < _nodes.size(); ++i) {
    const Node& n = _nodes[i];
    std::size_t slot = join_hash(n.state, n.parent, n.second, n.held, n.heldEnd) & mask;
    while (_joined[slot] != kNoStack) slot = (slot + 1) & mask;
    _joined[slot] = i;
  }
}

std::uint64_t Cactus::hash(std::uint32_t node) const {
  const Node& n = _nodes[node - _base];
  return join_hash(n.state, 0, n.second, n.held, n.heldEnd);
}

std::uint32_t Cactus::intern_node(std::uint32_t state, std::uint32_t parent, TextOffset end,
                                  std::uint64_t heldHead, TextOffset heldEnd) {
  const auto count = static_cast<std::uint32_t>(_nodes.size());
  const auto hash = join_hash(state, parent, end, heldHead, heldEnd);
  for (;;) {
    const auto mask = _joined.size() - 1;
    std::size_t slot = hash & mask;
    for (std::uint32_t i; (i = _joined[slot] - _joinBase) < count;) {
      const Node& n = _nodes[i];
      if (n.state == state && n.parent == parent && n.second == end &&
          n.held == heldHead && n.heldEnd == heldEnd) return i + _base;
      slot = (slot + 1) & mask;
    }
    if (count > mask / 2) [[unlikely]] {
      grow_join();
      continue;
    }
    _nodes.push_back({heldHead, heldEnd, state, parent, kNoStack, kNoStack, end});
    _joined[slot] = _joinBase + count;
    return count + _base;
  }
}

Cactus::Pushed Cactus::push(std::uint32_t state, std::uint32_t parent, TextOffset end,
                           std::uint64_t heldHead, TextOffset heldEnd, Derivation made,
                           std::size_t count) {
  const auto node = intern_node(state, parent, end, heldHead, heldEnd);
  auto& head = _nodes[node - _base].first;
  const bool shared = head != kNoStack;
  if (shared) [[unlikely]] {
    const auto children = kids(made, count);
    const auto same = [&](std::uint32_t at) {
      return _ways[at].production == made.production &&
             std::ranges::equal(children, kids(_ways[at], count));
    };
    const auto reuse = [&] {
      _kids.resize(_kids.size() - count);
      return Pushed{node, true};
    };
    if (same(head)) return reuse();
    auto& ways = _alternatives.try_emplace(node, head).first->second;
    if (ways.first != head && same(ways.first)) return reuse();
    std::uint64_t key = made.production;
    for (const auto child : children) key = (key ^ child) * 0x9E3779B97F4A7C15ull;
    const auto [begin, end] = ways.others.equal_range(key);
    for (auto it = begin; it != end; ++it)
      if (same(it->second)) return reuse();
    ++_revision;
    ways.others.emplace(key, static_cast<std::uint32_t>(_ways.size()));
  } else if (made.production == kNoProduction && made.arm == kNoStack) {
    // A shift with no pending choice shares the empty leaf at index zero.
    head = 0;
    return {node, false};
  }
  made.next = head;
  head = static_cast<std::uint32_t>(_ways.size());
  _ways.push_back(made);
  return {node, shared};
}

void Cactus::append(std::vector<std::uint32_t>& out, std::uint32_t choice, bool pending) const {
  const auto begin = out.size();
  while (choice != kNoStack) {
    const Choice& c = _choices[choice];
    out.push_back(c.arm);
    choice = pending ? c.pending : c.path;
  }
  std::reverse(out.begin() + begin, out.end());
}

bool Cactus::same(std::uint32_t a, std::uint32_t b, const Tables& t, const CancellationToken& stop) {
  if (a == b) return true;
  _pairs.clear();
  for (;;) {
    throwIfCancelled(stop);
    if (a < _base || b < _base || key(a) != key(b)) return false;
    const Derivation& u = _ways[ways(a)];
    const Derivation& v = _ways[ways(b)];
    if (u.next != kNoStack || v.next != kNoStack || u.production != v.production ||
        (state(parent(a)) != state(parent(b)) && hops(t, a, u) != hops(t, b, v)))
      return false;
    for (auto i = u.arm, j = v.arm; i != j; i = _choices[i].pending, j = _choices[j].pending)
      if (i == kNoStack || j == kNoStack || _choices[i].arm != _choices[j].arm) return false;
    if (u.production != kNoProduction)
      for (std::uint32_t i = 0; i < t.productionSize[u.production]; ++i)
        if (_kids[u.kids + i] != _kids[v.kids + i])
          _pairs.emplace_back(_kids[u.kids + i], _kids[v.kids + i]);
    if (_pairs.empty()) return true;
    std::tie(a, b) = _pairs.back();
    _pairs.pop_back();
  }
}

void Cactus::alias(std::uint32_t head, std::uint32_t node, const Tables& t, const CancellationToken& stop) {
  if ((_alias += 2) == 0) [[unlikely]] {
    for (auto& mark : _marks) mark.fork = 0;
    _alias = 2;
  }
  // The nodes readings went down through since the last alias.
  _marks.resize(_nodes.size());
  for (; _marked < _kids.size(); ++_marked)
    if (_kids[_marked] >= _base) _marks[_kids[_marked] - _base].passed = true;
  const auto pushed = [&](std::uint32_t n) { return n != kNoStack && n >= _base; };
  for (;;) {
    std::uint32_t& first = _nodes[head - _base].next;
    for (std::uint32_t at = std::exchange(_nodes[node - _base].next, kNoStack); at != kNoStack;) {
      const std::uint32_t after = _others[at].second;
      _others[at].second = first;
      _marks[_others[at].first - _base].head = head;
      first = std::exchange(at, after);
    }
    const std::uint32_t over = parent(head), under = parent(node);
    const bool joined = pushed(over) && pushed(under) && _marks[under - _base].head == over;
    if ((!joined && (!pushed(over) || !pushed(under) || _marks[over - _base].passed ||
                     _nodes[under - _base].next != kNoStack || key(over) != key(under))) ||
        !same(head, node, t, stop)) {
      _others.emplace_back(node, first);
      first = static_cast<std::uint32_t>(_others.size()) - 1;
      _marks[node - _base].head = head;
      return;
    }
    if (joined) return;
    head = over;
    node = under;
  }
}

std::uint32_t Cactus::route(std::uint32_t node, Routes& routes, bool inspect) {
  _marks[node - _base].passed = true;
  if (!routes.inspected.empty())
    if (const auto seen = routes.inspected.find(node); seen != routes.inspected.end()) return seen->second;
  std::uint32_t chosen = node;
  if (routes.used < routes.path.size()) {
    chosen = routes.pins[routes.path[routes.used]].node;
  } else {
    for (std::uint32_t at = _nodes[node - _base].next; at != kNoStack; at = _others[at].second)
      routes.forks.push_back(routes.pin(_others[at].first));
    routes.path.push_back(routes.pin(node));
  }
  ++routes.used;
  if (inspect) routes.inspected.emplace(node, chosen);
  return chosen;
}

bool Cactus::forks(std::uint32_t node) {
  if (_others.empty()) return false;
  _marks.resize(_nodes.size());
  auto at = node;
  bool found = false;
  for (; at != kNoStack && at >= _base; at = parent(at)) {
    const auto known = _marks[at - _base].fork;
    if (known >= _alias) {
      found = (known & 1) != 0;
      break;
    }
    if (_nodes[at - _base].next != kNoStack) {
      found = true;
      break;
    }
  }
  while (node != at) {
    _marks[node - _base].fork = _alias | found;
    node = parent(node);
  }
  return found;
}

[[gnu::flatten]] void Explorer::push(std::uint32_t state, std::uint32_t production, Token held) {
  if (options.routes && options.keepShift) [[likely]] node = made(state, held, production);
  else node = options.keepShift ? stacks.intern(state, node) : stacks.probe(state, node);
  fresh = solid ? 0 : fresh + 1;
  ++text.explored;
}

[[gnu::flatten]] void Explorer::shift(std::uint32_t state, std::uint32_t, Token token) {
  if (token.end == text.src + token.begin(text)) ++invented;
  from = under();
  at = token.end;
  left = 0;
  resume = 2 * state;
  if (options.keepShift)
    node = options.routes ? made(state, token, kNoProduction) : stacks.intern(state, node);
  taken = token;
  ++text.explored;
}

std::uint32_t Explorer::made(std::uint32_t state, Token held, std::uint32_t production) {
  const TextOffset heldEnd = held.end == nullptr
                                 ? Cactus::kNoHeldEnd
                                 : static_cast<TextOffset>(held.end - text.src);
  const auto [made, shared] = stacks.push(state, node, read(), held.head, heldEnd,
                                         {.production = production, .kids = kids, .arm = arm}, left);
  // Only a previously reached forest node can have a duplicate continuation.
  options.closure = shared ? roundClosure : nullptr;
  arm = kNoStack;
  return made;
}

namespace {

// A reading ahead, with the split action it has not derived yet and the
// token it shifted last.
struct Ahead : Reading {
  std::uint32_t arm = kNoStack;
  Token taken{nullptr, 0};
  TextOffset invented = 0;
  // Its last choice, or kNoStack to recover the plan from the graph.
  std::uint32_t path = kNoStack;
  // The text is read to its end; it took a token in the round under way.
  bool done = false;
  bool stepped = false;

  struct Trace {
    std::uint32_t node, arm, path;
  };
  Trace trace() const { return {node, arm, path}; }
};

// A derivation and the level reached in its unit chain. An original frame has no way.
struct Cursor {
  std::uint32_t node = kNoStack;
  std::uint32_t way = kNoStack;
  std::uint32_t level = 0;
  std::uint32_t list = 0;
  bool unit(const Tables& t) const { return list && level < t.unitWord[list]; }
  Cursor inner() const { return {node, way, level + 1, list}; }
  bool operator==(const Cursor&) const = default;
};

// The grammar's order among the ways a node was made: the decisions of two
// ways are compared as a reading from the top down meets them, and the first
// that differs tells.
class Election {
 public:
  Election(const Machine& text, Cactus& stacks) : t(text.t), stacks(stacks), stop(text.stop) {}

  void clear() {
    _need = kNoStack;
    _open.clear();
    _folds.clear();
  }

  // The actions of the splits met by the reading that stands on `top`, as
  // the ways elected for its nodes took them.
  void plan(std::uint32_t top, std::vector<std::uint32_t>& out) {
    for (std::uint32_t node = top; node != kNoStack && node >= stacks.base();
         node = stacks.parent(node))
      _open.push_back({node, false});
    while (!_open.empty()) {
      throwIfCancelled(stop);
      const auto [node, closed] = _open.back();
      _open.pop_back();
      if (closed) {
        stacks.append(out, node, true);
        continue;
      }
      const auto first = stacks.ways(node);
      const Derivation& made = stacks.way(stacks.way(first).next == kNoStack ? first : elected(node));
      // Its children first, the leftmost first of them, then its own action.
      if (made.arm != kNoStack) _open.push_back({made.arm, true});
      if (made.production != kNoProduction)
        for (const std::uint32_t kid : stacks.kids(made, t.productionSize[made.production]))
          if (kid >= stacks.base()) _open.push_back({kid, false});
    }
  }

  // Compare the symbols reached over the same parent, in the state above them.
  bool before(std::uint32_t a, std::uint32_t b) {
    auto over = stacks.state(a);
    while (stacks.parent(a) != stacks.parent(b)) {
      over = stacks.state(a);
      a = stacks.parent(a);
      b = stacks.parent(b);
      if (a == kNoStack || b == kNoStack || a < stacks.base() || b < stacks.base() ||
          stacks.state(a) != stacks.state(b)) return false;
    }
    _remaining.push_back({{a}, {b}, false, over});
    for (;;) {
      const int rank = order(0);
      if (_need == kNoStack) return rank < 0;
      elected(_need);
    }
  }

 private:
  // The way of `root` the grammar prefers, the only one the node keeps. The
  // nodes its order waits for are elected first.
  std::uint32_t elected(std::uint32_t root) {
    const auto wait = [&](std::uint32_t node) {
      const auto best = stacks.ways(node);
      _waiting.push_back({node, best, stacks.way(best).next, _remaining.size()});
    };
    _waiting.clear();
    wait(root);
    while (!_waiting.empty()) {
      throwIfCancelled(stop);
      auto& [node, best, other, base] = _waiting.back();
      if (other == kNoStack) {
        stacks.settle(node, best);
        _waiting.pop_back();
        continue;
      }
      if (_remaining.size() == base) _remaining.push_back({cursor(node, other), cursor(node, best)});
      const int rank = order(base);
      if (_need != kNoStack) {
        wait(_need);
        continue;
      }
      if (rank < 0) best = other;
      other = stacks.way(other).next;
    }
    _need = kNoStack;
    return stacks.ways(root);
  }

  // The elected way of `node`, from its outermost production. A node with
  // several ways still is asked for.
  Cursor cursor(std::uint32_t node, std::uint32_t way = kNoStack) {
    if (way == kNoStack) {
      way = stacks.ways(node);
      if (way != kNoStack && stacks.way(way).next != kNoStack) _need = node;
    }
    return {node, way, 0, way == kNoStack ? 0 : stacks.hops(t, node, stacks.way(way))};
  }
  [[nodiscard]] std::uint32_t production(const Cursor& c) const {
    return c.unit(t) ? t.unitProduction(c.list, c.level) : stacks.way(c.way).production;
  }
  // The left operand of the turn `c` stands on; none for a base or a frame.
  Cursor lead(const Cursor& c) {
    if (c.way == kNoStack) return {};
    const std::uint32_t p = production(c);
    if (p == kNoProduction || (t.productionOrder[p] & 1) == 0) return {};
    throwIfCancelled(stop);
    if (c.unit(t)) return c.inner();
    return cursor(stacks.kids(stacks.way(c.way), t.productionSize[p]).back());
  }
  // How many productions the fold of a node's one way holds, 0 while it is
  // not counted.
  std::uint32_t& fold(std::uint32_t node) {
    const std::size_t at = node - stacks.base();
    if (at >= _folds.size()) _folds.resize(at + 1);
    auto& [revision, count] = _folds[at];
    if (revision != stacks.revision()) {
      revision = stacks.revision();
      count = 0;
    }
    return count;
  }
  // How many productions the fold of `c` holds, its base counted. A list is
  // counted from the list it goes on, which was counted before.
  std::size_t turns(Cursor c) {
    std::size_t count = 0;
    _counted.clear();
    for (; c.node != kNoStack && _need == kNoStack; c = lead(c)) {
      if (c.level == 0 && c.node >= stacks.base() && stacks.way(c.way).next == kNoStack &&
          stacks.ways(c.node) == c.way) {
        if (const std::uint32_t known = fold(c.node); known != 0) {
          count += known;
          break;
        }
        _counted.emplace_back(c.node, count);
      }
      ++count;
    }
    if (_need == kNoStack)
      for (const auto& [node, before] : _counted) {
        throwIfCancelled(stop);
        fold(node) = static_cast<std::uint32_t>(count - before);
      }
    return count;
  }
  // Queue children right to left so the leftmost decision is compared first.
  void children(const Cursor& x, const Cursor& y, bool skipFirst) {
    const bool unitX = x.unit(t), unitY = y.unit(t);
    const Derivation& dx = stacks.way(x.way);
    const Derivation& dy = stacks.way(y.way);
    if (unitX || unitY) {
      if (!skipFirst) {
        const Cursor cx = unitX ? x.inner() : Cursor{stacks.kids(dx, 1).front()};
        const Cursor cy = unitY ? y.inner() : Cursor{stacks.kids(dy, 1).front()};
        _remaining.push_back({cx, cy});
      }
      return;
    }
    const std::size_t size = t.productionSize[dx.production];
    const auto kx = stacks.kids(dx, size), ky = stacks.kids(dy, size);
    for (std::size_t i = 0; i < size - skipFirst; ++i)
      if (kx[i] != ky[i]) _remaining.push_back({{kx[i]}, {ky[i]}});
  }

  // Where a fold stops and another goes on with the turn `past`, each to its
  // own end under a node of state `over`: 1 where the one that goes on is
  // preferred, -1 where the other is with what it reads on, which the state
  // tells the beginning of; 0 where nothing tells yet.
  int ahead(const Cursor& past, std::uint32_t over) {
    const std::uint32_t turn = t.turnOf[over];
    if (turn == kNoProduction) return 1;
    const std::uint32_t mine = production(past);
    if (turn == kSomeProduction || past.unit(t)) return 0;
    if (mine != turn) return t.productionOrder[mine] < t.productionOrder[turn] ? 1 : -1;
    const std::uint32_t via = t.turnVia[over];
    const std::size_t size = t.productionSize[mine];
    if (via == kNoProduction || size < 2) return 0;
    const Cursor second = cursor(stacks.kids(stacks.way(past.way), size)[size - 2]);
    if (_need != kNoStack || second.way == kNoStack) return 0;
    const std::uint32_t read = production(second);
    if (read == kNoProduction || read == via || (t.productionOrder[read] & 3) != 0) return 0;
    return t.productionOrder[read] < t.productionOrder[via] ? 1 : -1;
  }

  // Compare the pending siblings above this ballot's base, resuming at the
  // first unresolved child after its own ballot elects a way.
  int order(std::size_t base) {
    _need = kNoStack;
    while (_remaining.size() > base) {
      throwIfCancelled(stop);
      const auto item = _remaining.back();
      _remaining.pop_back();
      const auto pending = _remaining.size();
      const auto x = item.left.way == kNoStack ? cursor(item.left.node) : item.left;
      const auto y = item.right.way == kNoStack ? cursor(item.right.node) : item.right;
      const int rank = _need == kNoStack ? compare(x, y, item.over, item.turn) : 0;
      if (_need != kNoStack) {
        _remaining.resize(pending);
        _remaining.push_back(item);
        return 0;
      }
      if (rank != 0) {
        _remaining.resize(base);
        return rank;
      }
    }
    return 0;
  }

  int compare(Cursor x, Cursor y, std::uint32_t over, bool folded) {
    for (;;) {
      const bool frame = x.way == kNoStack || y.way == kNoStack;
      const std::uint32_t px = frame ? kNoProduction : production(x);
      const std::uint32_t py = frame ? kNoProduction : production(y);
      if (!folded && (frame || (px != kNoProduction && (t.productionOrder[px] & 3) != 0))) {
        // A left recursion reads as its fold, told apart from the base on:
        // one that goes on where the other stops is preferred. Two folds of
        // one text end together, else the one that ends further goes on.
        if (over == kNoStack && !frame && (t.productionOrder[px] & 2) != 0) {
          const TextOffset endX = stacks.end(x.node);
          const TextOffset endY = stacks.end(y.node);
          if (endX != endY) return endX > endY ? -1 : 1;
        }
        const std::size_t countX = turns(x), countY = turns(y);
        if (_need != kNoStack) return 0;
        Cursor past{};
        for (std::size_t i = countY; i < countX; ++i) past = std::exchange(x, lead(x));
        for (std::size_t i = countX; i < countY; ++i) past = std::exchange(y, lead(y));
        if (x == y && countX == countY) return 0;
        if (x == y)
          return (countX > countY ? -1 : 1) *
                 (over == kNoStack ? 1 : ahead(past, over));
        for (Cursor a = lead(x), b = lead(y); !(a == b); a = lead(x), b = lead(y)) {
          _remaining.push_back({x, y, true});
          x = a;
          y = b;
        }
        folded = true;
        continue;
      }
      if (px != kNoProduction && py != kNoProduction) {
        if (px != py) return t.productionOrder[px] < t.productionOrder[py] ? -1 : 1;
        children(x, y, folded && (t.productionOrder[px] & 1));
      }
      return 0;
    }
  }

  const Tables& t;
  Cactus& stacks;
  const CancellationToken& stop;
  // The node an order waits for, the nodes being elected, and the nodes
  // whose actions a plan still lists, each with whether its children are
  // listed.
  std::uint32_t _need = kNoStack;
  struct Ballot { std::uint32_t node, best, other; std::size_t base; };
  std::vector<Ballot> _waiting;
  std::vector<std::pair<std::uint32_t, bool>> _open;
  // Later siblings or outer turns, in reverse reading order.
  struct Comparison { Cursor left, right; bool turn = false; std::uint32_t over = kNoStack; };
  std::vector<Comparison> _remaining;
  // Per node, the count of its fold; and the nodes a count walked.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _folds;
  std::vector<std::pair<std::uint32_t, std::size_t>> _counted;
};

// Of two tokens read at one place, the longer, then the one of the lower
// rank.
bool beats(const Machine& text, Token u, Token v) {
  return u.end > v.end ||
         (u.end == v.end && u.terminal() != v.terminal() && text.rank(u) < text.rank(v));
}

// Restrict a split to ordinary actions or to the lexical fallback they ruled out.
bool eligible(Machine& text, Cactus& stacks, const Reading& r, std::uint32_t& begin,
              std::uint32_t& end, std::uint32_t retries) {
  const auto boundary = end - retries;
  const bool taken = takes(text, stacks, r, begin, boundary - begin);
  if (taken) end = boundary;
  else begin = boundary;
  return taken;
}

// Where a reading of a round stands: a hash of its node's key and its
// indentation, its node, and its place in the round.
struct Alike {
  std::uint64_t hash;
  std::uint32_t node;
  std::uint32_t index;
  auto operator<=>(const Alike&) const = default;
};

}  // namespace

struct Machine::Lookahead {
  explicit Lookahead(const Machine& text) : election(text, stacks) {}

  Cactus stacks;
  Election election;
  Explorer::Closure closure;
  std::vector<Ahead> live, next;
  Forks<Ahead, std::uint32_t> forks;
  // Per place a token was shifted from in a round, the token the lexer
  // prefers; the ways down of a step; the readings of a round that may
  // stand alike.
  std::vector<Token> leaders;
  Routes routes;
  std::vector<Alike> alike;
  // The readings that fell in each of the last real token positions; the
  // nodes they stood on still to walk, and those walked.
  std::array<std::vector<Ahead::Trace>, kValidateShifts> falls;
  std::vector<std::uint32_t> fellPlan;
  std::vector<std::uint32_t> stood;
  std::unordered_set<std::uint32_t> walked;
};

void resolve(Machine& text, std::span<const Frame> stack, const char* at, Token tok,
                      Indented indented, std::uint32_t first, std::uint32_t count,
                      std::uint32_t rereads, std::vector<std::uint32_t>& plan) {
  const Tables& t = text.t;
  if (text.lookahead == nullptr) text.lookahead = new Machine::Lookahead(text);
  auto& [stacks, election, closure, live, next, forks, leaders, routes, alike, falls, fellPlan,
         stood, walked] = *text.lookahead;
  stacks.clear(stack);
  const auto top = static_cast<std::uint32_t>(stack.size()) - 1;
  if (rereads) {
    auto end = first + count;
    const bool taken = eligible(text, stacks, {top, 0, at, tok, indented}, first, end, rereads);
    count = end - first;
    if (taken && count == 1) {
      plan.assign(1, first);
      return;
    }
  }
  live.clear();
  forks.open.clear();
  forks.active.clear();
  election.clear();
  for (std::uint32_t arm = 0; arm < count; ++arm) {
    const auto path = stacks.choose(first + arm, kNoStack, kNoStack);
    live.push_back({{top, first + arm, at, tok, indented}, path, {nullptr, 0}, 0, path});
  }
  // The actions of the splits a reading met, as it took them: over nodes
  // each made one way and by itself, those it took.
  const auto plan_of = [&](const Ahead::Trace& r, std::vector<std::uint32_t>& out) {
    if (r.path == kNoStack || stacks.revision()) {
      election.plan(r.node, out);
      stacks.append(out, r.arm, true);
      return;
    }
    stacks.append(out, r.path, false);
  };
  Ahead elected{};
  // The real tokens read: a reading that fell stays one a repair may take
  // until kValidateShifts more are read.
  std::size_t round = 0;
  for (auto& group : falls) group.clear();
  for (;;) {
    // The readings furthest back take a token each.
    const char* back = nullptr;
    for (const Ahead& r : live)
      if (!r.done && (back == nullptr || r.at < back)) back = r.at;
    if (!closure.seen.empty()) closure.seen.clear();
    next.clear();
    Token firstToken{nullptr, 0};
    bool different = false;
    bool fell = false;
    for (const Ahead& from : live) {
      if (from.done || from.at != back) {
        next.push_back(from);
        next.back().stepped = false;
        continue;
      }
      auto& open = forks.open;
      Forks<Ahead, std::uint32_t>::Entry fork{from, kNoStack};
      routes.pins.clear();
      for (;;) {
        throwIfCancelled(text.stop);
        const auto& start = fork.reading;
        routes.replay(fork.context);
        Explorer x(text, stacks, start, {.routes = &routes, .closure = forks.active.empty() ? &closure : nullptr});
        x.arm = start.arm;
        x.invented = start.invented;
        Exit exit = x.step();
        // A reading that goes down another way where a node stands for others
        // reads the step again from where it began.
        for (const auto route : routes.forks) {
          open.push_back({start, route});
          open.back().reading.path = kNoStack;
        }
        Ahead r{x.reading(), x.arm, x.taken, x.invented, start.path};
        std::uint32_t begin = x.arms, end = begin + x.armCount;
        const bool fallback = exit == Exit::split && x.rereads &&
                              !eligible(text, stacks, x, begin, end, x.rereads);
        if (fallback && !forks.enter(r, x.arms)) exit = Exit::joined;
        // A forced reread owns only the forced prefix up to the next decision.
        if (count == 1 && (exit == Exit::joined || !fallback || end - begin != 1)) {
          if (exit != Exit::joined) plan_of(r.trace(), plan);
          return;
        }
        if (exit == Exit::refused) {
          // Of the readings refused, one that stands at a caret first, then
          // the one that read furthest.
          const bool caret = r.tok.terminal() == text.ended;
          const bool before = elected.tok.terminal() == text.ended;
          if (!fell || (caret && !before) || (caret == before && r.at > elected.at)) elected = r;
          falls[round].push_back(r.trace());
          fell = true;
        } else if (exit == Exit::split) {
          // A split met on the way: a reading goes on from each action.
          if (stacks.revision()) r.path = kNoStack;
          for (std::uint32_t arm = end; arm-- > begin;) {
            Ahead& branch = open.emplace_back(r, kNoStack).reading;
            branch.resume = arm;
            branch.arm = stacks.choose(arm, r.path, r.arm);
            if (r.path != kNoStack) branch.path = branch.arm;
          }
        } else if (exit != Exit::joined) {
          r.done = exit == Exit::accepted;
          r.stepped = !r.done && r.taken.end != text.src + r.taken.begin(text);
          next.push_back(r);
          if (r.stepped) {
            if (firstToken.end == nullptr) firstToken = r.taken;
            different |= r.taken.head != firstToken.head || r.taken.end != firstToken.end;
          }
        }
        if (!forks.pending()) break;
        fork = open.back();
        open.pop_back();
      }
    }
    live.swap(next);
    // Only readings that shifted competing tokens need lexical election.
    if (different) {
      leaders.clear();
      for (const Ahead& r : live) {
        if (!r.stepped) continue;
        if (leaders.empty() || leaders.back().begin(text) != r.taken.begin(text))
          leaders.push_back(r.taken);
        else if (beats(text, r.taken, leaders.back()))
          leaders.back() = r.taken;
      }
      std::ranges::sort(leaders, [&](Token a, Token b) {
        return a.begin(text) != b.begin(text) ? a.begin(text) < b.begin(text) : beats(text, a, b);
      });
      std::erase_if(live, [&](const Ahead& r) {
        if (!r.stepped) return false;
        const auto best = std::ranges::lower_bound(leaders, r.taken.begin(text), {}, [&](Token token) { return token.begin(text); });
        return beats(text, *best, r.taken);
      });
    }
    // Readings can share a continuation only after a shift to the same
    // state. Most rounds have no candidate pair.
    std::uint64_t seen = 0;
    bool twice = false;
    unsigned accepts = 0;
    bool dropped = false;
    for (const Ahead& r : live) {
      if (r.done) {
        ++accepts;
      } else {
        const auto bit = std::uint64_t{1} << ((r.resume / 2) & 63);
        twice |= (seen & bit) != 0;
        seen |= bit;
      }
    }
    if (twice) {
      alike.clear();
      for (std::uint32_t i = 0; i < live.size(); ++i) {
        const Ahead& r = live[i];
        if (!r.done)
          alike.push_back({stacks.hash(r.node) ^ (std::uint64_t{r.indented.level} << 40) ^
                               r.indented.depth, r.node, i});
      }
      const auto key = [&](const Alike& a) {
        const Indented& in = live[a.index].indented;
        return std::tuple_cat(stacks.key(a.node), std::tuple(in.level, in.depth));
      };
      if (!std::ranges::is_sorted(alike)) std::ranges::sort(alike);
      std::uint32_t head = kNoStack;
      for (std::size_t k = 0; k < alike.size(); ++k) {
        if (k == 0 || alike[k - 1].hash != alike[k].hash || key(alike[k - 1]) != key(alike[k])) {
          head = alike[k].node;
          continue;
        }
        if (alike[k - 1].node != alike[k].node) stacks.alias(head, alike[k].node, t, text.stop);
        live[alike[k].index].node = kNoStack;
        dropped = true;
      }
    }
    if (accepts > 1) {
      Ahead* first = nullptr;
      for (Ahead& r : live) {
        if (!r.done) continue;
        if (first == nullptr) {
          first = &r;
        } else {
          if (election.before(r.node, first->node)) *first = r;
          r.node = kNoStack;
        }
      }
      dropped = true;
    }
    if (dropped) std::erase_if(live, [](const Ahead& r) { return r.node == kNoStack; });
    if (live.empty()) break;
    elected = live.front();
    if (elected.stepped && elected.taken.terminal() != t.indentTerminal) {
      round = (round + 1) % falls.size();
      falls[round].clear();
    }
    // One reading is left once none that fell may still be taken, and no
    // node under it stands for others.
    if (live.size() == 1) {
      const bool clear = std::ranges::all_of(falls, &std::vector<Ahead::Trace>::empty);
      if (elected.done || (clear && !stacks.forks(elected.node))) break;
    }
  }
  plan_of(elected.trace(), plan);
  if (live.empty()) {
    if (text.ended != kEndOfInput && elected.tok.terminal() == text.ended) {
      text.forked = std::min(text.forked, tok.begin(text));
    } else {
      // Leave recovery at the last choice shared by the recently refused
      // readings, and by the readings each stood for.
      const auto shared = [&] {
        plan.erase(std::ranges::mismatch(plan, fellPlan).in1, plan.end());
      };
      walked.clear();
      for (std::size_t i = 1; i <= falls.size(); ++i)
        for (const Ahead::Trace& fallen : falls[(round + i) % falls.size()]) {
          if (plan.empty()) break;
          fellPlan.clear();
          plan_of(fallen, fellPlan);
          shared();
          stood.assign(1, fallen.node);
          while (!stood.empty() && !plan.empty()) {
            std::uint32_t node = stood.back();
            stood.pop_back();
            for (; node != kNoStack && node >= stacks.base() && walked.insert(node).second;
                 node = stacks.parent(node))
              stacks.others(node, [&](std::uint32_t other) {
                fellPlan.clear();
                election.plan(other, fellPlan);
                shared();
                stood.push_back(other);
              });
          }
        }
    }
  }
}

// What the conditions of a text found: per stack of states and place, whether
// the text read on from there makes its condition hold. It serves one end of
// text.
struct Machine::Conditions {
  // A stack of states at a place of the text and of the indentation.
  struct Place {
    std::uint64_t at;
    Indented indented;
    TextOffset from;
    std::uint64_t token;
    bool operator==(const Place&) const = default;
  };
  struct Hash {
    std::size_t operator()(const Place& p) const {
      return join_hash(static_cast<std::uint32_t>(p.at >> 32), p.indented.level,
                       static_cast<TextOffset>(p.at), p.token ^ (std::uint64_t{p.indented.depth} << 48),
                       p.from);
    }
  };
  Cactus stacks;
  std::unordered_map<Place, Machine::PredicateResult, Hash> found;
  // While conditions are read: the one a reading met that is not known yet,
  // and where that reading stood in the indentation.
  bool reading = false;
  std::uint32_t wantedRoot = 0;
  const char* wanted = nullptr;
  Token wantedToken{nullptr, 0};
  Indented wantedAt{};
};

void Machine::read_to(const char* to, std::uint16_t ending) {
  if (end != to) mark = stop.stop_possible() ? src : to;
  end = to;
  ended = ending;
  blankLine = nullptr;
  stretches.erase(stretches.begin(), stretches.lower_bound(std::uint64_t{kNoToken} << 48));
  settled.clear();
  if (conditions != nullptr) {
    conditions->stacks.clear();
    conditions->found.clear();
  }
}

Machine::~Machine() {
  delete conditions;
  delete lookahead;
}

// The tokens a condition reads before it asks what was found of the rest: a
// condition is short.
constexpr std::uint32_t kAskedAfter = 3;

bool Machine::reads(std::uint32_t state, const char* from, Token tok, Indented indented) {
  if (conditions == nullptr) conditions = new Conditions;
  Conditions& kept = *conditions;
  Cactus& stacks = kept.stacks;
  const auto place = [&](std::uint32_t node, const char* at, Indented level,
                         const char* start = nullptr, std::uint64_t token = 0) {
    return Conditions::Place{std::uint64_t{node} << 32 | static_cast<TextOffset>(at - src), level,
                             start ? static_cast<TextOffset>(start - src) : kNoStack, token};
  };
  const auto initialRoot = stacks.intern(state, kNoStack);
  const auto initial = place(initialRoot, tok.end, indented, from, tok.head);
  if (const auto known = kept.found.find(initial); known != kept.found.end()) {
    observe_end(known->second.atCut);
    return known->second.value;
  }
  // A condition a reading meets is read before it, not within it: the
  // reading is read again once the condition is known.
  if (kept.reading) {
    if (kept.wanted == nullptr) {
      kept.wanted = from;
      kept.wantedToken = tok;
      kept.wantedRoot = initialRoot;
      kept.wantedAt = indented;
    }
    return false;
  }
  // Every way the condition's states read the text is tried, the last met
  // first. A reading that stands where the same states stood before reads
  // nothing again; the places it went through since its last split are told
  // what it came to. A condition holds nowhere it is asked while it is read.
  struct Open {
    Reading reading;
    std::uint32_t trail;
    std::uint32_t tokens;
  };
  struct Asked {
    Conditions::Place place;
    std::vector<Open> open;
    std::uint64_t observedFrom;
    std::set<SplitSite> seen{};
  };
  struct Suffix {
    Conditions::Place place;
    std::uint32_t prior;
    std::uint64_t observedFrom;
  };
  std::vector<Suffix> trail;
  const auto tell = [&](std::uint32_t link, bool holds) {
    for (; link != kNoStack; link = trail[link].prior) {
      throwIfCancelled(stop);
      kept.found[trail[link].place] = {holds, endReads != trail[link].observedFrom};
    }
  };
  std::vector<Asked> asked;
  // The condition's first token is the token that comes, as its states read
  // that text; read otherwise, the token itself, which they may refuse.
  const auto ask = [&](std::uint32_t root, const char* p, Token next, Indented level) {
    const auto s = stacks.state(root);
    const auto before = endReads;
    Token first = next;
    if (const auto mode = t.stateMode[s]; mode != kNoToken) {
      const Token read = observed(t.modes[mode].read(*this, src + next.begin(*this), mode));
      if (read.begin(*this) != next.begin(*this) || read.end == next.end) first = read;
    }
    asked.push_back({place(root, next.end, level, p, next.head),
                     {{{root, 2 * s + 1, p, first, level}, kNoStack, 0}}, before});
    kept.found[asked.back().place] = {};
  };
  ask(initialRoot, from, tok, indented);
  kept.reading = true;
  for (;;) {
    Asked& a = asked.back();
    bool holds = false;
    throwIfCancelled(stop);
    if (!a.open.empty()) {
      const Open o = a.open.back();
      a.open.pop_back();
      Reading r = o.reading;
      std::uint32_t link = o.trail;
      // After a shift, what follows is told by its states and its place.
      std::optional<bool> told;
      if (o.tokens >= kAskedAfter && r.resume == 2 * stacks.state(r.node)) {
        const Conditions::Place at = place(r.node, r.at, r.indented);
        if (const auto known = kept.found.find(at); known != kept.found.end()) {
          observe_end(known->second.atCut);
          told = known->second.value;
        } else {
          trail.push_back({at, link, endReads});
          link = static_cast<std::uint32_t>(trail.size() - 1);
        }
      }
      if (!told) {
        Explorer x(*this, stacks, r);
        const Exit exit = x.step();
        observed(exit == Exit::shifted ? x.taken : x.tok);
        auto first = x.arms, end = first + x.armCount;
        if (exit == Exit::split && x.rereads && kept.wanted == nullptr)
          eligible(*this, stacks, x, first, end, x.rereads);
        if (kept.wanted != nullptr) {
          a.open.push_back(o);
          ask(kept.wantedRoot, std::exchange(kept.wanted, nullptr), kept.wantedToken,
              kept.wantedAt);
          continue;
        }
        if (exit == Exit::split && !a.seen.insert(x.site(x.arms)).second) continue;
        if (exit == Exit::accepted || exit == Exit::refused) {
          told = exit == Exit::accepted;
        } else {
          r = x.reading();
          if (exit == Exit::shifted) a.open.push_back({r, link, o.tokens + 1});
          for (auto arm = end; arm-- > first;) {
            r.resume = arm;
            a.open.push_back({r, kNoStack, o.tokens});
          }
          continue;
        }
      }
      tell(link, *told);
      if (!*told) continue;
      holds = true;
    }
    // The condition is known: the reading that waits for it goes on.
    kept.found[a.place] = {holds, endReads != a.observedFrom};
    asked.pop_back();
    if (asked.empty()) {
      kept.reading = false;
      return holds;
    }
  }
}

Prediction predict_prefix(Machine& m, std::span<const PrefixArm> arms, const PrefixPath* path) {
  const char* at = m.tok.end;
  if (path) {
    if (path->stack.size() > static_cast<std::size_t>(m.top - m.frames) ||
        !std::equal(path->stack.begin(), path->stack.end(), m.top - path->stack.size(),
                    [](std::uint32_t state, Frame f) { return state == f.state; })) return {};
    for (const auto& [mode, terminal] : path->tokens) {
      const Token tok = m.t.modes[mode].read(m, at, mode);
      if (tok.hasGroup() || tok.terminal() != terminal || tok.end <= m.src + tok.begin(m)) return {};
      at = tok.end;
    }
  }
  // Only the arms of tied terminals tell a token several terminals read.
  if (m.tok.hasGroup() && arms.front().terminal == kNoToken) return {};
  std::uint32_t chosen = kNoStack;
  Token best{nullptr, 0};
  bool sole = !path;
  for (std::size_t i = 0; i < arms.size(); ++i) {
    const Token next = m.t.modes[arms[i].mode].read(m, at, arms[i].mode);
    if (next.hasGroup()) return {};
    const unsigned shifts = arms[i].shifts[next.terminal()];
    if (shifts == 0) {
      sole &= next.terminal() == m.t.terminalCount;
      continue;
    }
    if (arms[i].terminal != kNoToken && !m.takes_tied(m.tok, arms[i].terminal)) continue;
    if (shifts == 1 || next.end <= m.src + next.begin(m) ||
        (best.end && next.begin(m) != best.begin(m))) return {};
    sole &= !best.end;
    if (!best.end || next.end > best.end) {
      best = next;
      chosen = static_cast<std::uint32_t>(i);
    } else if (next.end == best.end) {
      chosen = kNoStack;
    }
  }
  if (chosen == kNoStack) return {};
  return {m.arms + chosen, sole ? static_cast<TextOffset>(best.end - m.src) : 0};
}

Prediction predict_cell(Machine& m, const PrefixTable& table) {
  const std::size_t cell = m.arms - table.first;
  if (cell >= table.wayOf.size() || table.wayOf[cell] == 0) return {};
  const PrefixWay& way = table.ways[table.wayOf[cell] - 1];
  if (way.depth != 0 && static_cast<std::size_t>(m.top - m.frames) <= way.depth) return {};
  std::uint32_t plan = way.fallback;
  if (way.count != 0) {
    const std::span states{table.states + way.contexts, way.count};
    const std::uint32_t state = m.top[-static_cast<std::ptrdiff_t>(way.depth) - 1].state;
    const auto found = std::ranges::lower_bound(states, state);
    if (found != states.end() && *found == state)
      plan = table.plans[way.contexts + (found - states.begin())];
  }
  if (plan == kNoPlan) return {};
  if ((plan & kPlanArm) != 0) return {m.arms + (plan & ~kPlanArm)};
  const PrefixPath* path = nullptr;
  if ((plan & kPlanRoute) != 0) {
    const PrefixRoute& route = table.routes[plan & ~kPlanRoute];
    plan = route.plan;
    path = route.path;
  }
  return predict_prefix(m, {table.arms + table.planAt[plan], table.arms + table.planAt[plan + 1]}, path);
}

std::uint32_t arm(Machine& m, Prediction* prediction) {
  // A plan made for another split says nothing of this one.
  if (m.planned != m.plan.size() &&
      (m.plan[m.planned] < m.arms || m.plan[m.planned] >= m.arms + m.armCount))
    m.planned = m.plan.size();
  if (m.planned == m.plan.size()) {
    m.plan.clear();
    m.planned = 0;
    if (prediction && m.t.predict) {
      *prediction = m.t.predict(m);
      if (prediction->arm != kNoStack) return prediction->arm;
    }
    resolve(m, {m.frames, m.top}, m.at, m.tok, m.indented, m.arms, m.armCount, m.rereads, m.plan);
    if (m.plan.empty()) return kNoStack;
  }
  return m.plan[m.planned++];
}

bool takes(Machine& text, Cactus& stacks, const Reading& from,
           std::uint32_t first, std::uint32_t count, std::uint16_t terminal, Token* shifted) {
  struct Open {
    Reading reading;
    std::uint32_t arm, end, pin = kNoStack;
  };
  struct Query {
    Token token;
    std::uint16_t terminal;
    std::vector<Open> open;
    std::set<SplitSite> seen{};
    Token best{};
    bool matched = false;
  };
  struct Pending {
    Query parent;
    Explorer split;
    std::map<SplitSite, std::optional<bool>>::iterator answer;
  };
  Query q{from.tok, terminal, {{from, first, first + count}}};
  std::vector<Pending> pending;
  std::map<SplitSite, std::optional<bool>> answers;
  Routes routes;
  const auto eligible = [&](const Explorer& split, bool taken) {
    const auto boundary = split.arms + split.armCount - split.rereads;
    q.open.push_back({split.reading(), taken ? split.arms : boundary,
                     taken ? boundary : split.arms + split.armCount});
  };
  for (;;) {
    if (q.open.empty() || (q.matched && q.terminal == kNoToken)) {
      const bool taken = q.matched;
      if (pending.empty()) {
        if (taken && shifted) *shifted = q.best;
        return taken;
      }
      auto& parent = pending.back();
      parent.answer->second = taken;
      q = std::move(parent.parent);
      eligible(parent.split, taken);
      pending.pop_back();
      continue;
    }
    const Open step = q.open.back();
    if (++q.open.back().arm == step.end) q.open.pop_back();
    throwIfCancelled(text.stop);
    Reading r = step.reading;
    r.resume = step.arm;
    routes.replay(step.pin);
    Explorer x(text, stacks, r, {.routes = &routes, .keepShift = false});
    const Exit exit = x.step();
    for (const auto pin : routes.forks)
      q.open.push_back({r, step.arm, step.arm + 1, pin});
    const auto same = [&](Token tok) {
      return tok.end == q.token.end && tok.begin(text) == q.token.begin(text) &&
             ((tok.head ^ q.token.head) & (kReplaced | kMixedIndent)) == 0 &&
             ((tok.terminal() == q.token.terminal() && ((tok.head ^ q.token.head) & kLiteralBranch) == 0) ||
              ((tok.head & kTold) && !(q.token.head & kTold) && text.rank(tok) == text.rank(q.token)));
    };
    if (exit == Exit::accepted && q.terminal == kNoToken) q.matched = true;
    if (exit == Exit::shifted && x.taken.begin(text) == q.token.begin(text)) {
      if (q.terminal == kNoToken) {
        q.matched = same(x.taken);
      } else {
        if (!q.best.end || beats(text, x.taken, q.best)) {
          q.best = x.taken;
          q.matched = false;
        }
        if (!beats(text, q.best, x.taken) && x.taken.terminal() == q.terminal) {
          q.best = x.taken;
          q.matched = true;
        }
      }
    }
    if (exit != Exit::split) continue;
    const auto site = x.site(x.arms);
    if (!q.seen.insert(site).second) continue;
    if (!x.rereads) {
      q.open.push_back({x.reading(), x.arms, x.arms + x.armCount});
    } else if (q.terminal == kNoToken && same(x.tok)) {
      // Re-lexing excludes the held token. Only ordinary actions can establish this answer.
      eligible(x, true);
    } else {
      const auto [answer, fresh] = answers.try_emplace(site, std::nullopt);
      if (fresh) {
        pending.push_back({std::move(q), x, answer});
        q = {x.tok, kNoToken, {{x.reading(), x.arms, x.arms + x.armCount - x.rereads}}};
      } else if (answer->second) {
        eligible(x, *answer->second);
      }
    }
  }
}

Exit run(Machine& m, bool* retry) {
  struct Choice { std::uint32_t arm; bool continuation, confirmed = false; };
  struct Site {
    std::size_t choice, keep, end;
    Token token;
    Indented indent;
    TextOffset at;
    std::uint32_t first, count, rereads;
    TextOffset sole;
    NodeCount from, limit = 0;
  };
  std::vector<Choice> choices;
  std::vector<Site> sites;
  std::vector<Frame> frames;
  NodeCount records = 0;
  Exit exit = m.t.drive(m);
  while (exit == Exit::split) [[unlikely]] {
    Prediction prediction;
    const bool advanced = sites.empty() || m.tape.records() != records;
    const auto next = arm(m, retry && advanced ? &prediction : nullptr);
    const bool predicted = prediction.arm != kNoStack;
    if (predicted) {
      // At most the frame spanning the previous record boundary and its
      // suffix changed. Reuse the part that still matches the last snapshot.
      Frame* from = m.frames + m.standing(records);
      if (!sites.empty() && static_cast<std::size_t>(from - m.frames) >= sites.back().keep) {
        const auto start = sites.size() == 1 ? 0 : sites[sites.size() - 2].end;
        const auto offset = static_cast<std::size_t>(from - m.frames) - sites.back().keep;
        if (offset < frames.size() - start)
          from = std::mismatch(from, m.top, frames.begin() + start + offset, frames.end(),
                               [](Frame a, Frame b) { return a.state == b.state; }).first;
      }
      frames.insert(frames.end(), from, m.top);
      sites.push_back({choices.size(), static_cast<std::size_t>(from - m.frames), frames.size(),
                       m.tok, m.indented, static_cast<TextOffset>(m.at - m.src), m.arms, m.armCount, m.rereads,
                       prediction.soleEnd, m.tape.records()});
      records = m.tape.records();
    }
    if (!sites.empty()) choices.push_back({next, !predicted && m.planned > 1});
    if (next == kNoStack) {
      exit = Exit::refused;
      break;
    }
    m.resume = next;
    exit = m.t.drive(m);
    if (predicted) sites.back().limit = m.tape.records();
  }
  if (!retry) return exit;
  *retry = false;
  if (exit == Exit::accepted || sites.empty()) return exit;

  // A failed reader expires after enough real shifts. If the next split
  // is already confirmed, reaching it past the sole token suffices.
  for (auto it = sites.rbegin(); it != sites.rend(); ++it) {
    const Site& s = *it;
    if (!s.sole) continue;
    throwIfCancelled(m.stop);
    unsigned need = s.choice + 1 < choices.size() && choices[s.choice + 1].confirmed ? 1 : kValidateShifts;
    for (auto i = s.limit; i > s.from;) {
      const auto& node = m.tape.at(--i);
      if (node.end < s.sole) break;
      if ((i & 1023) == 0) throwIfCancelled(m.stop);
      if (node.subtreeSize == 1 && !node.isHidden() && node.end != node.begin && --need == 0) {
        choices[s.choice].confirmed = true;
        break;
      }
    }
  }

  // Confirm the elected actions against the recorded splits. An independent
  // full election between two predictions already certified its own plan.
  std::vector<Frame> stack;
  std::vector<std::uint32_t> plan;
  std::size_t next = 0, begin = 0;
  for (const Site& s : sites) {
    throwIfCancelled(m.stop);
    stack.resize(s.keep);
    stack.insert(stack.end(), frames.begin() + begin, frames.begin() + s.end);
    begin = s.end;
    if (s.choice < next) continue;
    if (choices[s.choice].confirmed) {
      next = s.choice + 1;
      continue;
    }
    plan.clear();
    resolve(m, stack, m.src + s.at, s.token, s.indent, s.first, s.count, s.rereads, plan);
    next = s.choice;
    if (plan.empty() || plan.size() > choices.size() - next ||
        !std::equal(plan.begin(), plan.end(), choices.begin() + next,
                    [](auto a, Choice b) { return a == b.arm; })) {
      *retry = true;
      break;
    }
    next += plan.size();
    if (next < choices.size() && choices[next].continuation) {
      *retry = true;
      break;
    }
  }
  if (next == choices.size()) m.planned = m.plan.size();
  return exit;
}

}  // namespace cursiv::engine
