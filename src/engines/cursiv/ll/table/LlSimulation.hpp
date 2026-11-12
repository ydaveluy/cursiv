// The walk over sets of configurations: the construction runs it with a ceiling
// to decide a choice, and the repair search and the caret run it over the
// descent's stack. Predictions use `LlEpisodeSim`.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/ll/table/LlAtnView.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// "The caller is not known": a closure that reaches the end of a rule with
// this carries on into every state a call of it returns to.
inline constexpr std::uint32_t kLlCtxNone = 0xFFFFFFFFu;

// "The two contexts do not join": one of them is the unknown caller, and
// folding a known stack into it would lose the caller a walk actually has.
inline constexpr std::uint32_t kLlCtxApart = 0xFFFFFFFEu;

inline void ll_clear(auto&... x) { (x.clear(), ...); }

// Open-addressed flat arrays keep their storage across tokens. A round number
// clears the table without visiting each slot or allocating nodes.
template <class K, class H>
class LlRoundTable {
 public:
  void release() {
    std::vector<K>{}.swap(_key);
    std::vector<std::uint32_t>{}.swap(_val);
    std::vector<std::uint32_t>{}.swap(_mark);
    _gen = 1;
    _size = 0;
  }

  void clear() {
    _peak = std::max(_peak, _size);
    if (++_gen == 0) {
      std::fill(_mark.begin(), _mark.end(), 0u);
      _gen = 1;
    }
    _size = 0;
  }

  // Gives back the memory a much larger walk left behind: probing a table that
  // is mostly empty touches a cache line for every key.
  void trim() {
    if (_key.size() > kFloor && _key.size() > kSlack * std::max(_peak, _size))
      release();
    _peak = 0;
  }

  // The slot `k` names, made where it had none: `fresh` says which. Inlined
  // on purpose: a walk asks this millions of times, and out of line the call
  // and its marshalling cost as much as the probe.
  CURSIV_ALWAYS_INLINE std::uint32_t& at(const K& k, bool& fresh) {
    if ((_size + 1) * 2 > _key.size()) grow();
    const std::size_t mask = _key.size() - 1;
    std::size_t i = slot(H{}(k));
    while (_mark[i] == _gen && !(_key[i] == k)) i = (i + 1) & mask;
    fresh = _mark[i] != _gen;
    if (fresh) {
      _mark[i] = _gen;
      _key[i] = k;
      _val[i] = 0;
      ++_size;
    }
    return _val[i];
  }

  // The slot `k` names, or null: a lookup that makes nothing, for a caller
  // that may ask again while it works out what to store.
  const std::uint32_t* find(const K& k) const {
    if (_key.empty()) return nullptr;
    const std::size_t mask = _key.size() - 1;
    std::size_t i = slot(H{}(k));
    while (_mark[i] == _gen) {
      if (_key[i] == k) return &_val[i];
      i = (i + 1) & mask;
    }
    return nullptr;
  }

 private:
  // The index takes the high bits of a product by 2^64 / phi, never the low
  // ones, which would name only the last field the hash mixed in.
  std::size_t slot(std::uint64_t h) const {
    return static_cast<std::size_t>((h * 0x9E3779B97F4A7C15ull) >> _shift);
  }

  CURSIV_NOINLINE void grow() {
    const auto size = _key.empty() ? 2 : _key.size() * 2;
    std::vector<K> key(size);
    std::vector<std::uint32_t> val(key.size());
    std::vector<std::uint32_t> mark(key.size(), 0u);
    const std::size_t mask = key.size() - 1;
    _shift = 64u - static_cast<unsigned>(std::countr_zero(key.size()));
    for (std::size_t i = 0; i < _key.size(); ++i) {
      if (_mark[i] != _gen) continue;
      std::size_t j = slot(H{}(_key[i]));
      while (mark[j] == _gen) j = (j + 1) & mask;
      mark[j] = _gen;
      key[j] = _key[i];
      val[j] = _val[i];
    }
    _key.swap(key);
    _val.swap(val);
    _mark.swap(mark);
  }

