// The readings of an episode and their closure over the empty moves, best
// first: the first to reach a state with a stack is the reading the grammar
// prefers, and any other that reaches it has a worse past and the same future.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/engines/ll/runtime/LlStackSet.hpp"
#include "cursiv/engines/ll/runtime/LlThread.hpp"
#include "cursiv/engines/ll/table/LlAtnView.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"

namespace cursiv {

struct LlThreadKey {
  std::uint32_t state, stack, tag, root;
  bool operator==(const LlThreadKey&) const = default;
};

struct LlThreadKeyHash {
  std::size_t operator()(const LlThreadKey& k) const noexcept {
    std::uint64_t h = k.state;
    h = (h * 1099511628211ull) ^ k.stack;
    h = (h * 1099511628211ull) ^ k.tag;
    h = (h * 1099511628211ull) ^ k.root;
    return static_cast<std::size_t>(h);
  }
};

// The word of a return that goes to a caller the episode only guesses, which no
// decision names: its decision is the one that names none.
inline constexpr std::uint32_t kLlBlindWord = 0xFFFF0000u;

class LlEpisodeSim {
 public:
  // `pool` holds the stacks of the descent and the ones the episode makes. A
  // tracked episode keeps the rules each reading completed, and the readings
  // that share a closure, as the lexer reads an LR state's items.
  LlEpisodeSim(const LlAtnView& atn, LlReturnPool& pool, bool tracked)
      : _atn(atn), _pool(pool), _tracked(tracked) {}

  // The decisions whose arms rank before any lexical choice, a flag each, or
  // none where there are none.
  void rank_arms_over_lexes(const std::uint8_t* scoped) { _scoped = scoped; }

  // The rules, flagged by rule, whose callers share its body. Unset, none.
  void share_calls(const std::uint8_t* shared) { _shares = shared; }

  LlHistory& arms() { return _arms; }
  const LlHistory& arms() const { return _arms; }
  LlHistory& lexes() { return _lexes; }
  LlStackSet& stacks() { return _stacks; }
  const LlStackSet& stacks() const { return _stacks; }

  // Whether the run `tag` is the run `of` or one it went on from.
  bool reduced_from(std::uint32_t tag, std::uint32_t of) const {
    for (; tag != 0 && tag != kLlNoReduce; tag = _reduceUp[tag])
      if (tag == of) return true;
    return false;
  }

  // Where the text stands for the readings closed next, which is where the
  // decisions they pass are taken, and how many tokens of no width have been
  // read there.
  void at(std::uint32_t position, std::uint32_t lull = 0) {
    _pos = position;
    _lull = lull;
  }

  // Between two episodes.
  void reset() {
    _arms.clear();
    _lexes.clear();
    _reduces.clear();
    _reduceUp.assign(1, 0);
    _reduceState.assign(1, 0);
    _stacks.clear();
    _below.clear();
    _relatedProcessed.clear();
    _relatedFlags.clear();
    _counts.clear();
    _heads.clear();
    _stack.clear();
    _words.clear();
    _side.clear();
    _shared.clear();
    _joins.clear();
    _log = nullptr;
  }

  // ...and an episode that grew large gives its room back.
  void trim() {
    _arms.trim();
    _lexes.trim();
    _reduces.trim();
    _seen.trim();
    _at.trim();
    _held.trim();
    _crossed.trim();
    _calls.trim();
    _flat.trim();
    _below.trim();
    _relatedProcessed.trim();
    _relatedFlags.trim();
  }

  // Whether one reading precedes another: the lexical choices first, then the
  // decisions, each by the first word where they part -- except where the
  // readings part at a decision that ranks its arms over them.
  bool earlier(const LlThread& a, const LlThread& b) const {
    return earlier(a.arm, a.lex, b.arm, b.lex);
  }
  bool earlier(LlHistory::Node armA, LlHistory::Node lexA, LlHistory::Node armB,
               LlHistory::Node lexB) const {
    if (lexA != lexB) {
      if (ranks_arms(armA, armB)) return _arms.before(armA, armB);
      return _lexes.before(lexA, lexB);
    }
    return armA != armB && _arms.before(armA, armB);
  }

