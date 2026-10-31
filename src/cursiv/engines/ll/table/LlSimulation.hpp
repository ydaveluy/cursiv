// The one simulation both storeys run: the construction walks it with a
// ceiling to settle a choice once and for all, a parse walks it with the
// stack it actually stands on. Nothing here knows a grammar.
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/table/LlAtnView.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// "The caller is not known": a closure that reaches the end of a rule with
// this carries on into every state a call of it returns to.
inline constexpr std::uint32_t kLlCtxNone = 0xFFFFFFFFu;

// "The two contexts do not join": one of them is the unknown caller, and
// folding a known stack into it would lose the caller a walk actually has.
inline constexpr std::uint32_t kLlCtxApart = 0xFFFFFFFEu;

inline void ll_clear(auto&... x) { (x.clear(), ...); }

// Open-addressed flat arrays retain their storage across tokens.
// Stamps clear the table without visiting each slot or allocating nodes.
template <class K, class H>
class LlStamped {
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

  // Gives back the room a much larger walk left behind: probing a table that
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
  // THE INDEX TAKES THE HIGH BITS OF A MULTIPLICATION, NEVER THE LOW ONES: a
  // multiply carries upward alone, so masking the low bits of a hash that
  // ends in one leaves the index naming the last field the hash mixed in --
  // for a reading, its arm, and a walk of a thousand readings then queues on
  // four slots. 2^64 / phi is the multiplier that spreads them.
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

// The stack a closure carries, as a graph and not a list: a node names every
// state a return may go to, so two readings that reached one position by two
// routes are ONE reading with a forked stack. Without that, a rule reached
// through eight callers is eight configurations, and a grammar whose
// expressions cascade -- C's -- multiplies them at every level.
class LlCtxPool {
 public:
  // The nodes made so far, and which life of the pool they belong to: a node
  // is only meaningful against the pool that made it, until it is cleared.
  std::size_t size() const { return _n.size(); }
  std::uint64_t life() const { return _life; }