  // A table this many times larger than a walk filled it is mostly empty.
  static constexpr std::size_t kSlack = 8;
  // Below this a table is cheaper kept than made again.
  static constexpr std::size_t kFloor = std::size_t{1} << 12;

  std::vector<K> _key;
  std::vector<std::uint32_t> _val, _mark;
  std::uint32_t _gen = 1;
  std::size_t _size = 0;
  std::size_t _peak = 0;
  unsigned _shift = 64;
};

struct LlKeyHash {
  std::uint64_t operator()(std::uint64_t k) const noexcept {
    return k * 1099511628211ull;
  }
};

// The stack a closure carries, as a graph: a node names every state a return
// may go to, so two readings that reached one position by two routes are one
// reading with a forked stack.
class LlCtxPool {
 public:
  // The nodes made so far: a node is only meaningful against the pool that
  // made it, until it is cleared.
  std::size_t size() const { return _node.size(); }

  // The nodes made between these two are a walk's own, and go all at once
  // when it ends; the ones before stay. False where a walk already holds the
  // pool, which then ends it.
  bool begin_scratch() {
    if (_scratching) return false;
    _scratching = true;
    _mark = _node.size();
    return true;
  }
  void end_scratch() {
    if (_node.size() > _mark) {
      _frame.resize(_node[_mark].base);
      _node.resize(_mark);
    }
    _scratchIndex.clear();
    _memo.clear();
    _scratchIndex.trim();
    _memo.trim();
    _scratching = false;
  }

  // Whether a walk holds the pool, its nodes since `begin_scratch` its own.
  bool scratching() const { return _scratching; }

  // Whether `c` was made before the walk that holds the pool began: false for
  // every node while no walk holds it.
  bool before_walk(std::uint32_t c) const { return _scratching && c < _mark; }

  std::uint32_t push(std::uint32_t parent, std::uint32_t ret,
                     std::int64_t start = 0) {
    const std::uint32_t frame[kFrame] = {ret, parent,
                                         static_cast<std::uint32_t>(start)};
    return intern(frame, 1);
  }

  // The two stacks as one: a return both name goes to one place, parents
  // joined; a return only one names stays its own. `kLlCtxApart` where one side
  // has the unknown caller. Answered once per pair.
  std::uint32_t merge(std::uint32_t a, std::uint32_t b) {
    if (a == b) return a;
    if (a == kLlCtxNone || b == kLlCtxNone) return kLlCtxApart;
    const std::uint64_t key = merge_key(a, b);
    if (const std::uint32_t* at = _memo.find(key)) return *at;
    return join(a, b);
  }

  unsigned count(std::uint32_t c) const { return _node[c].n; }
  std::uint32_t ret(std::uint32_t c, unsigned i) const {
    return frame(c, i)[0];
  }
  std::uint32_t parent(std::uint32_t c, unsigned i) const {
    return frame(c, i)[1];
  }
  std::int64_t start(std::uint32_t c, unsigned i) const {
    return static_cast<std::int32_t>(frame(c, i)[2]);
  }

  void clear() {
    _frame.clear();
    _node.clear();
    _index.clear();
    _scratchIndex.clear();
    _memo.clear();
    std::vector<Join>{}.swap(_joins);
    _scratching = false;
  }

 private:
  // A node wider than this is a fork nobody profits from: the walk keeps the
  // readings apart instead, which costs configurations and never precision.
  static constexpr unsigned kLlCtxWidth = 16;

  // Words per frame: the state it returns to, its parent, and its start as 32
  // signed bits.
  static constexpr unsigned kFrame = 3;

  const std::uint32_t* frame(std::uint32_t c, unsigned i) const {
    return &_frame[_node[c].base + kFrame * i];
  }