  // The kernels two readings were found to share, since the last call.
  std::vector<std::pair<std::uint32_t, std::uint32_t>>& joins() {
    return _joins;
  }

  // The arms of a decision, each entered through the edge that names it, and
  // closed; `visited` lists every state the closure met, with its arm.
  void seed(
      std::uint32_t decisionState, std::uint32_t stack,
      std::vector<LlThread>& out,
      std::vector<std::pair<std::uint32_t, std::uint16_t>>* visited = nullptr) {
    _log = visited;
    _origins.clear();
    const LlAtnState& s = _atn.state(decisionState);
    for (std::uint32_t i = 0; i < s.nedges; ++i)
      _origins.push_back(LlThread{.state = _atn.edge(s.edge + i).target,
                                  .stack = stack,
                                  .arm = word(LlHistory::kRoot, s, i),
                                  .root = static_cast<std::uint16_t>(i),
                                  .kernel = kStart});
    close(_origins, out);
    _log = nullptr;
  }

  // The readings the origins grow into, closed over the empty moves, best
  // first. The origins come best first themselves: each is closed before the
  // next, and what a reading goes on to read is read in the order of its
  // edges, so the order of the readings needs no comparing. Only a reading
  // that returns past a cell leaves it, and those wait their turn.
  void close(const std::vector<LlThread>& origins, std::vector<LlThread>& out) {
    out.clear();
    _seen.clear();
    _at.clear();
    _calls.clear();
    _relatedProcessed.clear();
    _relatedFlags.clear();
    _stack.clear();
    _words.clear();
    _side.clear();
    _base = _stacks.size();
    std::size_t next = 0;
    for (;;) {
      const LlThread* from = nullptr;
      std::uint32_t word = kNoWord;
      bool onStack = false;
      if (!_stack.empty()) {
        from = &_stack.back();
        word = _words.back();
        onStack = true;
      } else if (next < origins.size()) {
        from = &origins[next];
      }
      if (!_side.empty()) {
        LlThread seen = from != nullptr ? *from : LlThread{};
        if (from != nullptr && word != kNoWord)
          seen.arm = _arms.append(seen.arm, word, _pos, _lull);
        if (from == nullptr || earlier(_side.front().thread, seen)) {
          std::pop_heap(_side.begin(), _side.end(),
                        [this](auto& a, auto& b) { return later(a, b); });
          LlThread t = _side.back().thread;
          _side.pop_back();
          visit(t, kNoWord, out);
          continue;
        }
      }
      if (from == nullptr) break;
      LlThread t = *from;
      if (onStack) {
        _stack.pop_back();
        _words.pop_back();
      } else {
        ++next;
      }
      visit(t, word, out);
    }
    _flat.clear();
    for (LlThread& t : out) t.stack = flat(t.stack);
    for (std::size_t k = _base; k < _stacks.size(); ++k) {
      const auto cell = LlStackSet::kCellBit | static_cast<std::uint32_t>(k);
      for (auto& m : _stacks.members(cell)) m.stack = flat(m.stack);
      _stacks.seal(cell);
    }
  }

  // The readings that take `terms`, each at the state its edge leads to, to be
  // closed where the token ends.
  void advance(const LlThread& t, const std::uint16_t* terms, std::size_t n,
               LlHistory::Node lex, std::uint32_t kernel,
               std::vector<LlThread>& origins) const {
    const LlAtnState& s = _atn.state(t.state);
    for (std::uint32_t i = 0; i < s.nedges; ++i) {
      const LlAtnEdge& e = _atn.edge(s.edge + i);
      if (e.kind != LlEdgeKind::atom) continue;
      for (std::size_t m = 0; m < n; ++m)
        if (e.label == terms[m]) {
          origins.push_back(LlThread{.state = e.target,
                                     .stack = t.stack,
                                     .arm = t.arm,
                                     .lex = lex,
                                     .root = t.root,
                                     .kernel = kernel});
          break;
        }
    }
  }