  // The nodes made between these two are a walk's own, and go all at once
  // when it ends; the ones before stay. False where a walk already holds the
  // pool, which then ends it.
  bool begin_scratch() {
    if (_scratching) return false;
    _scratching = true;
    _mark = _n.size();
    return true;
  }
  void end_scratch() {
    if (_n.size() > _mark) {
      _pair.resize(_base[_mark]);
      _starts.resize(_base[_mark] / 2);
      _base.resize(_mark);
      _link.resize(_mark);
      _n.resize(_mark);
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
    const std::uint32_t pair[2] = {ret, parent};
    return intern(pair, 1, &start);
  }

  // The two stacks as one: a return both name goes to one place with the two
  // parents joined, a return only one names stays its own. `kLlCtxApart`
  // where the unknown caller is on one side -- the walk then keeps both,
  // since no join can say what a known stack and an unknown one agree on.
  // Answered once per pair: a closure asks the same join at every position
  // that reaches one rule two ways, and the graph is small enough that the
  // memo is the whole of what makes merging cheaper than not merging.
  std::uint32_t merge(std::uint32_t a, std::uint32_t b) {
    if (a == b) return a;
    if (a == kLlCtxNone || b == kLlCtxNone) return kLlCtxApart;
    const std::uint64_t key = merge_key(a, b);
    if (const std::uint32_t* at = _memo.find(key)) return *at;
    return join(a, b);
  }

  unsigned count(std::uint32_t c) const { return _n[c]; }
  std::uint32_t ret(std::uint32_t c, unsigned i) const {
    return _pair[_base[c] + 2 * i];
  }
  std::uint32_t parent(std::uint32_t c, unsigned i) const {
    return _pair[_base[c] + 2 * i + 1];
  }
  std::int64_t start(std::uint32_t c, unsigned i) const {
    return _starts[_base[c] / 2 + i];
  }

  void clear() {
    _pair.clear();
    _starts.clear();
    _base.clear();
    _n.clear();
    _link.clear();
    _index.clear();
    _scratchIndex.clear();
    _memo.clear();
    std::vector<Join>{}.swap(_joins);
    _scratching = false;
    _life = ++lives();
  }

 private:
  // A node wider than this is a fork nobody profits from: the walk keeps the
  // readings apart instead, which costs configurations and never precision.
  static constexpr unsigned kLlCtxWidth = 16;

  struct Join {
    std::uint32_t a, b;
    unsigned i = 0, j = 0, n = 0;
    std::uint32_t pairs[2 * kLlCtxWidth]{};
    std::int64_t starts[kLlCtxWidth]{};
  };

  static std::uint64_t merge_key(std::uint32_t a, std::uint32_t b) {
    return a < b ? (std::uint64_t{a} << 32) | b : (std::uint64_t{b} << 32) | a;
  }

  // Parents precede their joins, without one native frame per stack level.
  // A child is memoised before its parent resumes at that same pair.
  std::uint32_t join(std::uint32_t a, std::uint32_t b) {
    _joins.clear();
    _joins.push_back({a, b});
    for (;;) {
      Join& q = _joins.back();
      bool pending = false, apart = false;
      while (q.i < _n[q.a] || q.j < _n[q.b]) {
        const auto ra = q.i < _n[q.a] ? ret(q.a, q.i) : kLlCtxNone;
        const auto rb = q.j < _n[q.b] ? ret(q.b, q.j) : kLlCtxNone;
        if (q.n == kLlCtxWidth) {
          apart = true;
          break;
        }
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
              _joins.push_back({p, other});
              pending = true;
              break;
            }
          }
          q.starts[q.n] = start(q.a, q.i);
          ++q.i;
          ++q.j;
        } else if (ra < rb) {
          q.starts[q.n] = start(q.a, q.i);
          p = parent(q.a, q.i++);
        } else {
          q.starts[q.n] = start(q.b, q.j);
          p = parent(q.b, q.j++);
        }
        q.pairs[2 * q.n] = std::min(ra, rb);
        q.pairs[2 * q.n + 1] = p;
        ++q.n;
      }
      if (pending) continue;
      const auto result = apart ? kLlCtxApart : intern(q.pairs, q.n, q.starts);
      bool fresh = false;
      _memo.at(merge_key(q.a, q.b), fresh) = result;
      _joins.pop_back();
      if (_joins.empty()) return result;
    }
  }

  static std::uint64_t hash(const std::uint32_t* pairs, unsigned n,
                            const std::int64_t* starts) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned k = 0; k < 2 * n; ++k) h = (h ^ pairs[k]) * 1099511628211ull;
    for (unsigned k = 0; k < n; ++k)
      h = (h ^ static_cast<std::uint64_t>(starts[k])) * 1099511628211ull;
    return h;
  }

  std::uint32_t find(std::uint32_t head, const std::uint32_t* pairs,
                     unsigned n, const std::int64_t* starts) const {
    for (std::uint32_t id = head; id != kLlCtxNone; id = _link[id]) {
      if (_n[id] != n) continue;
      bool same = true;
      for (unsigned k = 0; same && k < 2 * n; ++k)
        same = _pair[_base[id] + k] == pairs[k];
      for (unsigned k = 0; same && k < n; ++k)
        same = start(id, k) == starts[k];
      if (same) return id;
    }
    return kLlCtxNone;
  }

  // A walk's nodes are looked up after the ones that stay, and are chained
  // apart from them: they go without the index forgetting a thing.
  std::uint32_t intern(const std::uint32_t* pairs, unsigned n,
                       const std::int64_t* starts) {
    const std::uint64_t h = hash(pairs, n, starts);
    bool fresh = false;
    std::uint32_t* heads = nullptr;
    if (_scratching) {
      if (const std::uint32_t* kept = _index.find(h))
        if (const std::uint32_t id = find(*kept, pairs, n, starts);
            id != kLlCtxNone)
          return id;
      heads = &_scratchIndex.at(h, fresh);
    } else {
      heads = &_index.at(h, fresh);
    }
    std::uint32_t& head = *heads;
    if (!fresh)
      if (const std::uint32_t id = find(head, pairs, n, starts);
          id != kLlCtxNone)
        return id;
    const auto id = static_cast<std::uint32_t>(_n.size());
    _link.push_back(fresh ? kLlCtxNone : head);
    head = id;
    _base.push_back(static_cast<std::uint32_t>(_pair.size()));
    _n.push_back(static_cast<std::uint16_t>(n));
    for (unsigned k = 0; k < n; ++k) {
      _pair.push_back(pairs[2 * k]);
      _pair.push_back(pairs[2 * k + 1]);
      _starts.push_back(starts[k]);
    }
    return id;
  }

  std::vector<std::uint32_t> _pair;
  std::vector<std::int64_t> _starts;
  std::vector<std::uint32_t> _base;
  std::vector<std::uint16_t> _n;
  // Hash collisions link through pool nodes, avoiding per-hash allocations.
  std::vector<std::uint32_t> _link;
  LlStamped<std::uint64_t, LlKeyHash> _index;
  LlStamped<std::uint64_t, LlKeyHash> _scratchIndex;
  LlStamped<std::uint64_t, LlKeyHash> _memo;
  std::vector<Join> _joins;
  static std::atomic<std::uint64_t>& lives() {
    static std::atomic<std::uint64_t> count{0};
    return count;
  }
  std::size_t _mark = 0;
  bool _scratching = false;
  std::uint64_t _life = ++lives();
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
    if (follow == kNoFollow) return ctx;
    if ((follow & kLlTailReturn) == 0) return _ctx.push(ctx, follow);
    return ctx == kLlCtxNone ? _ctx.push(ctx, follow & ~kLlTailReturn) : ctx;
  }

  LlCtxPool& contexts() { return _ctx; }

 protected:
  static constexpr std::uint32_t kNoFollow = ~std::uint32_t{0};
  LlCtxPool _ctx;

 private:
  const std::uint32_t* _classes = nullptr;
};

