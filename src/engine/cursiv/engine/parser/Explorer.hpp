// A reading of the text that writes no tree: the generated states run over
// it as they run over the machine, one token at a time, its stack a path of
// a cactus that every reading of one search shares.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Tables.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// A shifted token or a production reduced over the nodes from `kids` on,
// last read first, with the choices taken since its last derived node.
struct Derivation {
  std::uint32_t production = kNoProduction;
  std::uint32_t kids = 0;
  std::uint32_t arm = kNoStack;
  // The next way its node was made.
  std::uint32_t next = kNoStack;
};

struct Routes;

// Shared stacks, identified by one node. Between clears an instance either
// shares the stacks of the same states (`intern`), or keeps a node per state,
// parent, end of the text read and token in hand, with the ways readings
// made it (`push`).
class Cactus {
 public:
  static constexpr TextOffset kNoHeldEnd = kNoStack;
  static_assert(kMaxDocument < kNoHeldEnd);

  // Forgets every node. The frames of `stack` are then nodes too, each by
  // its place: a reading stands on that stack with none of it copied.
  void clear(std::span<const Frame> stack = {}) {
    _nodes.clear();
    _ways.resize(1);
    _choices.clear();
    _kids.clear();
    if (!_alternatives.empty()) _alternatives.clear();
    _others.clear();
    _alias = 0;
    _marks.clear();
    _marked = 0;
    _revision = 0;
    _joinBase += static_cast<std::uint32_t>(_joined.size());
    if (_joinBase == 0) std::ranges::fill(_joined, kNoStack);
    _frames = stack.data();
    _base = static_cast<std::uint32_t>(stack.size());
  }
  // The first node that is no frame of the stack stood on.
  [[nodiscard]] std::uint32_t base() const { return _base; }
  // How many nodes were made.
  [[nodiscard]] std::size_t size() const { return _nodes.size(); }
  [[nodiscard]] std::uint32_t state(std::uint32_t node) const {
    return node < _base ? _frames[node].state : _nodes[node - _base].state;
  }
  [[nodiscard]] std::uint32_t parent(std::uint32_t node) const {
    return node < _base ? node - 1 : _nodes[node - _base].parent;
  }
  // Where the text of a node `push` made ends.
  [[nodiscard]] TextOffset end(std::uint32_t node) const { return _nodes[node - _base].second; }
  // How many states the stack of a node `intern` made holds.
  [[nodiscard]] std::uint32_t depth(std::uint32_t node) const {
    return node < _base ? node + 1 : _nodes[node - _base].second;
  }

  // The node of `state` over `parent`: a reading reduces into the same state
  // again and again, so the first child is asked first.
  CURSIV_ALWAYS_INLINE std::uint32_t intern(std::uint32_t state, std::uint32_t parent) {
    if (parent >= _base && parent != kNoStack) [[likely]] {
      const std::uint32_t first = _nodes[parent - _base].first;
      if (first != kNoStack && _nodes[first - _base].state == state) [[likely]] return first;
    }
    const auto at = intern_node(state, parent, parent == kNoStack ? 1u : depth(parent) + 1, 0,
                                kNoHeldEnd);
    if (parent >= _base && parent != kNoStack) _nodes[parent - _base].first = at;
    return at;
  }
  // A state reached while probing a held token. Its nodes carry no derivations.
  std::uint32_t probe(std::uint32_t state, std::uint32_t parent) {
    return intern_node(state, parent, 0, kNoToken, kNoHeldEnd);
  }
  struct Pushed { std::uint32_t node; bool shared; };
  // Intern a forest node and its derivation over the last `count` children.
  Pushed push(std::uint32_t state, std::uint32_t parent, TextOffset end, std::uint64_t heldHead,
              TextOffset heldEnd, Derivation made, std::size_t count);
  std::uint32_t choose(std::uint32_t arm, std::uint32_t path, std::uint32_t pending) {
    _choices.push_back({arm, path, pending});
    return static_cast<std::uint32_t>(_choices.size() - 1);
  }
  // Append the entire reading, or just the decisions since its last derived node.
  void append(std::vector<std::uint32_t>& out, std::uint32_t choice, bool pending) const;
  // Changes when an existing node gains a distinct derivation.
  [[nodiscard]] std::uint32_t revision() const { return _revision; }
  // Descend along the selected route. A reduction keeps its children, while
  // a guard inspects the same path without taking any frames off the stack.
  template <bool Inspect = false>
  CURSIV_ALWAYS_INLINE std::uint32_t down(std::uint32_t node, std::uint32_t count,
                                          Routes* routes, bool keep = true) {
    if (routes == nullptr) {
      while (count != 0 && node >= _base) {
        if (Inspect && node == kNoStack) return kNoStack;
        node = _nodes[node - _base].parent;
        --count;
      }
      if (Inspect && count > node) return kNoStack;
      return node - count;
    }
    const bool others = !_others.empty();
    for (; count != 0; --count) {
      if (Inspect && node == kNoStack) return kNoStack;
      if (others && node >= _base && _nodes[node - _base].next != kNoStack) [[unlikely]]
        node = route(node, *routes, Inspect);
      if constexpr (!Inspect) {
        if (keep) _kids.push_back(node);
      }
      node = parent(node);
    }
    return node;
  }
  // `node`, made by `push` alike `head` over another parent, stands for
  // `head`. While both were made the same way over parents alike, their
  // parents stand so instead, unless a reading went down through `head`'s.
  void alias(std::uint32_t head, std::uint32_t node, const Tables& t, const CancellationToken& stop);
  // Whether a reading on `node` goes down through a node that stands for
  // others.
  [[nodiscard]] bool forks(std::uint32_t node);
  // Each node `node` stands for.
  template <class Visit>
  void others(std::uint32_t node, Visit&& visit) const {
    for (std::uint32_t at = _nodes[node - _base].next; at != kNoStack; at = _others[at].second)
      visit(_others[at].first);
  }
  // What nodes `push` made alike share, whatever their parents: the state,
  // where the text ends, and the token; and a hash of it.
  [[nodiscard]] std::tuple<std::uint32_t, TextOffset, std::uint64_t, TextOffset> key(
      std::uint32_t node) const {
    const Node& n = _nodes[node - _base];
    return {n.state, n.second, n.held, n.heldEnd};
  }
  [[nodiscard]] std::uint64_t hash(std::uint32_t node) const;
  // Whether `a` and `b` were each made one way, the same way over kids made
  // the same way.
  bool same(std::uint32_t a, std::uint32_t b, const Tables& t, const CancellationToken& stop);
  // Where the kids the next `down` keeps begin.
  [[nodiscard]] std::uint32_t kept() const { return static_cast<std::uint32_t>(_kids.size()); }