  // `frames` is written up to `n` and read no further: a join asked for at
  // every reading does not wipe it first.
  struct Join {
    Join(std::uint32_t x, std::uint32_t y) : a(x), b(y) {}
    std::uint32_t a, b;
    unsigned i = 0, j = 0, n = 0;
    std::uint32_t frames[kFrame * kLlCtxWidth];
  };

  static std::uint64_t merge_key(std::uint32_t a, std::uint32_t b) {
    return a < b ? (std::uint64_t{a} << 32) | b : (std::uint64_t{b} << 32) | a;
  }

  // Parents precede their joins, without one native frame per stack level.
  // A child is memoised before its parent resumes at that same pair.
  std::uint32_t join(std::uint32_t a, std::uint32_t b) {
    _joins.clear();
    _joins.emplace_back(a, b);
    for (;;) {
      Join& q = _joins.back();
      bool pending = false, apart = false;
      while (q.i < count(q.a) || q.j < count(q.b)) {
        const auto ra = q.i < count(q.a) ? ret(q.a, q.i) : kLlCtxNone;
        const auto rb = q.j < count(q.b) ? ret(q.b, q.j) : kLlCtxNone;
        if (q.n == kLlCtxWidth) {
          apart = true;
          break;
        }
        std::uint32_t* joined = q.frames + kFrame * q.n;
        std::uint32_t p;
        if (ra == rb) {
          if (start(q.a, q.i) != start(q.b, q.j)) {
            apart = true;
            break;
          }
          p = parent(q.a, q.i);
          const auto other = parent(q.b, q.j);
          if (p != other) {
            if (p == kLlCtxNone || other == kLlCtxNone) {
              apart = true;
              break;
            }
            if (const auto* kept = _memo.find(merge_key(p, other))) {
              p = *kept;
              if (p == kLlCtxApart) {
                apart = true;
                break;
              }
            } else {
              _joins.emplace_back(p, other);
              pending = true;
              break;
            }
          }
          joined[2] = frame(q.a, q.i)[2];
          ++q.i;
          ++q.j;
        } else if (ra < rb) {
          joined[2] = frame(q.a, q.i)[2];
          p = parent(q.a, q.i++);
        } else {
          joined[2] = frame(q.b, q.j)[2];
          p = parent(q.b, q.j++);
        }
        joined[0] = std::min(ra, rb);
        joined[1] = p;
        ++q.n;
      }
      if (pending) continue;
      const auto result = apart ? kLlCtxApart : intern(q.frames, q.n);
      bool fresh = false;
      _memo.at(merge_key(q.a, q.b), fresh) = result;
      _joins.pop_back();
      if (_joins.empty()) return result;
    }
  }

  static std::uint64_t hash(const std::uint32_t* frames, unsigned n) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned k = 0; k < kFrame * n; ++k)
      h = (h ^ frames[k]) * 1099511628211ull;
    return h;
  }

  std::uint32_t find(std::uint32_t head, const std::uint32_t* frames,
                     unsigned n) const {
    for (std::uint32_t id = head; id != kLlCtxNone; id = _node[id].link) {
      if (_node[id].n != n) continue;
      const std::uint32_t* held = frame(id, 0);
      unsigned k = 0;
      while (k < kFrame * n && held[k] == frames[k]) ++k;
      if (k == kFrame * n) return id;
    }
    return kLlCtxNone;
  }

  // A walk's nodes are looked up after the ones that stay, and are chained
  // apart from them: they go without the index forgetting a thing.
  std::uint32_t intern(const std::uint32_t* frames, unsigned n) {
    const std::uint64_t h = hash(frames, n);
    bool fresh = false;
    std::uint32_t* heads = nullptr;
    if (_scratching) {
      if (const std::uint32_t* kept = _index.find(h))
        if (const std::uint32_t id = find(*kept, frames, n); id != kLlCtxNone)
          return id;
      heads = &_scratchIndex.at(h, fresh);
    } else {
      heads = &_index.at(h, fresh);
    }
    std::uint32_t& head = *heads;
    if (!fresh)
      if (const std::uint32_t id = find(head, frames, n); id != kLlCtxNone)
        return id;
    const auto id = static_cast<std::uint32_t>(_node.size());
    _node.push_back({.base = static_cast<std::uint32_t>(_frame.size()),
                     .link = fresh ? kLlCtxNone : head,
                     .n = static_cast<std::uint16_t>(n)});
    head = id;
    for (unsigned k = 0; k < kFrame * n; ++k) _frame.push_back(frames[k]);
    return id;
  }

  // Where a node's frames begin and how many they are, and the next node of
  // its hash chain: collisions link through the pool, with no allocation.
  struct Node {
    std::uint32_t base, link;
    std::uint16_t n;
  };
  std::vector<Node> _node;
  std::vector<std::uint32_t> _frame;
  LlRoundTable<std::uint64_t, LlKeyHash> _index;
  LlRoundTable<std::uint64_t, LlKeyHash> _scratchIndex;
  LlRoundTable<std::uint64_t, LlKeyHash> _memo;
  std::vector<Join> _joins;
  std::size_t _mark = 0;
  bool _scratching = false;
};