// One step of the walk over a set of configurations. `alts` is what the set
// still admits; a set every configuration of which names one arm is settled.
class LlSimulation : public LlReturnPool {
 public:
  explicit LlSimulation(const LlAtnView& atn, std::uint32_t closureLimit = 0)
      : _atn(atn), _limit(closureLimit) {}
  bool capped() const { return _capped; }
  // How many positions its walks have visited: what their work comes to.
  std::uint64_t visits() const { return _total; }

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
  // ...and steps one set of readings by the same terminals again and again: a
  // step is kept, by the set and the terminals, while its contexts last.
  void keep_steps() { _keepSteps = true; }
  // Recovery keeps even tail calls: leaving one must name the exact call,
  // and its cost depends on how many author tokens it has read.
  void track_calls() {
    _keepCalls = true;
    LlReturnPool::read_returns_by_class(nullptr);
  }
  void at_token(std::int64_t token) { _callFrom = token; }
  std::uint32_t push_return(std::uint32_t ctx, std::uint32_t follow) {
    if (!_keepCalls) return LlReturnPool::push_return(ctx, follow);
    return follow == kNoFollow
               ? ctx
               : _ctx.push(ctx, follow & ~kLlTailReturn, _callFrom);
  }

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

  // ...and the contexts go too, which is what a walk that keeps no set
  // between calls owes: the pool would otherwise grow with the document.
  void forget() {
    reset();
    _ctx.clear();
    _kept.clear();
    _keptConfigs.clear();
    _keptTerms.clear();
    _keptIndex.clear();
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
  bool leave(std::uint32_t depth, std::uint32_t ret) {
    std::vector<LlConfig> from = _cur;
    _next.clear();
    forget_visits();
    for (const LlConfig& c : from) {
      std::vector<std::uint32_t> level{c.ctx}, next;
      for (std::uint32_t d = 1; d <= depth; ++d) {
        next.clear();
        for (const std::uint32_t ctx : level) {
          if (ctx == kLlCtxNone) continue;
          for (unsigned j = 0; j < _ctx.count(ctx); ++j) {
            const std::uint32_t parent = _ctx.parent(ctx, j);
            if (d == depth) {
              const std::uint32_t to = _ctx.ret(ctx, j);
              if (_atn.same_return(to, ret))
                closure(LlConfig{to, parent, c.alt}, _next);
            } else if (std::ranges::find(next, parent) == next.end()) {
              next.push_back(parent);
            }
          }
        }
        level.swap(next);
      }
    }
    _cur.swap(_next);
    std::sort(_cur.begin(), _cur.end());
    return !_cur.empty();
  }

  // The closure at one state, not past it: what a caret asks, where the
  // question is what may be read HERE and not which arm was taken.
  void seed(std::uint32_t state, std::uint32_t stack) {
    reset();
    closure(LlConfig{.state = state, .ctx = stack, .alt = 0}, _next);
    _cur.swap(_next);
  }

  // The arms of one decision, each entered through the edge that names it.
  void start(std::uint32_t decisionState, std::uint32_t stack) {
    reset();
    const LlAtnState& s = _atn.state(decisionState);
    for (std::uint32_t i = 0; i < s.nedges; ++i)
      feed(LlConfig{.state = _atn.edge(s.edge + i).target,
                    .ctx = stack,
                    .alt = static_cast<std::uint16_t>(i)});
    settle();
    _cur.swap(_next);
  }

  // Advance every configuration that can read this token. A spelling several
  // terminals match is several terminals: a keyword guarded against running
  // into a word and the bare literal of the same text are two of them, and an
  // arm that wants one must not die because another was named first. False
  // when no reading survives, which is a choice no arm answers.
  bool step(const std::uint16_t* terms, std::size_t n) {
    if (!_keepSteps) return advance(Labelled{terms, n, labels(terms, n)});
    return kept_step(terms, n);
  }
  bool step(std::uint16_t term) { return step(&term, 1); }

  // The step a caret replays over a token it did not read itself: the edges
  // `takes(edge index)` names, whatever they carry -- an implied token is read
  // across the condition that stands for it.
  template <class Takes>
  bool step_edges(Takes&& takes) {
    return advance(
        [&](std::uint32_t at, const LlAtnEdge&) { return takes(at); });
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

  // The next token is also needed to evaluate a pending condition. Traverse
  // those conditions speculatively to find its alphabet, without changing the
  // configurations that the real filter will retain. A choice's wider lexical
  // mode could otherwise reserve a keyword that has no transition here.
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
  std::uint16_t settled() const {
    if (_cur.empty()) return kNoRow;
    const std::uint16_t a = _cur.front().alt;
    for (const LlConfig& c : _cur)
      if (c.alt != a) return kNoRow;
    return a;
  }

  // ...and the lowest arm still standing, which is what a walk that runs out
  // of input or of patience answers: the arms are ordered, and the first that
  // admits the text is the reading the grammar states.
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

  // The set, the terminals and the pool decide a step, and the pool holds
  // every context a step walked before made: that step is answered as it was.
  bool kept_step(const std::uint16_t* terms, std::size_t n) {
    std::uint64_t h = 1469598103934665603ull ^ (_truncated ? 1u : 0u);
    const std::int64_t from = _keepCalls ? _callFrom : -1;
    h = (h ^ static_cast<std::uint64_t>(from)) * 1099511628211ull;
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
            e.truncated != _truncated || e.callFrom != from ||
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
    e.callFrom = from;
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
    const bool read = advance(Labelled{terms, n, labels(terms, n)});
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
                                           std::size_t n) {
    if (n <= kLinearLabels) return nullptr;
    _labels.assign(*std::max_element(terms, terms + n) / 64u + 1u, 0u);
    for (std::size_t m = 0; m < n; ++m)
      _labels[terms[m] >> 6] |= std::uint64_t{1} << (terms[m] & 63u);
    return &_labels;
  }

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
    settle();
    _cur.swap(_next);
    return !_cur.empty();
  }

  // A reading already standing at this position with another stack is the
  // same reading with a forked stack: the two join, and the walk carries on
  // only where the join widened what was already covered. Where they cannot
  // join -- one of them has no caller to name -- both are kept.
  // Depth first, on a stack of its own: a reading that returns through every
  // caller of a deep document walks as far as the document nests. A call's
  // context is pushed when it is visited, in the order a recursion would.
  bool closure(const LlConfig& c, std::vector<LlConfig>& out,
               std::vector<LlConfig>* conditions = nullptr) {
    _pending.clear();
    _pending.push_back({c, kNoFollow});
    const bool was = std::exchange(_immediate, true);
    const bool done = drain(out, conditions);
    _immediate = was;
    return done;
  }

  // A walk that shares epsilon positions takes all its sources before it
  // expands any, so the contexts that meet at a position leave it once. Where
  // two stacks cannot join, the seats they split into depend on the order the
  // readings came in, and a worse split costs the steps after it: the sources
  // are then walked one at a time too, and that walk's seats win if it ends
  // within a few times the first one's work.
  void feed(const LlConfig& c) {
    if (!_shareEpsilon) {
      closure(c, _next);
    } else {
      _sources.push_back(c);
      _pending.push_back({c, kNoFollow});
    }
  }
  void settle() {
    if (!_shareEpsilon) return;
    _split = false;
    _visits = 0;
    _stopAtSplit = true;
    drain(_next);
    _stopAtSplit = false;
    // The first walk stops where the stacks part: the one-at-a-time walk wins
    // when it ends within a few times what the first had read by then, and
    // the first is read to its end only when it does not.
    if (_split && !one_at_a_time(kSerialRatio * _visits + kSerialRoom)) {
      _split = false;
      _visits = 0;
      _next.clear();
      forget_visits();
      for (const LlConfig& c : _sources) _pending.push_back({c, kNoFollow});
      drain(_next);
      std::vector<LlConfig> joined = std::move(_next);
      if (!one_at_a_time(kSerialRatio * _visits + kSerialRoom))
        _next = std::move(joined);
    }
    forget_deltas();
    _sources.clear();
    // One order for one set, so the step kept for it is found again.
    std::sort(_next.begin(), _next.end());
  }

  // Whether every source ends within `budget` visits.
  bool one_at_a_time(std::uint64_t budget) {
    _budget = budget;
    _next.clear();
    forget_visits();
    _visits = 0;
    bool done = true;
    for (const LlConfig& c : _sources)
      if (!(done = closure(c, _next))) break;
    _budget = 0;
    return done;
  }

  bool drain(std::vector<LlConfig>& out,
             std::vector<LlConfig>* conditions = nullptr) {
    for (;;) {
      while (!_pending.empty() && !_capped && !(_stopAtSplit && _split)) {
        Pending next = _pending.back();
        _pending.pop_back();
        next.config.ctx = push_return(next.config.ctx, next.follow);
        visit(next.config, out, conditions);
        if (_immediate && _budget != 0 && _visits > _budget) return false;
      }
      if (_ready.empty() || _capped || (_stopAtSplit && _split)) return true;
      std::pop_heap(_ready.begin(), _ready.end(), std::greater<>{});
      const auto at = static_cast<std::uint32_t>(_ready.back());
      _ready.pop_back();
      const LlConfig held{
          .state = out[at].state, .ctx = _delta[at], .alt = out[at].alt};
      _delta[at] = kLlCtxApart;
      expand(held);
    }
  }

  // Where a state stands in a reverse postorder of the graph a closure walks:
  // a seat expands once everything that reaches it from lower down has arrived.
  void rank_states() {
    const std::uint32_t n = _atn.nstates;
    _rank.assign(n, 0);
    std::vector<std::uint8_t> seen(n, 0);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> path;
    std::uint32_t post = 0;
    for (std::uint32_t root = 0; root < n; ++root) {
      if (seen[root]) continue;
      seen[root] = 1;
      path.emplace_back(root, 0);
      while (!path.empty()) {
        const auto [state, from] = path.back();
        const LlAtnState& q = _atn.state(state);
        std::uint32_t next = kNoFollow, at = from;
        if (q.kind == LlAtnKind::ruleStop) {
          const std::uint32_t base = _atn.callerBase[q.rule];
          if (base + at < _atn.callerBase[q.rule + 1])
            next = _atn.callerAt[base + at++];
        } else {
          while (at < q.nedges && next == kNoFollow) {
            const LlAtnEdge& e = _atn.edge(q.edge + at++);
            if (e.kind == LlEdgeKind::epsilon || e.kind == LlEdgeKind::rule)
              next = e.target;
          }
        }
        path.back().second = at;
        if (next == kNoFollow) {
          _rank[state] = n - 1 - post++;
          path.pop_back();
        } else if (!seen[next]) {
          seen[next] = 1;
          path.emplace_back(next, 0);
        }
      }
    }
  }

  void forget_visits() {
    _seen.clear();
    _at.clear();
    _seatNext.clear();
    forget_deltas();
  }

  void forget_deltas() {
    _pending.clear();
    _delta.clear();
    _ready.clear();
  }

  // The contexts that reached a position and have not yet left it.
  void defer(std::uint32_t at, const LlConfig& cur, std::size_t seats) {
    if (_delta.size() < seats) _delta.resize(seats, kLlCtxApart);
    std::uint32_t& held = _delta[at];
    if (held == kLlCtxApart) {
      held = cur.ctx;
      _ready.push_back(std::uint64_t{_rank[cur.state]} << 32 | at);
      std::push_heap(_ready.begin(), _ready.end(), std::greater<>{});
      return;
    }
    const std::uint32_t joined = _ctx.merge(held, cur.ctx);
    if (joined == kLlCtxApart) {
      _split = true;
      expand(cur);
    } else {
      held = joined;
    }
  }

  void visit(const LlConfig& cur, std::vector<LlConfig>& out,
             std::vector<LlConfig>* conditions = nullptr) {
    ++_visits;
    ++_total;
    const LlAtnState& s = _atn.state(cur.state);
    bool ready = s.kind == LlAtnKind::ruleStop && cur.ctx == kLlCtxNone;
    bool conditional = false;
    if (!_shareEpsilon || conditions != nullptr)
      for (std::uint32_t i = 0; i < s.nedges; ++i) {
        const auto kind = _atn.edge(s.edge + i).kind;
        conditional = conditional || kind == LlEdgeKind::condition;
        ready =
            ready || kind == LlEdgeKind::atom || kind == LlEdgeKind::condition;
      }
    bool first = false, joined = false;
    std::uint32_t* seat = nullptr;
    std::uint32_t where = 0;
    const bool shared = ready || _shareEpsilon;
    if (shared) {
      const std::uint64_t key =
          (static_cast<std::uint64_t>(cur.state) << 16) | cur.alt;
      seat = &_at.at(key, first);
      if (!first) {
        for (std::uint32_t at = *seat; at != kNoFollow;
             at = _shareEpsilon ? _seatNext[at] : kNoFollow) {
          const std::uint32_t m = _ctx.merge(out[at].ctx, cur.ctx);
          if (m == kLlCtxApart) {
            _split =
                _split || (out[at].ctx != kLlCtxNone && cur.ctx != kLlCtxNone);
            continue;
          }
          if (m == out[at].ctx) return;
          out[at].ctx = m;
          // Publish the union, traverse only the new contexts. Only repair
          // weighing joins at epsilon positions: elsewhere joining stacks of
          // different depth is quadratic.
          joined = true;
          where = at;
          break;
        }
      }
    }
    // A first shared state is necessarily new. A context union that widened
    // its published state necessarily contributes new returns. Only the
    // unmergeable arrivals and unshared bookkeeping need a second set.
    if (!shared || (!first && !joined)) {
      bool fresh = false;
      _seen.at(cur, fresh);
      if (!fresh) return;
    }
    if (_limit != 0 && ++_visited > _limit) {
      _capped = true;
      return;
    }
    if (!shared) {
      out.push_back(cur);
    } else if (first || !joined) {
      const auto at = static_cast<std::uint32_t>(out.size());
      where = at;
      if (_shareEpsilon) {
        _seatNext.resize(out.size() + 1, kNoFollow);
        _seatNext[at] = first ? kNoFollow : *seat;
      }
      if (first || _shareEpsilon) *seat = at;
      out.push_back(cur);
    }
    if (conditions != nullptr && conditional) conditions->push_back(cur);
    if (_shareEpsilon && !_immediate)
      defer(where, cur, out.size());
    else
      expand(cur);
  }

  void expand(const LlConfig& cur) {
    const LlAtnState& s = _atn.state(cur.state);
    if (s.kind == LlAtnKind::ruleStop) {
      if (cur.ctx != kLlCtxNone) {
        // Each return a reduction can take is a stack of its own.
        for (unsigned k = _ctx.count(cur.ctx); k-- > 0;)
          _pending.push_back({LlConfig{.state = _ctx.ret(cur.ctx, k),
                                       .ctx = _ctx.parent(cur.ctx, k),
                                       .alt = cur.alt},
                              kNoFollow});
      } else {
        const std::uint32_t from = _atn.callerBase[s.rule];
        _unbound =
            _unbound || (_truncated && _atn.callerBase[s.rule + 1] != from);
        for (std::uint32_t i = _atn.callerBase[s.rule + 1]; i-- > from;)
          _pending.push_back({LlConfig{.state = _atn.callerAt[i],
                                       .ctx = kLlCtxNone,
                                       .alt = cur.alt},
                              kNoFollow});
      }
      return;
    }
    for (std::uint32_t i = s.nedges; i-- > 0;) {
      const LlAtnEdge& e = _atn.edge(s.edge + i);
      if (e.kind == LlEdgeKind::epsilon)
        _pending.push_back(
            {LlConfig{.state = e.target, .ctx = cur.ctx, .alt = cur.alt},
             kNoFollow});
      else if (e.kind == LlEdgeKind::rule)
        _pending.push_back(
            {LlConfig{.state = e.target, .ctx = cur.ctx, .alt = cur.alt},
             return_of(e.follow)});
    }
  }

  static constexpr std::size_t kLinearLabels = 8;
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
    std::int64_t callFrom = -1;
    std::uint32_t next = 0;
    std::uint32_t from = 0, fromSize = 0, terms = 0, termCount = 0;
    std::uint32_t to = 0, toSize = 0;
    bool unbound = false, truncated = false;
  };
  std::vector<Kept> _kept;
  std::vector<LlConfig> _keptConfigs;
  std::vector<std::uint16_t> _keptTerms;
  LlStamped<std::uint64_t, LlKeyHash> _keptIndex;
  bool _keepCalls = false;
  std::int64_t _callFrom = 0;