  // How many readings the stack of a thread stands for, counted up to two:
  // one, or one for each stack of the cell its frames stand over.
  unsigned standing(std::uint32_t stack) {
    const std::uint32_t cell = below(stack);
    if (cell == kLlCtxNone) return 1;
    const std::size_t at = LlStackSet::index(cell);
    if (_counts.size() <= at) _counts.resize(at + 1, 0);
    if (_counts[at] == 0)
      for (const auto& m : _stacks.members(cell)) {
        const unsigned n = standing(m.stack);
        _counts[at] = static_cast<std::uint8_t>(std::min(2u, _counts[at] + n));
        if (_counts[at] == 2) break;
      }
    return _counts[at];
  }

  // ...and the arms of those readings: each with the decisions of its own and
  // then the ones taken since. `visit` stops the walk by answering false.
  template <class Visit>
  bool each_arm(std::uint32_t stack, LlHistory::Node arm, Visit&& visit) {
    const Graft top{nullptr, arm, arm};
    return each_arm(stack, top, visit);
  }

  // What `common` and every reading of a thread share, folded into `common`
  // from the first reading `met`: false once it is no deeper than `floor`,
  // and the walk stops. A reading is grafted only where `common` runs past
  // the arm it starts with, and a cell is met, unwalked, by the prefix its
  // readings' arms share where they part right there or `common` parts from
  // it first.
  bool meet_arms(std::uint32_t stack, LlHistory::Node arm,
                 LlHistory::Node& common, bool& met, std::uint32_t floor) {
    const Graft top{nullptr, arm, arm};
    return meet_arms(stack, top, common, met, floor);
  }

 private:
  // A reading's arm, made once some reading below asks for it: its own head
  // past which what the reading above took after `from` is grafted.
  struct Graft {
    const Graft* above;
    LlHistory::Node head, from;
    mutable LlHistory::Node made = kUnmade;
  };
  static constexpr LlHistory::Node kUnmade = ~LlHistory::Node{0};

  LlHistory::Node made(const Graft& g) {
    if (g.made == kUnmade)
      g.made = g.above == nullptr ? g.head
                                  : _arms.graft(g.head, g.from, made(*g.above));
    return g.made;
  }

  template <class Visit>
  bool each_arm(std::uint32_t stack, const Graft& g, Visit& visit) {
    const std::uint32_t cell = below(stack);
    if (cell == kLlCtxNone) return visit(made(g));
    const auto& members = _stacks.members(cell);
    for (const auto& m : members) {
      const Graft next{&g, m.arm, members[0].arm};
      if (!each_arm(m.stack, next, visit)) return false;
    }
    return true;
  }

  // The prefix the arms every reading below a cell starts with share, and the
  // depth of the shortest of those arms: where each runs past the prefix, the
  // readings part right there, whatever was taken above them.
  struct Heads {
    LlHistory::Node shared = kUnmade;
    std::uint32_t shortest = 0;
    // `single`: every arm is `shared`. `unsettled`: `shared` is no more than
    // a prefix of them all, since one arm ends there while others go on, and
    // what comes after the cell says whether they still agree.
    bool single = false, unsettled = false;
  };
  // A member's arms are those of the cell its stack stands on, with what the
  // member added to that cell's lead put after each.
  Heads heads(std::uint32_t cell) {
    const std::size_t at = LlStackSet::index(cell);
    if (_heads.size() <= at) _heads.resize(at + 1);
    if (_heads[at].shared == kUnmade) {
      Heads all;
      for (const auto& m : _stacks.members(cell)) {
        const std::uint32_t next = below(m.stack);
        Heads h{m.arm, _arms.depth(m.arm), true, false};
        if (next != kLlCtxNone) {
          const Heads lower = heads(next);
          const LlHistory::Node lead = _stacks.members(next)[0].arm;
          const std::uint32_t added = _arms.depth(m.arm) - _arms.depth(lead);
          if (_arms.depth(m.arm) < _arms.depth(lead) ||
              _arms.ancestor(m.arm, _arms.depth(lead)) != lead) {
            h = {LlHistory::kRoot, 0, false, true};
          } else if (!lower.unsettled &&
                     lower.shortest > _arms.depth(lower.shared)) {
            h = {lower.shared, lower.shortest + added, false, false};
          } else if (!lower.unsettled && lower.single) {
            const LlHistory::Node s = _arms.graft(lower.shared, lead, m.arm);
            h = {s, _arms.depth(s), true, false};
          } else {
            h = {lower.shared, lower.shortest + added, false, true};
          }
        }
        if (all.shared == kUnmade) {
          all = h;
        } else {
          all.unsettled = all.unsettled || h.unsettled;
          all.single = !all.unsettled && all.single && h.single &&
                       all.shared == h.shared;
          all.shared = _arms.common(all.shared, h.shared);
          all.shortest = std::min(all.shortest, h.shortest);
        }
      }
      _heads[at] = all;
    }
    return _heads[at];
  }