// Where the walk stands, which arm it came from, and what it will return into.
struct LlConfig {
  std::uint32_t state = 0;
  std::uint32_t ctx = kLlCtxNone;
  std::uint16_t alt = 0;

  bool operator==(const LlConfig&) const = default;
  auto operator<=>(const LlConfig&) const = default;
};

struct LlConfigHash {
  std::uint64_t operator()(const LlConfig& c) const noexcept {
    std::uint64_t h = c.state;
    h = (h * 1099511628211ull) ^ c.ctx;
    h = (h * 1099511628211ull) ^ c.alt;
    return h;
  }
};

// The pool of contexts a stack lives in, and how a return is read into it.
class LlReturnPool {
 public:
  // A prediction reads a return by its class; a caret, which names the call
  // a return belongs to, reads it as it is.
  void read_returns_by_class(const std::uint32_t* classes) {
    _classes = classes;
  }
  std::uint32_t return_of(std::uint32_t state) const {
    return _classes != nullptr ? _classes[state] : state;
  }

  // A tail call leaves no frame over a known caller; over an unknown one the
  // frame is what says which rule the walk is leaving.
  std::uint32_t push_return(std::uint32_t ctx, std::uint32_t follow) {
    if ((follow & kLlTailReturn) == 0) return _ctx.push(ctx, follow);
    return ctx == kLlCtxNone ? _ctx.push(ctx, follow & ~kLlTailReturn) : ctx;
  }

  LlCtxPool& contexts() { return _ctx; }

 private:
  LlCtxPool _ctx;
  const std::uint32_t* _classes = nullptr;
};

// One step of the walk over a set of configurations. `alts` is what the set
// still admits; a set every configuration of which names one arm is decided.
class LlSimulation {
 public:
  explicit LlSimulation(const LlAtnView& atn, std::uint32_t closureLimit = 0);
  ~LlSimulation();
  bool capped() const { return _capped; }
  // How many positions its walks have visited: what their work comes to.
  std::uint64_t visits() const { return _total; }

  // The parse's stop, read as a walk visits its positions. Unset, none is read.
  void watch(const CancellationPoll& stop) { _stop = stop; }

  // The stack ends in an unknown caller by choice, not because the descent
  // stands there: `unbound` then says a reading returned out of a rule into it.
  void truncated(bool on) { _truncated = on; }
  bool unbound() const { return _unbound; }

  // Repair weighing keeps epsilon positions as unions too, not one copy per
  // route.
  void share_epsilon_contexts() {
    _shareEpsilon = true;
    if (_rank.empty()) rank_states();
  }
  // Repair weighing and a caret's replay step one set by the same terminals
  // again and again: a step is kept, by set and terminals, while its contexts
  // last.
  void keep_steps() { _keepSteps = true; }
  // The token every call keeps with its frame from here on: leaving one names
  // the exact call, and its cost depends on the tokens it has read.
  void at_token(std::int64_t token) { _callFrom = token; }
  LlCtxPool& contexts() { return _ctx; }

