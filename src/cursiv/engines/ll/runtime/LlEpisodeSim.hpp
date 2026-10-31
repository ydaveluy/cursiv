// The readings of an episode and their closure over the empty moves, best
// first: the first to reach a state with a stack is the reading the grammar
// prefers, and any other that reaches it has a worse past and the same future.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>
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
  std::uint64_t operator()(const LlThreadKey& k) const noexcept {
    std::uint64_t h = k.state;
    h = (h * 1099511628211ull) ^ k.stack;
    h = (h * 1099511628211ull) ^ k.tag;
    h = (h * 1099511628211ull) ^ k.root;
    return h;
  }
};

// Where a frame lent from the descent stands in its stack: how many frames it
// has under it, and a frame further down it, chosen so that the frame at any
// depth is reached in a logarithmic number of steps and not one per frame.
struct LlLentRung {
  std::uint32_t depth = 0, jump = kLlCtxNone;

  // The rung of a frame lent over `parent`. Its jump goes as far as the
  // parent's jump goes on from the one it reaches, where the two spans are
  // equal, and to the parent otherwise.
  static LlLentRung over(const std::vector<LlLentRung>& rungs,
                         std::uint32_t parent) {
    const auto depth = [&](std::uint32_t n) {
      return n == kLlCtxNone ? 0u : rungs[n].depth;
    };
    const auto jump = [&](std::uint32_t n) {
      return n == kLlCtxNone ? kLlCtxNone : rungs[n].jump;
    };
    const std::uint32_t near = jump(parent), far = jump(near);
    LlLentRung r{depth(parent) + 1, parent};
    if (depth(parent) - depth(near) == depth(near) - depth(far)) r.jump = far;
    return r;
  }
};

// The word of a return that goes to a caller the episode only guesses, which no
// decision names: its decision is the one that names none.
inline constexpr std::uint32_t kLlBlindWord = 0xFFFF0000u;

// A choice readings went by, and the arms they took there, one bit per arm:
// none where two ways to it took different arms, or where readings also part
// there with no choice of their own.
struct LlChoiceArms {
  std::uint32_t at = 0, lull = 0;
  std::uint16_t decision = 0;
  std::uint64_t arms = 0;
};

class LlEpisodeSim {
  // The test of the walks over the frames lent asks them directly.
  friend struct LlLentStandsProbe;

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

  // The depths of the frames lent, which the cache of the parse keeps. Unset,
  // the walks go down a lent stack as they go down the episode's own.
  void read_lent_rungs(const std::vector<LlLentRung>* rungs) {
    _rungs = rungs;
  }