  bool meet_arms(std::uint32_t stack, const Graft& g, LlHistory::Node& common,
                 bool& met, std::uint32_t floor) {
    const std::uint32_t cell = below(stack);
    if (cell == kLlCtxNone) {
      if (!met) {
        common = made(g);
        met = true;
      } else if (const LlHistory::Node c = _arms.common(common, g.head);
                 _arms.depth(c) < _arms.depth(g.head)) {
        common = c;
      } else {
        common = _arms.common(common, made(g));
      }
      return _arms.depth(common) > floor;
    }
    if (const Heads h = heads(cell); h.shared != kUnmade) {
      const std::uint32_t depth = _arms.depth(h.shared);
      LlHistory::Node meets = kUnmade;
      if (!h.unsettled && h.shortest > depth) {
        meets = met ? _arms.common(common, h.shared) : h.shared;
      } else if (!h.unsettled && h.single) {
        const LlHistory::Node whole =
            _arms.graft(h.shared, _stacks.members(cell)[0].arm, made(g));
        meets = met ? _arms.common(common, whole) : whole;
      } else if (met) {
        if (const LlHistory::Node c = _arms.common(common, h.shared);
            _arms.depth(c) < depth)
          meets = c;
      }
      if (meets != kUnmade) {
        common = meets;
        met = true;
        return _arms.depth(common) > floor;
      }
    }
    const auto& members = _stacks.members(cell);
    for (const auto& m : members) {
      const Graft next{&g, m.arm, members[0].arm};
      if (!meet_arms(m.stack, next, common, met, floor)) return false;
    }
    return true;
  }

 public:
  // The edges a crossing keeps back: none.
  struct NoStay {
    bool operator()(std::uint32_t) const { return false; }
  };

  // Whether a reading waits on a condition.
  bool needs_filter(const std::vector<LlThread>& threads) const {
    for (const LlThread& t : threads)
      if (_atn.has(t.state, LlEdgeKind::condition)) return true;
    return false;
  }

  // The conditions `accepts` takes are crossed, and what stands past them is
  // read with what stood there; the readings still waiting are not kept.
  template <class F>
  bool filter(std::vector<LlThread>& threads, F&& accepts) {
    std::vector<LlThread> everyone = threads;
    expand(everyone, accepts);
    threads.clear();
    for (const LlThread& t : everyone)
      if (ready_to_read(t)) threads.push_back(t);
    std::stable_sort(
        threads.begin(), threads.end(),
        [this](const LlThread& a, const LlThread& b) { return earlier(a, b); });
    // The same reading reached twice is the better one.
    _held.clear();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < threads.size(); ++i) {
      bool fresh = false;
      std::uint32_t& owner = _held.at(key(threads[i]), fresh);
      if (!fresh) {
        join(owner, threads[i].kernel);
        continue;
      }
      owner = threads[i].kernel;
      threads[kept++] = threads[i];
    }
    threads.resize(kept);
    return !threads.empty();
  }

  // The terminals the readings may read once the conditions they wait on are
  // crossed.
  template <class F, class S = NoStay>
  void possible_terms(const std::vector<LlThread>& threads, F&& read,
                      S&& stays = {}) {
    std::vector<LlThread> everyone = threads;
    expand(everyone, [](std::uint16_t, std::uint32_t) { return true; }, stays);
    for (const LlThread& t : everyone) {
      const LlAtnState& s = _atn.state(t.state);
      for (std::uint32_t i = 0; i < s.nedges; ++i) {
        const LlAtnEdge& e = _atn.edge(s.edge + i);
        if (e.kind == LlEdgeKind::atom) read(e.label);
      }
    }
  }

  static constexpr std::uint32_t kStart = ~std::uint32_t{0};