  // The contexts stand: an exploration interns them once and resumes from
  // sets that name them.
  void reset() {
    _cur.clear();
    _next.clear();
    forget_visits();
    _conditions.clear();
    _visited = 0;
    _capped = false;
  }

  // `reset`, and the contexts go too, as a walk that keeps no set between
  // calls needs: the pool would otherwise grow with the document.
  void forget() {
    reset();
    _ctx.clear();
    forget_steps();
    _truncated = _unbound = false;
  }

  // Between two walks of one parse: the contexts and the steps kept stand, so
  // a step asked again is answered. The walk that outgrows them forgets.
  void resume() {
    if (_ctx.size() > kPoolCap || _keptConfigs.size() > kKeptCap) {
      forget();
      return;
    }
    reset();
    _truncated = _unbound = false;
  }

  const std::vector<LlConfig>& configs() const { return _cur; }

  // Carry on from a set an exploration kept: the contexts it names are this
  // simulation's own, so only the walk is resumed, never the pool.
  void load(const std::vector<LlConfig>& cs) { _cur = cs; }
  void load(const LlConfig* cs, std::size_t n) { _cur.assign(cs, cs + n); }

  // Replay a call abandon over every stack the reading still carries.
  bool leave(std::uint32_t depth, std::uint32_t ret);

  // The closure at one state, not past it: what a caret asks, where the
  // question is what may be read here and not which arm was taken.
  void seed(std::uint32_t state, std::uint32_t stack) {
    reset();
    closure(LlConfig{.state = state, .ctx = stack, .alt = 0}, _next);
    _cur.swap(_next);
  }

  // The arms of one decision, each entered through the edge that names it.
  void start(std::uint32_t decisionState, std::uint32_t stack);

  // Advances every configuration that can read this token. A spelling several
  // terminals match counts as all of them, as a guarded keyword and the bare
  // literal do. False when no reading survives.
  bool step(const std::uint16_t* terms, std::size_t n);
  bool step(std::uint16_t term) { return step(&term, 1); }

  // The step a caret replays over a token it did not read itself: the edges
  // `takes(edge index)` names, whatever they carry; an implied token is read
  // across the condition that stands for it. `terms`, where given, are all
  // `takes` reads beside the edge: the step is kept under them, and the steps
  // kept go when they outgrow `kKeptCap`.
  template <class Takes>
  bool step_edges(Takes&& takes, std::span<const std::uint16_t> terms = {}) {
    const auto walk = [&] {
      return advance(
          [&](std::uint32_t at, const LlAtnEdge&) { return takes(at); });
    };
    if (!_keepSteps || terms.empty()) return walk();
    if (_keptConfigs.size() > kKeptCap) forget_steps();
    return kept_step(terms.data(), terms.size(), walk);
  }

  // A reading stands where a rule ends that has callers, and which of them it
  // returns to is not known.
  bool at_unbound_return() const {
    for (const LlConfig& c : _cur) {
      const LlAtnState& s = _atn.state(c.state);
      if (c.ctx == kLlCtxNone && s.kind == LlAtnKind::ruleStop &&
          _atn.callerBase[s.rule + 1] != _atn.callerBase[s.rule])
        return true;
    }
    return false;
  }

  // A configuration waits on a condition `asks` names.
  template <class Asks>
  bool needs_filter(Asks&& asks) const {
    for (const LlConfig& c : _cur)
      for (const LlAtnEdge& e : _atn.outgoing(c.state))
        if (e.kind == LlEdgeKind::condition && asks(e.label)) return true;
    return false;
  }
  bool needs_filter() const {
    return needs_filter([](std::uint16_t) { return true; });
  }