  // The ways a node was made, the first of them, none for a frame; and one
  // of them.
  [[nodiscard]] std::uint32_t ways(std::uint32_t node) const {
    return node < _base ? kNoStack : _nodes[node - _base].first;
  }
  [[nodiscard]] const Derivation& way(std::uint32_t at) const { return _ways[at]; }
  // The unit productions the way crossed from its parent state.
  [[nodiscard]] std::uint32_t hops(const Tables& t, std::uint32_t node, const Derivation& way) const {
    const Node& n = _nodes[node - _base];
    return t.derivation(state(n.parent), way.production, static_cast<std::uint16_t>(n.held));
  }
  // The `count` nodes a way was reduced over, the last read first.
  [[nodiscard]] std::span<const std::uint32_t> kids(const Derivation& made,
                                                     std::size_t count) const {
    return std::span(_kids).subspan(made.kids, count);
  }
  // `node` keeps `elected` alone of its ways.
  void settle(std::uint32_t node, std::uint32_t elected) {
    _nodes[node - _base].first = elected;
    _ways[elected].next = kNoStack;
    _alternatives.erase(node);
  }

 private:
  // A canonical node caches its last child and depth in first and second.
  // A forest node holds its aliases, first derivation and end in next, first and second.
  struct Node {
    std::uint64_t held;
    TextOffset heldEnd;
    std::uint32_t state;
    std::uint32_t parent;
    std::uint32_t next;
    std::uint32_t first;
    std::uint32_t second;
  };
  static_assert(sizeof(Node) == 32);

  std::uint32_t intern_node(std::uint32_t state, std::uint32_t parent, TextOffset end,
                            std::uint64_t held, TextOffset heldEnd);
  // The node `routes` takes where `node` stands for others.
  CURSIV_NOINLINE std::uint32_t route(std::uint32_t node, Routes& routes, bool inspect);

  std::vector<Node> _nodes;
  std::vector<Derivation> _ways{Derivation{}};
  std::vector<std::uint32_t> _kids;
  // The stack stood on.
  const Frame* _frames = nullptr;
  std::uint32_t _base = 0;
  std::uint32_t _revision = 0;
  struct Choice { std::uint32_t arm, path, pending; };
  std::vector<Choice> _choices;
  // Only a node with competing derivations needs an index.
  struct Alternatives {
    std::uint32_t first;
    std::unordered_multimap<std::uint64_t, std::uint32_t> others;
  };
  std::unordered_map<std::uint32_t, Alternatives> _alternatives;
  // The nodes a node stands for, each with the next.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _others;
  // Even generation of the alias links. A current fork mark uses bit zero for its answer.
  std::uint32_t _alias = 0;
  // Per node `push` made, the node it stands for, and whether a reading went
  // down through it, as known at the last alias; the kids told so far.
  struct Mark {
    std::uint32_t head = kNoStack;
    bool passed = false;
    std::uint32_t fork = 0;
  };
  std::vector<Mark> _marks;
  std::size_t _marked = 0;
  // The pairs of nodes `same` has yet to compare.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _pairs;
  // Slots hold node indices biased by a new table-width on each clear.
  [[gnu::cold]] CURSIV_NOINLINE void grow_join();
  std::vector<std::uint32_t> _joined = std::vector<std::uint32_t>(64, kNoStack);
  std::uint32_t _joinBase = 0;
};