  const LlAtnView& _atn;
  std::vector<LlConfig> _cur, _next;
  LlStamped<LlConfig, LlConfigHash> _seen;
  // Where a reading of one position and one arm already stands, so a second
  // route to it joins instead of doubling the set.
  LlStamped<std::uint64_t, LlKeyHash> _at;
  // Every compatible union at a position, for repair weighing: the first
  // stack seated there cannot keep the others apart.
  std::vector<std::uint32_t> _seatNext;
  // Per seat of a walk that shares epsilon positions: the contexts that
  // arrived since it last expanded, and the seats waiting to expand.
  std::vector<std::uint32_t> _delta;
  // (rank, seat) as one word: the seats leave in the order of the ranks.
  std::vector<std::uint64_t> _ready;
  std::vector<std::uint32_t> _rank;
  std::vector<std::uint64_t> _labels;
  std::vector<LlConfig> _sources;
  bool _immediate = false, _split = false, _stopAtSplit = false;
  std::uint64_t _visits = 0, _budget = 0, _total = 0;
  std::vector<LlConfig> _conditions;
  std::uint32_t _limit = 0, _visited = 0;
  bool _capped = false, _truncated = false, _unbound = false;
  bool _shareEpsilon = false;
  bool _keepSteps = false;
};

}  // namespace cursiv