  // The terminals the next token may be, for a pending condition too: its
  // conditions are walked ahead without changing the configurations, so a wider
  // mode does not reserve a keyword with no edge here.
  template <class F>
  void possible_terms(F&& read) {
    const bool pending = needs_filter();
    if (pending) expand_conditions([](std::uint16_t) { return true; });
    for (const auto& c : pending ? _next : _cur)
      for (const LlAtnEdge& e : _atn.outgoing(c.state))
        if (e.kind == LlEdgeKind::atom) read(e.label);
  }

  // Conditions wait for the token at this position. A step has only just
  // consumed the preceding token, so its closure must leave these edges
  // pending until the caller supplies the next token and its gap.
  template <class F>
  bool filter(F&& accepts) {
    expand_conditions(std::forward<F>(accepts));
    _cur.clear();
    // Intermediate closure positions are bookkeeping, not surviving arms.
    // Keep only positions that can consume or have completed the reading.
    for (const auto& c : _next)
      if (ready(c)) _cur.push_back(c);
    std::sort(_cur.begin(), _cur.end());
    _cur.erase(std::unique(_cur.begin(), _cur.end()), _cur.end());
    return !_cur.empty();
  }

  // The arm every surviving configuration names, `kNoRow` while more than
  // one does.
  std::uint16_t decided() const {
    if (_cur.empty()) return kNoRow;
    const std::uint16_t a = _cur.front().alt;
    for (const LlConfig& c : _cur)
      if (c.alt != a) return kNoRow;
    return a;
  }

  // The lowest arm still standing, which is what a walk that runs out of input
  // or of patience answers: the arms are ordered, and the first that admits
  // the text is the reading the grammar states.
  std::uint16_t lowest() const {
    std::uint16_t a = kNoRow;
    for (const LlConfig& c : _cur)
      if (a == kNoRow || c.alt < a) a = c.alt;
    return a;
  }

 private:
  template <class F>
  void expand_conditions(F&& accepts) {
    _next.assign(_cur.begin(), _cur.end());
    _conditions.assign(_cur.begin(), _cur.end());
    forget_visits();
    // A union may widen a published position after its conditions were
    // traversed. Walk each incoming delta, not each published position once.
    for (std::size_t at = 0; at < _conditions.size(); ++at) {
      const LlConfig c = _conditions[at];
      for (const LlAtnEdge& e : _atn.outgoing(c.state))
        if (e.kind == LlEdgeKind::condition && accepts(e.label))
          closure(LlConfig{.state = e.target, .ctx = c.ctx, .alt = c.alt},
                  _next, &_conditions);
    }
  }

  void forget_steps() {
    _kept.clear();
    _keptConfigs.clear();
    _keptTerms.clear();
    _keptIndex.clear();
  }