 private:
  struct Pending {
    LlThread thread;
    std::uint32_t seq;
  };

  bool later(const Pending& a, const Pending& b) const {
    if (earlier(b.thread, a.thread)) return true;
    if (earlier(a.thread, b.thread)) return false;
    return a.seq > b.seq;
  }

  static constexpr std::uint32_t kNoWord = ~std::uint32_t{0};

  void push(const LlThread& t, std::uint32_t word = kNoWord) {
    _stack.push_back(t);
    _words.push_back(word);
  }

  void push_later(const LlThread& t) {
    _side.push_back({t, _seq++});
    std::push_heap(_side.begin(), _side.end(),
                   [this](auto& a, auto& b) { return later(a, b); });
  }

  void join(std::uint32_t a, std::uint32_t b) {
    if (_tracked && a != b) _joins.emplace_back(a, b);
  }

  // The word a decision's edge adds; none where nothing is chosen.
  LlHistory::Node word(LlHistory::Node arm, const LlAtnState& s,
                       std::uint32_t edge) {
    const std::uint32_t w = word_of(s, edge);
    return w == kNoWord ? arm : _arms.append(arm, w, _pos, _lull);
  }
  static std::uint32_t word_of(const LlAtnState& s, std::uint32_t edge) {
    if (s.decision == kNoRow || s.nedges < 2) return kNoWord;
    return (std::uint32_t{s.decision} << 16) | edge;
  }

  bool ranks_arms(LlHistory::Node a, LlHistory::Node b) const {
    std::uint32_t wordA = 0, wordB = 0;
    if (_scoped == nullptr || !_arms.parts(a, b, wordA, wordB)) return false;
    const std::uint32_t x = wordA >> 16;
    return x == wordB >> 16 && x < kNoRow && _scoped[x] != 0;
  }

  static LlThreadKey key(const LlThread& t) {
    return {t.state, t.stack, t.tag, t.root};
  }

  bool ready_to_read(const LlThread& t) const {
    return (_atn.state(t.state).kind == LlAtnKind::ruleStop &&
            t.stack == kLlCtxNone) ||
           _atn.has(t.state, LlEdgeKind::atom);
  }

  // The readings past the conditions `accepts` takes, after those that were
  // there: each reading is crossed from once, and every way to a reading is
  // kept for the best to be chosen.
  template <class F, class S = NoStay>
  void expand(std::vector<LlThread>& everyone, F&& accepts, S&& stays = {}) {
    std::vector<LlThread> origins, grown;
    _crossed.clear();
    for (std::size_t at = 0; at < everyone.size();) {
      origins.clear();
      for (; at < everyone.size(); ++at) {
        const LlThread t = everyone[at];
        bool fresh = false;
        _crossed.at(key(t), fresh);
        if (!fresh) continue;
        const LlAtnState& s = _atn.state(t.state);
        for (std::uint32_t i = 0; i < s.nedges; ++i) {
          const LlAtnEdge& e = _atn.edge(s.edge + i);
          if (e.kind == LlEdgeKind::condition &&
              accepts(e.label, s.edge + i) && !stays(s.edge + i))
            origins.push_back(LlThread{.state = e.target,
                                       .stack = t.stack,
                                       .arm = t.arm,
                                       .lex = t.lex,
                                       .tag = t.tag,
                                       .root = t.root,
                                       .kernel = t.kernel});
        }
      }
      if (origins.empty()) break;
      close(origins, grown);
      everyone.insert(everyone.end(), grown.begin(), grown.end());
    }
  }