  const LlHistory& arms() const { return _arms; }
  LlHistory& lexes() { return _lexes; }
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
    ll_clear(_arms, _lexes, _reduces, _stacks, _below, _relatedProcessed,
             _relatedFlags, _relatedBaseCount, _relatedBases, _heads, _stack,
             _words, _side, _shared, _joins);
    _reduceUp.assign(1, 0);
    _reduceState.assign(1, 0);
    _log = nullptr;
    _stopped = false;
  }

  // ...and an episode that grew large gives its room back.
  void trim() {
    const auto each = [](auto&... x) { (x.trim(), ...); };
    each(_arms, _lexes, _reduces, _seen, _at, _held, _crossed, _calls, _flat,
         _below, _relatedProcessed, _relatedFlags, _relatedBaseCount,
         _relatedBases);
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
    ll_clear(out, _seen, _at, _calls, _relatedProcessed, _relatedFlags,
             _relatedBaseCount, _relatedBases, _stack, _words, _side);
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
    }
    for (std::size_t k = _base; k < _stacks.size(); ++k)
      _stacks.seal(LlStackSet::kCellBit | static_cast<std::uint32_t>(k));
  }

  // The readings that take `terms`, each at the state its edge leads to, to be
  // closed where the token ends.
  void advance(const LlThread& t, const std::uint16_t* terms, std::size_t n,
               LlHistory::Node lex, std::uint32_t kernel,
               std::vector<LlThread>& origins) const {
    for (const LlAtnEdge& e : _atn.outgoing(t.state)) {
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
    for (auto cell = below(stack); cell != kLlCtxNone;) {
      const auto& members = _stacks.members(cell);
      // Every member stands for at least one reading. Two already settle
      // this capped count, without walking either member's descendants.
      if (members.size() > 1) return 2;
      cell = below(members.front().stack);
    }
    return 1;
  }

  // ...and the arms of those readings: each with the decisions of its own and
  // then the ones taken since. `visit` stops the walk by answering false.
  template <class Visit>
  bool each_arm(std::uint32_t stack, LlHistory::Node arm, Visit&& visit) {
    const auto cell = below(stack);
    if (cell == kLlCtxNone) return visit(arm);
    return walk_arms(cell, arm, [&](auto& path) {
      if (path.back().cell != kLlCtxNone) return ArmVisit::descend;
      return visit(made(path)) ? ArmVisit::skip : ArmVisit::stop;
    });
  }

  // Whether a reading left a rule with no caller under it: the entry, or a
  // rule whose caller is not known.
  bool stopped() const { return _stopped; }

  // The choices the readings of `threads` went by, sorted by place, each with
  // the arms taken there. A reading over a cell took what each member of the
  // cell took, whatever came before it, so each cell is read once, not once
  // per reading over it. False where the words of a member do not extend the
  // lead of the cell it stands on.
  bool choices(const std::vector<LlThread>& threads,
               std::vector<LlChoiceArms>& out) {
    out.clear();
    // Each reading, and each member of a cell met, as the cell below it and
    // the words it took past the lead of that cell.
    struct Taken {
      std::uint32_t base;
      std::uint32_t begin, end;
    };
    std::vector<Taken> taken;
    std::vector<LlHistory::Event> words;
    std::vector<std::uint32_t> cells;
    std::vector<std::uint8_t> met(_stacks.size(), 0);
    const auto take = [&](std::uint32_t stack, LlHistory::Node arm) {
      const std::uint32_t base = below(stack);
      LlHistory::Node lead = LlHistory::kRoot;
      if (base != kLlCtxNone) {
        lead = _stacks.members(base)[0].arm;
        if (met[LlStackSet::index(base)] == 0) {
          met[LlStackSet::index(base)] = 1;
          cells.push_back(base);
        }
      }
      if (!_arms.starts_with(arm, lead)) return false;
      const auto begin = static_cast<std::uint32_t>(words.size());
      _arms.each(arm, _arms.depth(lead), [&](const LlHistory::Event& e) {
        words.push_back(e);
        return true;
      });
      taken.push_back({base, begin, static_cast<std::uint32_t>(words.size())});
      return true;
    };
    for (const LlThread& t : threads)
      if (!take(t.stack, t.arm)) return false;
    while (!cells.empty()) {
      const std::uint32_t cell = cells.back();
      cells.pop_back();
      for (const LlStackSet::Member& m : _stacks.members(cell))
        if (!take(m.stack, m.arm)) return false;
    }
    const auto key = [](const LlHistory::Event& e) {
      return std::tuple{e.word, e.at, e.lull};
    };
    std::sort(taken.begin(), taken.end(), [&](const Taken& a, const Taken& b) {
      if (a.base != b.base) return a.base < b.base;
      return std::lexicographical_compare(
          words.begin() + a.begin, words.begin() + a.end,
          words.begin() + b.begin, words.begin() + b.end,
          [&](const auto& x, const auto& y) { return key(x) < key(y); });
    });
    // The readings of one cell that share their first `depth` words, and the
    // arms they take next. Where they go on to more than one choice, or some
    // stop there while others go on, nothing is known of those choices.
    struct Range {
      std::size_t lo, hi;
      std::uint32_t depth;
    };
    std::vector<Range> ranges;
    for (std::size_t lo = 0; lo < taken.size();) {
      std::size_t hi = lo + 1;
      while (hi < taken.size() && taken[hi].base == taken[lo].base) ++hi;
      ranges.push_back({lo, hi, 0});
      lo = hi;
    }
    const auto size = [&](const Taken& t) { return t.end - t.begin; };
    while (!ranges.empty()) {
      const Range r = ranges.back();
      ranges.pop_back();
      std::size_t lo = r.lo;
      while (lo < r.hi && size(taken[lo]) == r.depth) ++lo;
      if (lo == r.hi) continue;
      const bool ends = lo != r.lo;
      const auto first = static_cast<std::uint32_t>(out.size());
      for (std::size_t at = lo; at < r.hi;) {
        const LlHistory::Event e = words[taken[at].begin + r.depth];
        std::size_t next = at + 1;
        while (next < r.hi && key(words[taken[next].begin + r.depth]) == key(e))
          ++next;
        ranges.push_back({at, next, r.depth + 1});
        const auto decision = static_cast<std::uint16_t>(e.word >> 16);
        const std::uint32_t arm = e.word & 0xFFFFu;
        const std::uint64_t bit = arm < 64 ? std::uint64_t{1} << arm : 0;
        if (out.size() > first && out.back().at == e.at &&
            out.back().lull == e.lull && out.back().decision == decision)
          out.back().arms |= bit;
        else
          out.push_back({e.at, e.lull, decision, bit});
        at = next;
      }
      if (ends || out.size() - first > 1)
        for (std::size_t k = first; k < out.size(); ++k) out[k].arms = 0;
    }
    // The ways to one choice: it keeps its arms where they all agree.
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      return std::tuple{a.at, a.lull, a.decision, a.arms} <
             std::tuple{b.at, b.lull, b.decision, b.arms};
    });
    std::size_t kept = 0;
    for (std::size_t k = 0; k < out.size();) {
      std::size_t next = k + 1;
      bool agree = true;
      for (; next < out.size() && out[next].at == out[k].at &&
             out[next].lull == out[k].lull &&
             out[next].decision == out[k].decision;
           ++next)
        agree = agree && out[next].arms == out[k].arms;
      out[kept] = out[k];
      if (!agree) out[kept].arms = 0;
      ++kept;
      k = next;
    }
    out.resize(kept);
    return true;
  }

  // What `common` and every reading of a thread share, folded into `common`
  // from the first reading `met`: false once it is no deeper than `floor`,
  // and the walk stops. A reading is grafted only where `common` runs past
  // the arm it starts with, and a cell is met, unwalked, by the prefix its
  // readings' arms share where they part right there or `common` parts from
  // it first.
  bool meet_arms(std::uint32_t stack, LlHistory::Node arm,
                 LlHistory::Node& common, bool& met, std::uint32_t floor) {
    const auto cell = below(stack);
    if (cell == kLlCtxNone) {
      common = met ? _arms.common(common, arm) : arm;
      met = true;
      return _arms.depth(common) > floor;
    }
    return walk_arms(
        cell, arm, [&](auto& path) { return meet(path, common, met, floor); });
  }

 private:
  // A reading's arm, made once some reading below asks for it: its own head
  // past which what the reading above took after `from` is grafted.
  struct Graft {
    LlHistory::Node head, from;
    LlHistory::Node made = kUnmade;
  };
  static constexpr LlHistory::Node kUnmade = ~LlHistory::Node{0};

  struct ArmFrame {
    std::uint32_t cell;
    Graft graft;
    std::size_t next = 0;
  };
  enum class ArmVisit { descend, skip, stop };

  LlHistory::Node made(std::vector<ArmFrame>& path) {
    auto at = path.size() - 1;
    while (at != 0 && path[at].graft.made == kUnmade) --at;
    if (path[at].graft.made == kUnmade)
      path[at].graft.made = path[at].graft.head;
    for (++at; at < path.size(); ++at) {
      Graft& g = path[at].graft;
      g.made = _arms.graft(g.head, g.from, path[at - 1].graft.made);
    }
    return path.back().graft.made;
  }

  // The active DFS path also holds each lazy graft. Its parent is the prior
  // frame's index, so growing the vector leaves no dangling parent pointer.
  template <class Visit>
  bool walk_arms(std::uint32_t cell, LlHistory::Node arm, Visit&& visit) {
    std::vector<ArmFrame> path{{cell, {arm, arm}}};
    while (!path.empty()) {
      ArmFrame& f = path.back();
      if (f.next == 0) {
        const auto action = visit(path);
        if (action == ArmVisit::stop) return false;
        if (action == ArmVisit::skip) {
          path.pop_back();
          continue;
        }
      }
      const auto& members = _stacks.members(f.cell);
      if (f.next == members.size()) {
        path.pop_back();
        continue;
      }
      const auto& m = members[f.next++];
      path.push_back({below(m.stack), {m.arm, members.front().arm}});
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
    const auto cached = [&](std::uint32_t c) {
      const auto at = LlStackSet::index(c);
      if (_heads.size() <= at) _heads.resize(at + 1);
      return _heads[at].shared != kUnmade;
    };
    if (cached(cell)) return _heads[LlStackSet::index(cell)];
    struct Frame {
      std::uint32_t cell;
      std::size_t next = 0;
      Heads all{};
    };
    std::vector<Frame> path{{cell}};
    while (!path.empty()) {
      Frame& f = path.back();
      const auto& members = _stacks.members(f.cell);
      if (f.next == members.size()) {
        _heads[LlStackSet::index(f.cell)] = f.all;
        path.pop_back();
        continue;
      }
      const auto& m = members[f.next];
      const auto next = below(m.stack);
      if (next != kLlCtxNone && !cached(next)) {
        path.push_back({next});
        continue;
      }
      Heads h{m.arm, _arms.depth(m.arm), true, false};
      if (next != kLlCtxNone) {
        const Heads lower = _heads[LlStackSet::index(next)];
        const LlHistory::Node lead = _stacks.members(next)[0].arm;
        const std::uint32_t added = _arms.depth(m.arm) - _arms.depth(lead);
        if (!_arms.starts_with(m.arm, lead)) {
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
      Heads& all = f.all;
      if (all.shared == kUnmade) {
        all = h;
      } else {
        all.unsettled = all.unsettled || h.unsettled;
        all.single =
            !all.unsettled && all.single && h.single && all.shared == h.shared;
        all.shared = _arms.common(all.shared, h.shared);
        all.shortest = std::min(all.shortest, h.shortest);
      }
      ++f.next;
    }
    return _heads[LlStackSet::index(cell)];
  }

  ArmVisit meet(std::vector<ArmFrame>& path, LlHistory::Node& common, bool& met,
                std::uint32_t floor) {
    const auto cell = path.back().cell;
    const auto& g = path.back().graft;
    if (cell == kLlCtxNone) {
      if (!met) {
        common = made(path);
        met = true;
      } else if (const LlHistory::Node c = _arms.common(common, g.head);
                 _arms.depth(c) < _arms.depth(g.head)) {
        common = c;
      } else {
        common = _arms.common(common, made(path));
      }
      return _arms.depth(common) > floor ? ArmVisit::skip : ArmVisit::stop;
    }
    if (const Heads h = heads(cell); h.shared != kUnmade) {
      const std::uint32_t depth = _arms.depth(h.shared);
      LlHistory::Node meets = kUnmade;
      if (!h.unsettled && h.shortest > depth) {
        meets = met ? _arms.common(common, h.shared) : h.shared;
      } else if (!h.unsettled && h.single) {
        const LlHistory::Node whole =
            _arms.graft(h.shared, _stacks.members(cell)[0].arm, made(path));
        meets = met ? _arms.common(common, whole) : whole;
      } else if (met) {
        if (const LlHistory::Node c = _arms.common(common, h.shared);
            _arms.depth(c) < depth)
          meets = c;
      }
      if (meets != kUnmade) {
        common = meets;
        met = true;
        return _arms.depth(common) > floor ? ArmVisit::skip : ArmVisit::stop;
      }
    }
    return ArmVisit::descend;
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
    _everyone = threads;
    expand(_everyone, accepts);
    threads.clear();
    for (const LlThread& t : _everyone)
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
    _everyone = threads;
    expand(_everyone, [](std::uint16_t, std::uint32_t) { return true; },
           stays);
    for (const LlThread& t : _everyone)
      for (const LlAtnEdge& e : _atn.outgoing(t.state))
        if (e.kind == LlEdgeKind::atom) read(e.label);
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
    std::vector<LlThread>& origins = _crossOrigins;
    std::vector<LlThread>& grown = _crossGrown;
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
      cell = _stacks.open({q.stack, q.arm, q.lex});
      q.stack = cell;
      return true;
    }
    if (!_stacks.holds(cell, q.stack))
      _stacks.add(cell, {q.stack, q.arm, q.lex});
    return false;
  }

  // A frame made before the episode's walk began: the descent's stack, lent
  // to the episode, with no cell under it. The walks down a stack stop there,
  // so they cost the frames the episode made rather than the descent's depth.
  bool lent(std::uint32_t stack) const {
    return _pool.contexts().before_walk(stack);
  }

  // The rung of a lent frame, or null for any other: with it, what a walk
  // asks of a stack that has reached the descent's frames is answered there.
  const LlLentRung* rung(std::uint32_t stack) const {
    if (_rungs == nullptr || stack >= _rungs->size() ||
        (*_rungs)[stack].depth == 0 || !lent(stack))
      return nullptr;
    return &(*_rungs)[stack];
  }

  // Whether the lent frame `a`, which has the rung `r`, is `b` with frames
  // over it. The unknown caller is under every lent stack; a frame not lent is
  // under none, since the frames lent stand on frames lent alone.
  bool lent_stands_on(std::uint32_t a, const LlLentRung& r, std::uint32_t b) {
    if (a == b || b == kLlCtxNone) return true;
    const LlLentRung* under = rung(b);
    if (under == nullptr || under->depth >= r.depth) return false;
    const auto& rungs = *_rungs;
    while (rungs[a].depth > under->depth) {
      const std::uint32_t jump = rungs[a].jump;
      const bool far = jump != kLlCtxNone && rungs[jump].depth >= under->depth;
      a = far ? jump : _pool.contexts().parent(a, 0);
    }
    return a == b;
  }

  // A stack without the cells that hold a single stack: the frames over such
  // a cell stand over that stack, so that a rule entered again and again
  // leaves no chain of them behind.
  std::uint32_t flat(std::uint32_t stack) {
    _frames.clear();
    std::uint32_t at = stack, base = kLlCtxNone;
    bool changed = false;
    while (at != kLlCtxNone) {
      if (lent(at)) {
        base = at;
        break;
      }
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
    while (at != kLlCtxNone && !lent(at)) {
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

  // Whether `a` is `b` with frames over it. The walk stops at the first frame
  // lent, which knows its depth: it costs the frames the episode made.
  bool stands_on(std::uint32_t a, std::uint32_t b) {
    for (;;) {
      if (a == b) return true;
      if (a == kLlCtxNone || LlStackSet::is_cell(a)) return false;
      if (const LlLentRung* r = rung(a)) return lent_stands_on(a, *r, b);
      a = _pool.contexts().parent(a, 0);
    }
  }

  bool related(std::uint32_t a, std::uint32_t b) {
    return stands_on(a, b) || stands_on(b, a);
  }

  // Whether `stack` stands on a member of the cell, or a member on it. The
  // chains of the members are indexed once, a flag per frame; a chain stops at
  // the first frame lent, which is listed as a base of the cell instead, so
  // that the descent's depth is not walked once per cell.
  bool member_related(std::uint32_t cell, std::uint32_t stack) {
    static constexpr std::uint32_t root = 1u, ancestor = 2u;
    const auto key = [cell](std::uint32_t context) {
      return (std::uint64_t{cell} << 32) | context;
    };
    const auto& members = _stacks.members(cell);
    const auto* known = _relatedProcessed.find(cell);
    const std::size_t processed = known == nullptr ? 0 : *known;
    bool fresh = false;
    for (std::size_t i = processed; i < members.size(); ++i) {
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
        if (rung(at) != nullptr) {
          std::uint32_t& count = _relatedBaseCount.at(cell, fresh);
          _relatedBases.at(key(count++), fresh) = at;
          break;
        }
        at = _pool.contexts().parent(at, 0);
      }
    }
    _relatedProcessed.at(cell, fresh) =
        static_cast<std::uint32_t>(members.size());
    const std::uint32_t* bases = _relatedBaseCount.find(cell);
    const std::uint32_t nbases = bases == nullptr ? 0 : *bases;
    const auto base = [&](std::uint32_t k) {
      return *_relatedBases.find(key(k));
    };
    if (const auto* flags = _relatedFlags.find(key(stack));
        flags != nullptr && (*flags & ancestor) != 0)
      return true;
    // A member on the frames lent: the bases that stand on `stack`.
    if (stack == kLlCtxNone || rung(stack) != nullptr)
      for (std::uint32_t k = 0; k < nbases; ++k)
        if (stands_on(base(k), stack)) return true;
    for (;;) {
      if (const auto* flags = _relatedFlags.find(key(stack));
          flags != nullptr && (*flags & root) != 0)
        return true;
      if (stack == kLlCtxNone || LlStackSet::is_cell(stack)) return false;
      if (rung(stack) != nullptr) break;
      stack = _pool.contexts().parent(stack, 0);
    }
    // Where the stack reaches the frames lent, a member among them is a base
    // that it stands on; the unknown caller, a member too, ends every chain.
    if (const auto* flags = _relatedFlags.find(key(kLlCtxNone));
        flags != nullptr && (*flags & root) != 0)
      return true;
    for (std::uint32_t k = 0; k < nbases; ++k) {
      const auto* flags = _relatedFlags.find(key(base(k)));
      if (flags != nullptr && (*flags & root) != 0 &&
          stands_on(stack, base(k)))
        return true;
    }
    return false;
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
      std::uint32_t stack =
          _stacks.fork({first, e.arm, e.lex}, {second, p.arm, p.lex});
      for (std::size_t i = _shared.size(); i-- > 0;)
        stack = _pool.push_return(stack, _shared[i]);
      e.stack = stack;
      return;
    }
    const LlStackSet::Member added{p.stack, p.arm, p.lex};
    if (LlStackSet::is_cell(e.stack) && LlStackSet::index(e.stack) >= _base &&
        !_stacks.by_call(e.stack)) {
      _stacks.add(e.stack, added);
      return;
    }
    e.stack = _stacks.fork({e.stack, e.arm, e.lex}, added);
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
      _stopped = true;
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
        m.arm = _arms.graft(members[k].arm, members[0].arm, p.arm);
        m.lex = _lexes.graft(members[k].lex, members[0].lex, p.lex);
        push_later(m);
      }
      LlThread lead = p;
      lead.stack = members[0].stack;
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
  // What filter and possible_terms read past the conditions: the readings and
  // the ones a crossing grows, kept between calls for their room.
  std::vector<LlThread> _everyone, _crossOrigins, _crossGrown;
  LlStamped<LlThreadKey, LlThreadKeyHash> _seen, _held, _crossed, _calls;
  LlStamped<LlThreadKey, LlThreadKeyHash> _at;
  LlStamped<std::uint64_t, LlKeyHash> _below;
  LlStamped<std::uint64_t, LlKeyHash> _relatedProcessed, _relatedFlags;
  // The frames lent that the chains of a cell's members end on, listed per
  // cell: how many, and each by its place in the list.
  LlStamped<std::uint64_t, LlKeyHash> _relatedBaseCount, _relatedBases;
  std::vector<std::uint32_t> _chain, _frames;
  LlStamped<std::uint64_t, LlKeyHash> _flat;
  std::vector<std::uint32_t> _shared;
  std::vector<Heads> _heads;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> _joins;
  std::vector<std::pair<std::uint32_t, std::uint16_t>>* _log = nullptr;
  std::size_t _base = 0;
  bool _stopped = false;
  std::uint32_t _seq = 0;
  std::uint32_t _pos = 0;
  std::uint32_t _lull = 0;
  const std::uint8_t* _scoped = nullptr;
  const std::uint8_t* _shares = nullptr;
  const std::vector<LlLentRung>* _rungs = nullptr;
};

}  // namespace cursiv