  // The set, the terminals and the pool decide the step `walk` takes, and the
  // pool holds every context a step walked before made: that step is answered
  // as it was.
  template <class Walk>
  bool kept_step(const std::uint16_t* terms, std::size_t n, Walk&& walk) {
    std::uint64_t h = 1469598103934665603ull ^ (_truncated ? 1u : 0u);
    h = (h ^ static_cast<std::uint64_t>(_callFrom)) * 1099511628211ull;
    for (const LlConfig& c : _cur) {
      h = (h ^ (std::uint64_t{c.state} << 32 | c.ctx)) * 1099511628211ull;
      h = (h ^ c.alt) * 1099511628211ull;
    }
    for (std::size_t m = 0; m < n; ++m) h = (h ^ terms[m]) * 1099511628211ull;
    bool fresh = false;
    std::uint32_t& head = _keptIndex.at(h, fresh);
    if (!fresh)
      for (std::uint32_t k = head; k != kNoFollow; k = _kept[k].next) {
        const Kept& e = _kept[k];
        if (e.fromSize != _cur.size() || e.termCount != n ||
            e.truncated != _truncated || e.callFrom != _callFrom ||
            !std::equal(_cur.begin(), _cur.end(),
                        _keptConfigs.begin() + e.from) ||
            !std::equal(terms, terms + n, _keptTerms.begin() + e.terms))
          continue;
        _cur.assign(_keptConfigs.begin() + e.to,
                    _keptConfigs.begin() + e.to + e.toSize);
        _unbound = _unbound || e.unbound;
        return !_cur.empty();
      }
    Kept e;
    e.callFrom = _callFrom;
    e.truncated = _truncated;
    e.next = fresh ? kNoFollow : head;
    e.from = static_cast<std::uint32_t>(_keptConfigs.size());
    e.fromSize = static_cast<std::uint32_t>(_cur.size());
    _keptConfigs.insert(_keptConfigs.end(), _cur.begin(), _cur.end());
    e.terms = static_cast<std::uint32_t>(_keptTerms.size());
    e.termCount = static_cast<std::uint32_t>(n);
    _keptTerms.insert(_keptTerms.end(), terms, terms + n);
    const bool was = _unbound;
    _unbound = false;
    const bool read = walk();
    e.unbound = _unbound;
    _unbound = _unbound || was;
    e.to = static_cast<std::uint32_t>(_keptConfigs.size());
    e.toSize = static_cast<std::uint32_t>(_cur.size());
    _keptConfigs.insert(_keptConfigs.end(), _cur.begin(), _cur.end());
    head = static_cast<std::uint32_t>(_kept.size());
    _kept.push_back(e);
    return read;
  }

  bool ready(const LlConfig& c) const {
    return (_atn.state(c.state).kind == LlAtnKind::ruleStop &&
            c.ctx == kLlCtxNone) ||
           _atn.has(c.state, LlEdgeKind::atom);
  }

  struct Labelled {
    const std::uint16_t* terms;
    std::size_t n;
    const std::vector<std::uint64_t>* bits;
    bool operator()(std::uint32_t, const LlAtnEdge& e) const {
      if (e.kind != LlEdgeKind::atom) return false;
      if (bits != nullptr)
        return (e.label >> 6) < bits->size() &&
               ((*bits)[e.label >> 6] >> (e.label & 63u) & 1u) != 0;
      for (std::size_t m = 0; m < n; ++m)
        if (e.label == terms[m]) return true;
      return false;
    }
  };

  // Many terminals are tested once per edge of every reading: a bit row.
  const std::vector<std::uint64_t>* labels(const std::uint16_t* terms,
                                           std::size_t n);

  template <class Takes>
  bool advance(Takes&& takes) {
    _next.clear();
    forget_visits();
    _conditions.clear();
    _visited = 0;
    _capped = false;
    for (const LlConfig c : _cur) {
      const LlAtnState& s = _atn.state(c.state);
      for (std::uint32_t i = 0; i < s.nedges; ++i) {
        const LlAtnEdge& e = _atn.edge(s.edge + i);
        if ((e.kind == LlEdgeKind::atom || e.kind == LlEdgeKind::condition) &&
            takes(s.edge + i, e))
          feed(LlConfig{.state = e.target, .ctx = c.ctx, .alt = c.alt});
      }
    }
    expand_sources();
    _cur.swap(_next);
    return !_cur.empty();
  }

  // A reading already here with another stack joins it, and the walk goes on
  // only where the join widened the stack; where they cannot join, both are
  // kept. Depth first, on a stack of its own.
  bool closure(const LlConfig& c, std::vector<LlConfig>& out,
               std::vector<LlConfig>* conditions = nullptr) {
    _pending.clear();
    _pending.push_back({c, kNoFollow});
    const bool was = std::exchange(_immediate, true);
    const bool done = drain(out, conditions);
    _immediate = was;
    return done;
  }

  // A walk that shares epsilon positions takes all its sources before expanding
  // any. Where stacks cannot join, the sources are also walked one at a time,
  // and that walk wins if it costs at most a few times the first.
  void feed(const LlConfig& c) {
    if (!_shareEpsilon) {
      closure(c, _next);
    } else {
      _sources.push_back(c);
      _pending.push_back({c, kNoFollow});
    }
  }
  void expand_sources();