  // `p` stands one word short of where it goes: the word is laid down only for
  // a reading that is not met twice.
  void visit(LlThread& p, std::uint32_t pending, std::vector<LlThread>& out) {
    const LlAtnState& s = _atn.state(p.state);
    bool fresh = false;
    std::uint32_t& owner = _seen.at(key(p), fresh);
    if (!fresh) {
      join(owner, p.kernel);
      return;
    }
    owner = p.kernel;
    if (pending != kNoWord) p.arm = _arms.append(p.arm, pending, _pos, _lull);
    if (_log != nullptr) _log->emplace_back(p.state, p.root);
    bool ready = s.kind == LlAtnKind::ruleStop && p.stack == kLlCtxNone;
    for (std::uint32_t i = 0; i < s.nedges; ++i) {
      const auto kind = _atn.edge(s.edge + i).kind;
      ready =
          ready || kind == LlEdgeKind::atom || kind == LlEdgeKind::condition;
    }
    if (ready) {
      bool first = false;
      std::uint32_t& seat =
          _at.at(LlThreadKey{p.state, 0, p.tag, p.root}, first);
      if (first) {
        seat = static_cast<std::uint32_t>(out.size());
        out.push_back(p);
      } else if (joinable(out[seat], p)) {
        fork(out[seat], p);
        join(out[seat].kernel, p.kernel);
        return;
      } else {
        out.push_back(p);
        return;
      }
    }
    if (s.kind == LlAtnKind::ruleStop) {
      leave(p, s);
      return;
    }
    for (std::uint32_t i = s.nedges; i-- > 0;) {
      const LlAtnEdge& e = _atn.edge(s.edge + i);
      LlThread q = p;
      if (e.kind == LlEdgeKind::epsilon) {
        q.state = e.target;
        push(q, word_of(s, i));
      } else if (e.kind == LlEdgeKind::rule) {
        q.state = e.target;
        q.stack = _pool.push_return(p.stack, _pool.return_of(e.follow));
        if (call(q)) push(q);
      }
    }
  }

  // The callers of a shared rule read its body once, over a cell of their
  // stacks, however many ways they stand: false for a caller that joined the
  // cell another opened.
  bool call(LlThread& q) {
    if (_shares == nullptr || _shares[_atn.state(q.state).rule] == 0)
      return true;
    bool fresh = false;
    std::uint32_t& cell = _calls.at(
        LlThreadKey{q.state, _tracked ? q.kernel : 0, q.tag, q.root}, fresh);
    if (fresh) {
      cell = _stacks.open({q.stack, q.arm, q.lex, q.root});
      q.stack = cell;
      return true;
    }
    if (!_stacks.holds(cell, q.stack))
      _stacks.add(cell, {q.stack, q.arm, q.lex, q.root});
    return false;
  }

  // A stack without the cells that hold a single stack: the frames over such
  // a cell stand over that stack, so that a rule entered again and again
  // leaves no chain of them behind.
  std::uint32_t flat(std::uint32_t stack) {
    _frames.clear();
    std::uint32_t at = stack, base = kLlCtxNone;
    bool changed = false;
    while (at != kLlCtxNone) {
      if (const std::uint32_t* known = _flat.find(at)) {
        base = *known;
        changed = changed || base != at;
        break;
      }
      if (LlStackSet::is_cell(at)) {
        const auto& members = _stacks.members(at);
        if (members.size() != 1) {
          base = at;
          break;
        }
        at = members[0].stack;
        changed = true;
        continue;
      }
      _frames.push_back(at);
      at = _pool.contexts().parent(at, 0);
    }
    if (!changed) {
      if (!_frames.empty()) {
        bool fresh = false;
        _flat.at(stack, fresh) = stack;
      }
      return stack;
    }
    for (std::size_t i = _frames.size(); i-- > 0;) {
      base = _pool.contexts().push(base, _pool.contexts().ret(_frames[i], 0));
      bool fresh = false;
      _flat.at(_frames[i], fresh) = base;
    }
    return base;
  }

  // The cell, if there is one, that the frames of a stack stand over.
  std::uint32_t below(std::uint32_t stack) {
    _chain.clear();
    std::uint32_t at = stack, bottom = kLlCtxNone;
    while (at != kLlCtxNone) {
      if (LlStackSet::is_cell(at)) {
        bottom = at;
        break;
      }
      if (const std::uint32_t* known = _below.find(at); known != nullptr) {
        bottom = *known == 1 ? kLlCtxNone : *known - 2;
        break;
      }
      _chain.push_back(at);
      at = _pool.contexts().parent(at, 0);
    }
    for (const std::uint32_t node : _chain) {
      bool fresh = false;
      _below.at(node, fresh) = bottom == kLlCtxNone ? 1u : bottom + 2u;
    }
    return bottom;
  }