// A reading's stack, resume point, text position, token and indentation.
struct Reading {
  std::uint32_t node = kNoStack;
  std::uint32_t resume = 0;
  const char* at = nullptr;
  Token tok{nullptr, 0};
  Indented indented{};
  // This part of a derived run, copied or assigned as a whole.
  Reading& reading() noexcept { return *this; }
  const Reading& reading() const noexcept { return *this; }
  auto site(std::uint32_t first) const {
    return std::tuple(node, first, at, tok.end, tok.head, indented.level, indented.depth);
  }
};
using SplitSite = decltype(Reading{}.site(0));

struct Explorer : Reading {
  struct Closure;
  struct Options {
    Routes* routes = nullptr;
    Closure* closure = nullptr;
    bool keepShift = true;
  };
  Machine& text;
  Cactus& stacks;
  Options options;
  Closure* const roundClosure;

  Explorer(Machine& text, Cactus& stacks, const Reading& reading,
           Options options = {nullptr, nullptr, true})
      : Reading(reading), text(text), stacks(stacks), options(options), roundClosure(options.closure) {
    if (options.routes) this->options.closure = nullptr;
  }

  Exit step() {
    ++text.explored;
    return text.t.explore(*this);
  }

  // A run comes back after each token it shifts, and says what it derives.
  static constexpr bool kSteps = true;
  CURSIV_ALWAYS_INLINE Machine& lexing() { return text; }
  CURSIV_NOINLINE void push(std::uint32_t state, std::uint32_t production,
                             Token held = {nullptr, 0});
  // Carry the reduction's production to its shared goto for ranking.
  CURSIV_ALWAYS_INLINE NodeCount pop(std::uint32_t count, std::uint32_t production) {
    solid = count > fresh;
    fresh -= solid ? fresh : count;
    left = count;
    kids = stacks.kept();
    node = stacks.down(node, count, options.routes, options.keepShift);
    return production;
  }
  CURSIV_ALWAYS_INLINE std::uint32_t under(std::uint32_t depth = 0) const {
    if (!depth) return stacks.state(node);
    const auto below = stacks.down<true>(node, depth, options.routes);
    return below == kNoStack ? kNoStack : stacks.state(below);
  }
  CURSIV_ALWAYS_INLINE NodeCount here(std::uint32_t production) {
    left = 0;
    solid = false;
    return production;
  }
  CURSIV_ALWAYS_INLINE void settle() {}
  // Its steps write no records, and its frames grow as they are pushed.
  static constexpr bool roomy(bool, bool) { return true; }
  static constexpr bool flushed() { return true; }
  static void make_room() {}
  CURSIV_ALWAYS_INLINE void forget(const char*) {}
  CURSIV_NOINLINE void shift(std::uint32_t state, std::uint32_t, Token token);
  template <unsigned Shapes, class Fuse = std::nullptr_t>
  CURSIV_ALWAYS_INLINE NodeCount close(NodeCount open, std::uint32_t, const char*,
                                       Fuse&& = nullptr) {
    return open;
  }
  CURSIV_ALWAYS_INLINE void accept() {}
  CURSIV_ALWAYS_INLINE Exit shifted() {
    return Exit::shifted;
  }
  Exit refused(Token held) {
    tok = held;
    resume = 2 * under() + 1;
    return Exit::refused;
  }
  [[gnu::cold]] CURSIV_NOINLINE Exit split(Token held, std::uint32_t first, std::uint32_t count,
                                         std::uint32_t reread = 0) {
    refused(held);
    arms = first;
    armCount = count;
    rereads = reread;
    return Exit::split;
  }

  // Where its nodes stand: the text read, past the tokens with no text.
  [[nodiscard]] TextOffset read() const {
    return static_cast<TextOffset>(at - text.src) + invented;
  }
  // The node of `state` with `held` in hand or shifted, made by `production`.
  std::uint32_t made(std::uint32_t state, Token held, std::uint32_t production);

  bool visited(std::uint32_t production, const char* p, Token held) const;

  // The token it shifted last and the number of zero-width tokens shifted.
  Token taken{nullptr, 0};
  TextOffset invented = 0;

  // The actions of the split and the number of trailing lexical fallbacks.
  std::uint32_t arms = 0;
  std::uint32_t armCount = 0;
  std::uint32_t rereads = 0;
  // The state it shifted its last token from.
  std::uint32_t from = 0;
  // How many frames on top it made of no text, and whether the reduction
  // under way pops one that holds some.
  std::uint32_t fresh = 0;
  bool solid = false;
  // Its ways down, where it keeps how it made its nodes; the nodes its
  // reduction popped, where they begin among the kids, and the split action
  // still to derive.
  std::uint32_t left = 0;
  std::uint32_t kids = 0;
  std::uint32_t arm = kNoStack;
};

}  // namespace cursiv::engine