  // Whether every source ends within `budget` visits.
  bool one_at_a_time(std::uint64_t budget);

  bool drain(std::vector<LlConfig>& out,
             std::vector<LlConfig>* conditions = nullptr);

  // Where a state stands in a reverse postorder of the graph a closure walks:
  // a entry expands once everything that reaches it from lower down has
  // arrived.
  void rank_states();

  void forget_visits() {
    _seen.clear();
    _at.clear();
    _nextAtPosition.clear();
    forget_deltas();
  }

  void forget_deltas() {
    _pending.clear();
    _delta.clear();
    _ready.clear();
  }

  // The contexts that reached a position and have not yet left it.
  void defer(std::uint32_t at, const LlConfig& cur, std::size_t entries);

  void visit(const LlConfig& cur, std::vector<LlConfig>& out,
             std::vector<LlConfig>* conditions = nullptr);

  void expand(const LlConfig& cur);

  static constexpr std::uint32_t kNoFollow = ~std::uint32_t{0};
  static constexpr std::size_t kLinearLabels = 8;
  // The entries of a position a new stack tries to join, latest first; one that
  // joins none of them takes a entry of its own, so a long right-recursive list
  // is not quadratic.
  static constexpr unsigned kEntriesTried = 8;
  static constexpr std::size_t kPoolCap = std::size_t{1} << 16;
  static constexpr std::size_t kKeptCap = std::size_t{1} << 20;
  // The one-at-a-time walk may spend this many visits per visit of the first,
  // and this many more.
  static constexpr std::uint64_t kSerialRatio = 4, kSerialRoom = 1000;
  struct Pending {
    LlConfig config;
    std::uint32_t follow;
  };
  std::vector<Pending> _pending;
  // Where a kept step's set, its terminals and the set it reached lie in the
  // flat arrays, and whether it returned into the unknown caller.
  struct Kept {
    std::int64_t callFrom = 0;
    std::uint32_t next = 0;
    std::uint32_t from = 0, fromSize = 0, terms = 0, termCount = 0;
    std::uint32_t to = 0, toSize = 0;
    bool unbound = false, truncated = false;
  };
  std::vector<Kept> _kept;
  std::vector<LlConfig> _keptConfigs;
  std::vector<std::uint16_t> _keptTerms;
  LlRoundTable<std::uint64_t, LlKeyHash> _keptIndex;
  std::int64_t _callFrom = 0;

  const LlAtnView& _atn;
  LlCtxPool _ctx;
  std::vector<LlConfig> _cur, _next;
  LlRoundTable<LlConfig, LlConfigHash> _seen;
  // Where a reading of one position and one arm already stands, so a second
  // route to it joins instead of doubling the set.
  LlRoundTable<std::uint64_t, LlKeyHash> _at;
  // Every compatible union at a position, for repair weighing: the first
  // stack stored there cannot keep the others apart.
  std::vector<std::uint32_t> _nextAtPosition;
  // Per entry of a walk that shares epsilon positions: the contexts that
  // arrived since it last expanded, and the entries waiting to expand.
  std::vector<std::uint32_t> _delta;
  // (rank, entry) as one word: the entries leave in the order of the ranks.
  std::vector<std::uint64_t> _ready;
  std::vector<std::uint32_t> _rank;
  std::vector<std::uint64_t> _labels;
  std::vector<LlConfig> _sources;
  bool _immediate = false, _split = false, _stopAtSplit = false;
  std::uint64_t _visits = 0, _budget = 0, _total = 0;
  std::vector<LlConfig> _conditions;
  std::uint32_t _limit = 0, _visited = 0;
  bool _capped = false, _truncated = false, _unbound = false;
  CancellationPoll _stop;
  bool _shareEpsilon = false;
  bool _keepSteps = false;
};

}  // namespace cursiv::engine