  // Whether `a` is `b` with frames over it.
  bool stands_on(std::uint32_t a, std::uint32_t b) {
    for (;;) {
      if (a == b) return true;
      if (a == kLlCtxNone || LlStackSet::is_cell(a)) return false;
      a = _pool.contexts().parent(a, 0);
    }
  }

  bool related(std::uint32_t a, std::uint32_t b) {
    return stands_on(a, b) || stands_on(b, a);
  }

  bool member_related(std::uint32_t cell, std::uint32_t stack) {
    static constexpr std::uint32_t root = 1u, ancestor = 2u;
    const auto key = [cell](std::uint32_t context) {
      return (std::uint64_t{cell} << 32) | context;
    };
    const auto& members = _stacks.members(cell);
    const auto* known = _relatedProcessed.find(cell);
    const std::size_t processed = known == nullptr ? 0 : *known;
    for (std::size_t i = processed; i < members.size(); ++i) {
      bool fresh = false;
      _relatedFlags.at(key(members[i].stack), fresh) |= root;
      std::uint32_t at = members[i].stack;
      for (;;) {
        bool marked = false;
        {
          auto& flags = _relatedFlags.at(key(at), fresh);
          marked = (flags & ancestor) != 0;
          flags |= ancestor;
        }
        // The rest of a marked chain is already indexed. Its terminal
        // cell or unknown caller belongs to that chain too.
        if (marked || at == kLlCtxNone || LlStackSet::is_cell(at)) break;
        at = _pool.contexts().parent(at, 0);
      }
    }
    bool fresh = false;
    _relatedProcessed.at(cell, fresh) =
        static_cast<std::uint32_t>(members.size());
    if (const auto* flags = _relatedFlags.find(key(stack));
        flags != nullptr && (*flags & ancestor) != 0)
      return true;
    for (;;) {
      if (const auto* flags = _relatedFlags.find(key(stack));
          flags != nullptr && (*flags & root) != 0)
        return true;
      if (stack == kLlCtxNone || LlStackSet::is_cell(stack)) return false;
      stack = _pool.contexts().parent(stack, 0);
    }
  }

  // Unrelated stacks at one state share the reading ahead even where their
  // first returns agree; leave() restores each member's history.
  bool joinable(const LlThread& e, const LlThread& p) {
    if (related(e.stack, p.stack)) return false;
    if (LlStackSet::is_cell(p.stack)) return true;
    if (!LlStackSet::is_cell(e.stack)) return true;
    if (_stacks.members(e.stack).size() > 16)
      return !member_related(e.stack, p.stack);
    for (const auto& m : _stacks.members(e.stack))
      if (related(m.stack, p.stack)) return false;
    return true;
  }

  // The reading `e`, now over the stacks of `p` as well; it leads. Common
  // return frames stand above the fork, shared until the first that differs.
  void fork(LlThread& e, const LlThread& p) {
    std::uint32_t first = e.stack, second = p.stack;
    _shared.clear();
    while (first != kLlCtxNone && second != kLlCtxNone &&
           !LlStackSet::is_cell(first) && !LlStackSet::is_cell(second)) {
      const auto ret = _pool.contexts().ret(first, 0);
      if (ret != _pool.contexts().ret(second, 0)) break;
      _shared.push_back(ret);
      first = _pool.contexts().parent(first, 0);
      second = _pool.contexts().parent(second, 0);
    }
    if (!_shared.empty()) {
      // A new cell's histories are based at this join. An existing cell
      // below the shared frames keeps its own base and is nested as a member.
      std::uint32_t stack = _stacks.fork({first, e.arm, e.lex, e.root},
                                         {second, p.arm, p.lex, p.root});
      for (std::size_t i = _shared.size(); i-- > 0;)
        stack = _pool.push_return(stack, _shared[i]);
      e.stack = stack;
      return;
    }
    const LlStackSet::Member added{p.stack, p.arm, p.lex, p.root};
    if (LlStackSet::is_cell(e.stack) && LlStackSet::index(e.stack) >= _base &&
        !_stacks.by_call(e.stack)) {
      _stacks.add(e.stack, added);
      return;
    }
    e.stack = _stacks.fork({e.stack, e.arm, e.lex, e.root}, added);
  }

  // The rules completed since the last token as one number: the same run met
  // twice is the same tag. Zero is the empty run.
  std::uint32_t reduced(std::uint32_t run, std::uint32_t state) {
    bool fresh = false;
    std::uint32_t& tag = _reduces.at(std::uint64_t{run} << 32 | state, fresh);
    if (fresh) {
      tag = static_cast<std::uint32_t>(_reduceUp.size());
      _reduceUp.push_back(run);
      _reduceState.push_back(state);
    }
    return tag;
  }

  // Whether the run of completions behind `tag` already ends the rule `state`
  // stops: a caller that is not known sends the reading round a cycle of
  // returns, and every lap would be a run of its own.
  bool completed(std::uint32_t tag, std::uint32_t state) const {
    for (; tag != 0 && tag != kLlNoReduce; tag = _reduceUp[tag])
      if (_reduceState[tag] == state) return true;
    return false;
  }

  // A reading at the end of a rule returns into the callers it has.
  void leave(const LlThread& p, const LlAtnState& s) {
    if (_tracked && p.stack == kLlCtxNone && completed(p.tag, p.state)) return;
    std::uint32_t reduce = kLlNoReduce;
    if (_tracked) reduce = reduced(p.tag == kLlNoReduce ? 0u : p.tag, p.state);
    LlThread q = p;
    q.tag = reduce;
    if (p.stack == kLlCtxNone) {
      const std::uint32_t from = _atn.callerBase[s.rule];
      const std::uint32_t to = _atn.callerBase[s.rule + 1];
      for (std::uint32_t i = to; i-- > from;) {
        q.state = _atn.callerAt[i];
        q.arm = to - from > 1 ? _arms.append(p.arm, kLlBlindWord | (i - from),
                                             _pos, _lull)
                              : p.arm;
        push(q);
      }
    } else if (LlStackSet::is_cell(p.stack)) {
      // Past a cell the reading is one per stack, each with its own words
      // and then the ones read since; all but the first wait their turn.
      const auto& members = _stacks.members(p.stack);
      for (std::size_t k = members.size(); k-- > 1;) {
        LlThread m = p;
        m.stack = members[k].stack;
        m.root = members[k].root;
        m.arm = _arms.graft(members[k].arm, members[0].arm, p.arm);
        m.lex = _lexes.graft(members[k].lex, members[0].lex, p.lex);
        push_later(m);
      }
      LlThread lead = p;
      lead.stack = members[0].stack;
      lead.root = members[0].root;
      push(lead);
    } else {
      const std::uint32_t ret = _pool.contexts().ret(p.stack, 0);
      q.state = ret;
      q.stack = _pool.contexts().parent(p.stack, 0);
      if (_tracked) q.tag = reduced(reduce, ret);
      push(q);
    }
  }

  const LlAtnView& _atn;
  LlReturnPool& _pool;
  bool _tracked;
  LlHistory _arms, _lexes;
  LlStamped<std::uint64_t, LlKeyHash> _reduces;
  std::vector<std::uint32_t> _reduceUp{0};
  std::vector<std::uint32_t> _reduceState{0};
  LlStackSet _stacks;
  std::vector<LlThread> _stack;
  std::vector<std::uint32_t> _words;
  std::vector<Pending> _side;
  std::vector<LlThread> _origins;
  LlStamped<LlThreadKey, LlThreadKeyHash> _seen, _held, _crossed, _calls;
  LlStamped<LlThreadKey, LlThreadKeyHash> _at;
  LlStamped<std::uint64_t, LlKeyHash> _below;
  LlStamped<std::uint64_t, LlKeyHash> _relatedProcessed, _relatedFlags;
  std::vector<std::uint32_t> _chain, _frames;
  LlStamped<std::uint64_t, LlKeyHash> _flat;
  std::vector<std::uint32_t> _shared;
  std::vector<std::uint8_t> _counts;
  std::vector<Heads> _heads;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _joins;
  std::vector<std::pair<std::uint32_t, std::uint16_t>>* _log = nullptr;
  std::size_t _base = 0;
  std::uint32_t _seq = 0;
  std::uint32_t _pos = 0;
  std::uint32_t _lull = 0;
  const std::uint8_t* _scoped = nullptr;
  const std::uint8_t* _shares = nullptr;
};

}  // namespace cursiv
