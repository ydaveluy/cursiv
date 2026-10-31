// An episode: the readings of a choice no automaton parts, stepped over the
// text together and best first until a few are left or the text ends. The
// choices they pass are the plan the descent is answered from, and a reading
// that two others reach one place with is the better one alone.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/runtime/LlAhead.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeCache.hpp"
#include "cursiv/engines/ll/runtime/LlEpisodeMemo.hpp"
#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/engines/ll/runtime/LlPlan.hpp"
#include "cursiv/engines/ll/runtime/LlThread.hpp"
#include "cursiv/engines/ll/runtime/ll_indent.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// The arm of the first decision every reading stands for, `kNoRow` while more
// than one does.
inline std::uint16_t ll_settled_arm(const std::vector<LlThread>& threads) {
  if (threads.empty()) return kNoRow;
  for (const LlThread& t : threads)
    if (t.root != threads.front().root) return kNoRow;
  return threads.front().root;
}

// The readings as one key: the same readings met twice are one, whatever order
// they were reached in.
using LlThreadSetKey =
    std::vector<std::tuple<std::uint32_t, std::uint64_t, std::uint16_t>>;
inline void ll_thread_set_key(const LlEpisodeSim& sim,
                              const std::vector<LlThread>& threads,
                              LlThreadSetKey& key) {
  key.clear();
  for (const LlThread& t : threads)
    key.emplace_back(t.state, sim.stacks().canon(t.stack), t.root);
  std::sort(key.begin(), key.end());
  key.erase(std::unique(key.begin(), key.end()), key.end());
}

// No token further on tells the arms apart: every reading stands with another
// at one place and one stack, and every place names the same arms.
inline bool ll_stuck_apart(const LlEpisodeSim& sim,
                           const std::vector<LlThread>& threads) {
  if (threads.size() < 2) return false;
  thread_local LlThreadSetKey seats;
  ll_thread_set_key(sim, threads, seats);
  const auto place = [](const auto& s) {
    return std::pair{std::get<0>(s), std::get<1>(s)};
  };
  std::size_t first = 0;
  while (first < seats.size() && place(seats[first]) == place(seats[0]))
    ++first;
  if (first < 2) return false;
  for (std::size_t at = first; at < seats.size();) {
    std::size_t k = 0;
    for (; at + k < seats.size() && place(seats[at + k]) == place(seats[at]);
         ++k)
      if (k >= first || std::get<2>(seats[at + k]) != std::get<2>(seats[k]))
        return false;
    if (k != first) return false;
    at += k;
  }
  return true;
}

// The decisions on the path to `node` below `from`, down to the first return
// into a caller the episode only guessed: past it the descent may go
// elsewhere. False where the path stops there, or does not pass `from`.
inline bool ll_plan_path(const LlHistory& arms, LlHistory::Node node,
                         std::vector<LlPlanEntry>& out, LlHistory::Node from) {
  if (!arms.starts_with(node, from)) return false;
  bool laid = true;
  arms.each(node, arms.depth(from), [&](const LlHistory::Event& e) {
    if ((e.word >> 16) == 0xFFFFu) return laid = false;
    out.push_back({e.at, e.lull, static_cast<std::uint16_t>(e.word >> 16),
                   static_cast<std::uint16_t>(e.word & 0xFFFFu)});
    return true;
  });
  return laid;
}

// What every reading of a frontier agrees on (`common`), and the choice where
// they part and none of them lives past: the first arm `deeper` than it names
// that choice.
inline void ll_plan_dead_end(const LlHistory& arms, LlHistory::Node common,
                             const LlHistory::Node* deeper,
                             std::vector<LlPlanEntry>& out,
                             LlHistory::Node from) {
  if (!ll_plan_path(arms, common, out, from) || deeper == nullptr) return;
  const LlHistory::Event n = arms.event(*deeper, arms.depth(common));
  if ((n.word >> 16) != 0xFFFFu)
    out.push_back(
        {n.at, n.lull, static_cast<std::uint16_t>(n.word >> 16), kNoRow});
}

// The descent follows the plan: it is installed where the parse can keep it,
// with the ways its refusals below hold on where it has any. Zero where there
// is no parse to keep it.
template <const auto& T>
inline LlPlan::Laid ll_plan_install(const LlContext& c,
                                    std::vector<LlPlanEntry>& entries,
                                    std::vector<LlPlanWay>* ways = nullptr) {
  auto* slot = ll_episode_session_slot<T>();
  if (slot == nullptr || !slot->has_value()) return 0;
  return (*slot)->plan.install(entries, ways, c.repaired.count());
}

// The descent took `arm` at a choice the plan did not answer.
template <const auto& T>
inline void ll_plan_went(const LlContext& c, std::uint16_t decision,
                         std::uint16_t arm) {
  auto* slot = ll_episode_session_slot<T>();
  if (slot == nullptr || !slot->has_value()) return;
  (*slot)->plan.went(decision, c.at, c.lull, arm);
}

// The decisions the readings have gone by since the plan was laid, put after
// it; false when the plan is no longer the one laid.
template <const auto& T>
inline bool ll_plan_extend(LlPlan::Laid laid,
                           const std::vector<LlPlanEntry>& path) {
  auto* slot = ll_episode_session_slot<T>();
  return slot != nullptr && slot->has_value() &&
         (*slot)->plan.extend(laid, path);
}

// What the plan of the parse says of a choice where the descent stands.
template <const auto& T>
inline std::uint16_t ll_planned(const LlContext& c, std::uint16_t decision) {
  auto* slot = ll_episode_session_slot<T>();
  if (slot == nullptr || !slot->has_value()) return kLlUnplanned;
  return (*slot)->plan.say(decision, c.at, c.lull, c.repaired.count(),
                           T.decisions[decision].again);
}

template <const auto& T>
class LlEpisode {
 public:
  std::uint16_t run(LlContext& c, std::uint16_t decision) {
    _cp = &c;
    _decision = decision;
    _inspected = _chosenAt = 0;
    _ended = _chosenEnded = false;
    _whole = true;
    _resumed = false;
    abandon();
    _walk.emplace(c);
    const struct Release {
      std::optional<LlExactWalk<T>>& walk;
      const bool& held;
      ~Release() {
        if (!held) walk.reset();
      }
    } release{_walk, _held};
    return solve();
  }

  // The episode that stopped where its choice was made, with readings left,
  // reads on for the choice the descent asks next, as far as the plan needs to
  // name it; false where the descent is not on the way of its readings.
  bool resume(LlContext& c, std::uint16_t decision, std::uint16_t& arm) {
    if (!_held) return false;
    auto* slot = ll_episode_session_slot<T>();
    if (slot == nullptr || !slot->has_value() || &slot->value() != _heldBy ||
        c.repaired.count() != _heldRepairs || c.src != _heldSrc ||
        c.at < _heldAt || _positions.empty() ||
        c.at > _positions.front().key[0]) {
      abandon();
      return false;
    }
    _cp = &c;
    _want = {c.at, c.lull, decision};
    _resumed = true;
    struct Keep {
      LlEpisode& episode;
      bool on = false;
      ~Keep() {
        if (!on) episode.abandon();
      }
    } keep{*this};
    const std::uint16_t reached = drive();
    keep.on = reached == kHeldOn && _sim->arms().depth(_laidAt) < kHeldReach;
    if (reached == kRestart || reached == kLost) return false;
    arm = ll_planned<T>(c, decision);
    return arm != kLlUnplanned;
  }

  // Lets go of the episode held, if it is the one of `owner`'s parse.
  void abandon() {
    if (!_held) return;
    _held = false;
    _walk.reset();
  }
  void release(const LlEpisodeCache* owner) {
    if (_held && _heldBy == owner) abandon();
  }

  std::uint16_t solve() {
    std::uint16_t remembered = kNoRow;
    _kept = memo();
    if (_kept != nullptr) _q = question();
    if (recall(remembered)) return remembered;
    auto& scratch = ll_scratch<T.termWords>();
    LlLookRead first;
    ll_first_read<T>(ctx(), dec(), scratch.viable, scratch.read, &first);
    touch(first);
    _firstTerms = scratch.read;
    _firstTop = first.top;
    // Only a plain attempt asks to start over, and it starts over tracked.
    const std::uint16_t arm = attempt(first.different);
    return arm != kRestart ? arm : attempt(true);
  }

  // What the episode has read since it was last asked.
  std::uint64_t take_steps() { return std::exchange(_steps, 0); }

 private:
  using Key = std::array<std::uint32_t, 5>;
  static constexpr std::uint16_t kRestart = 0xFFFCu;
  // "The episode stands, held, with readings left" and "it cannot name the
  // choice asked".
  static constexpr std::uint16_t kHeldOn = 0xFFFBu;
  // How many decisions a held episode lays before the next choice starts an
  // episode of its own: past them its readings cost every token they cross,
  // and a fresh episode reads only what its own choice needs.
  static constexpr std::uint32_t kHeldReach = 256;
  static constexpr std::uint16_t kLost = 0xFFFAu;
  static constexpr std::uint32_t kStart = LlEpisodeSim::kStart;
  static constexpr TextOffset kNoGap = ~TextOffset{0};

  enum class Verdict { go, restart, done };

  struct Position {
    Key key;
    std::vector<LlThread> threads;
    bool first = false;
    // More than one closure laid readings here: they are not in order.
    bool mixed = false;
    // Where the gap of the last token that reached here begins, the nearest.
    TextOffset gap = 0;
  };

  struct LlMeetKey {
    std::uint32_t state;
    std::uint64_t canon;
    std::uint16_t root;
    bool operator==(const LlMeetKey&) const = default;
  };
  struct LlMeetHash {
    std::uint64_t operator()(const LlMeetKey& k) const noexcept {
      std::uint64_t h = k.state;
      h = (h * 1099511628211ull) ^ k.canon;
      h = (h * 1099511628211ull) ^ k.root;
      return h;
    }
  };

  struct Candidate {
    LlHistory::Node arm = LlHistory::kRoot;
    LlHistory::Node lex = LlHistory::kRoot;
    std::uint16_t root = kNoRow;
    bool valid() const { return root != kNoRow; }
  };

  struct Reading {
    std::uint16_t term;
    TextOffset begin, end;
    std::uint32_t group;
    std::uint8_t tier = 0;
  };

  // What visit_tracked keeps of a group before its readings are made, a
  // reading made apart, and where the choices of a place stand; and a thread
  // by its group.
  struct Place {
    LlMask<T.termWords> atoms, own;
    LlLookRead next;
    TextOffset begin = 0;
    bool found = false, open = false;
  };
  struct Tail {
    TextOffset begin, end;
    unsigned rank;
    std::uint32_t choice;
  };
  struct Aged {
    std::uint32_t group, order;
    LlThread thread;
  };

  // The token every reading of one history crosses, and where it stands.
  struct Crossing {
    TextOffset begin, end, gapFrom;
    std::uint32_t lull;
    IndentAt at;
    TextOffset held;
  };

  struct Kernels {
    std::vector<std::uint32_t> up;
    std::uint32_t make() {
      up.push_back(static_cast<std::uint32_t>(up.size()));
      return up.back();
    }
    std::uint32_t find(std::uint32_t k) {
      if (k == kStart) return kStart;
      while (up[k] != k) k = up[k] = up[up[k]];
      return k;
    }
    void unite(std::uint32_t a, std::uint32_t b) {
      a = find(a);
      b = find(b);
      if (a != b && a != kStart && b != kStart) up[a] = b;
    }
  };

  std::uint16_t attempt(bool tracked) {
    _tracked = tracked;
    _sim = &ll_episode_sim<T>(tracked);
    if constexpr (kIndented<T>) ll_indent_copy(ctx(), _own);
    _entryRule = ctx().document;
    ll_clear(_positions, _entries, _below, _ways, _frontier, _final, _ends,
             _kernels.up);
    _decided = _early = _halted = false;
    _chosenAt = 0;
    _answer = kNoRow;
    _completed = _atEnd = _provenEnd = {};
    _frontierAt = 0;
    _sim->at(ctx().at, ctx().lull);
    Position& first = insert(key(ctx().la.begin, ctx().lull));
    _sim->seed(dec().state, _walk->context, first.threads);
    first.first = true;
    first.gap = 0;
    return drive();
  }

  // The positions in order, nearest first: until they are done; or, the choice
  // made with readings left, the plan is laid as far as they agree and the
  // episode waits; or, resumed, the plan names the choice asked.
  std::uint16_t drive() {
    while (!_positions.empty() && !_halted) {
      ctx().stop.poll();
      Position p = std::move(_positions.front());
      _positions.erase(_positions.begin());
      ++_steps;
      const Verdict verdict = visit(p);
      if (verdict == Verdict::restart) return kRestart;
      spare(std::move(p.threads));
      if (verdict == Verdict::done) break;
      if (!_decided) agree();
      if (_early) break;
      if (!_decided || _positions.empty()) continue;
      if (!_resumed) return hold();
      if (const std::uint16_t asked = names_want(); asked != kLlUnplanned)
        return asked;
    }
    return conclude();
  }

  void touch(const LlLookRead& r) {
    _inspected = std::max(_inspected, r.inspected);
    _ended = _ended || r.ended;
  }

  // Where the question of this episode begins, and what it asks of the memo.
  struct Question {
    TextOffset begin, gap, first;
    std::uint64_t hash;
  };
  Question question() const {
    const TextOffset begin = std::min(ctx().at, ctx().la.begin);
    const TextOffset gap = ctx().la.begin - begin;
    const TextOffset first = ctx().la.end - ctx().la.begin;
    return {begin, gap, first,
            ll_remembered_hash(
                _walk->key,
                std::string_view(ctx().src + begin, ctx().la.end - begin),
                first, gap, ctx().la.sym, ctx().section)};
  }

  LlEpisodeMemo* memo() {
    if constexpr (kIndented<T>) return nullptr;
    auto* slot = ll_episode_session_slot<T>();
    if (slot == nullptr || !slot->has_value()) return nullptr;
    auto& all = (*slot)->memo;
    if (all.empty()) all.resize(T.ndecisions);
    return &all[_decision];
  }

  // A place that reads the same as one the episodes met is answered by the
  // plan of the first.
  bool recall(std::uint16_t& arm) {
    if (_kept == nullptr) return false;
    const TextOffset room = ctx().len - _q.begin;
    const LlRemembered* row =
        _kept->find(_q.hash, room, [&](const LlRemembered& k) {
          return k.term == ctx().la.sym && k.first == _q.first &&
                 k.gap == _q.gap && k.section == ctx().section &&
                 k.context == _walk->key && k.length <= room &&
                 (!k.ended || k.length == room) &&
                 std::memcmp(ctx().src + _q.begin, ctx().src + k.from,
                             k.length) == 0;
        });
    if (row == nullptr) return false;
    if (row->count != 0) {
      _kept->lay(*row, _q.begin, _entries);
      ll_plan_install<T>(ctx(), _entries);
    } else if (auto* slot = ll_episode_session_slot<T>();
               slot != nullptr && slot->has_value()) {
      // The arm remembered may leave the way of the refusals below.
      (*slot)->plan.drop_below();
    }
    arm = row->arm;
    return true;
  }

  void remember() {
    if (_kept == nullptr) return;
    LlRemembered row{.context = _walk->key,
                     .from = _q.begin,
                     .first = _q.first,
                     .gap = _q.gap,
                     .term = ctx().la.sym,
                     .arm = _decided ? _answer : kNoRow,
                     .section = ctx().section};
    const auto reached = [&](TextOffset at) {
      return at >= _q.begin && _q.gap + _q.first <= at - _q.begin;
    };
    // The arm alone is answered by less text than the plan, and by a plan the
    // text refused later.
    const bool whole = _whole && reached(_inspected);
    if (_chosenAt != 0 && reached(_chosenAt) &&
        !(whole && _chosenAt == _inspected)) {
      row.length = _chosenAt - _q.begin;
      row.ended = _chosenEnded;
      _kept->add(_q.hash, row, {});
    }
    if (!whole) return;
    row.length = _inspected - _q.begin;
    row.ended = _ended;
    _kept->add(_q.hash, row, _entries);
  }

  Key key(TextOffset from, std::uint32_t lull) const {
    if constexpr (kIndented<T>)
      return {from, _own.at.head, _own.at.depth, _own.held, lull};
    else
      return {from, 0, 0, 0, lull};
  }

  Position& insert(const Key& k) {
    auto it = std::lower_bound(
        _positions.begin(), _positions.end(), k,
        [](const Position& p, const Key& at) { return p.key < at; });
    if (it == _positions.end() || it->key != k)
      it = _positions.insert(it, Position{k, spare(), false, false, kNoGap});
    else
      it->mixed = true;
    return *it;
  }

  std::vector<LlThread> spare() {
    if (_spare.empty()) return {};
    std::vector<LlThread> v = std::move(_spare.back());
    _spare.pop_back();
    v.clear();
    return v;
  }
  void spare(std::vector<LlThread>&& v) { _spare.push_back(std::move(v)); }

  Candidate best(const std::vector<LlThread>& threads) const {
    if (threads.empty()) return {};
    return {threads.front().arm, threads.front().lex, threads.front().root};
  }

  Candidate preferred(const Candidate& a, const Candidate& b) const {
    if (!a.valid()) return b;
    if (!b.valid()) return a;
    return _sim->earlier(b.arm, b.lex, a.arm, a.lex) ? b : a;
  }

  // The arm the choice takes once its readings agree on one; the others are
  // readings of it that stand where the one that is kept stands.
  void decide(std::uint16_t arm) {
    if (_decided || arm == kNoRow) return;
    _decided = true;
    _answer = arm;
    _chosenAt = _inspected;
    _chosenEnded = _ended;
    for (Position& p : _positions)
      std::erase_if(p.threads,
                    [&](const LlThread& t) { return t.root != arm; });
    std::erase_if(_positions,
                  [](const Position& p) { return p.threads.empty(); });
    for (Candidate* c : {&_completed, &_atEnd, &_provenEnd})
      if (c->root != arm) *c = {};
  }

  // Every reading that is left stands for one arm.
  void agree() {
    std::uint16_t selected = _completed.root;
    for (const Position& p : _positions)
      for (const LlThread& t : p.threads) {
        if (selected == kNoRow) selected = t.root;
        if (selected != t.root) return;
      }
    decide(selected);
  }

  // The decisions that every reading, and every complete one, went by, to the
  // first return into a caller guessed: false where the path stops there.
  // Once the readings part at `from`, they agree on nothing past it: the rest
  // are not walked.
  bool laid_path(bool afoot, std::vector<LlPlanEntry>& out,
                 LlHistory::Node from) {
    const LlHistory& arms = _sim->arms();
    const std::uint32_t floor = arms.depth(from);
    _stood = false;
    bool apart = false;
    for (const Candidate* c : {&_completed, &_atEnd, &_provenEnd})
      if (!apart && c->valid()) {
        _common = _stood ? arms.common(_common, c->arm) : c->arm;
        _stood = true;
        apart = arms.depth(_common) <= floor;
      }
    const auto walk = [&](const std::vector<LlThread>& threads) {
      for (const LlThread& t : threads)
        if (!apart)
          apart = !_sim->meet_arms(t.stack, t.arm, _common, _stood, floor);
    };
    if (afoot) {
      for (const Position& p : _positions) walk(p.threads);
    } else {
      walk(_final);
    }
    if (!_stood) return true;
    return ll_plan_path(arms, _common, out, from);
  }

  // The choice is made and readings stand: the plan is what they agree on, and
  // the episode is kept to read on for the choices the plan does not name.
  std::uint16_t hold() {
    auto* slot = ll_episode_session_slot<T>();
    _entries.clear();
    if (slot == nullptr || !slot->has_value() ||
        !laid_path(true, _entries, LlHistory::kRoot)) {
      _early = true;
      _final.clear();
      for (const Position& p : _positions)
        _final.insert(_final.end(), p.threads.begin(), p.threads.end());
      return conclude();
    }
    _laidAt = _common;
    _held = true;
    _heldBy = &slot->value();
    _heldRepairs = ctx().repaired.count();
    _heldSrc = ctx().src;
    _heldAt = ctx().at;
    return done();
  }

  // Whether the readings that stand agree on the choice the descent asks: then
  // the plan is laid as far as they go, and the episode waits again.
  std::uint16_t names_want() {
    if (_positions.front().key[0] < _want.at) return kLlUnplanned;
    thread_local std::vector<LlPlanEntry> path;
    path.clear();
    const bool open = laid_path(true, path, _laidAt);
    if (!_stood || _common == _laidAt) return kLlUnplanned;
    bool named = false;
    for (std::size_t i = 0; i < path.size() && !named; ++i)
      named = path[i].at == _want.at && path[i].lull == _want.lull &&
              path[i].decision == _want.decision;
    if (!named)
      return open && path.back().at <= _want.at ? kLlUnplanned : kLost;
    if (!ll_plan_extend<T>(_plan, path)) return kLost;
    _laidAt = _common;
    return kHeldOn;
  }

  // The deepest decision every reading of `threads` went by.
  LlHistory::Node common_of(const std::vector<LlThread>& threads) {
    bool met = false;
    LlHistory::Node common = LlHistory::kRoot;
    for (const LlThread& t : threads)
      if (!_sim->meet_arms(t.stack, t.arm, common, met, 0)) break;
    return common;
  }

  void note_death(const std::vector<LlThread>& threads, TextOffset at) {
    if (!_frontier.empty() && at < _frontierAt) return;
    _frontier = threads;
    _frontierAt = at;
    _frontierStep = _steps;
  }

  LlHistory::Node from() const { return _resumed ? _laidAt : LlHistory::kRoot; }

  std::uint16_t conclude() {
    _entries.clear();
    const LlHistory& arms = _sim->arms();
    if (_early) {
      laid_path(false, _entries, from());
      return done();
    }
    const Candidate winner = _tracked && _provenEnd.valid()
                                 ? _provenEnd
                                 : preferred(_completed, _atEnd);
    if (winner.valid()) {
      if (!_decided) {
        _decided = true;
        _answer = winner.root;
      }
      ll_plan_path(arms, winner.arm, _entries, from());
    } else if (!_decided) {
      _entries.push_back({ctx().at, ctx().lull, _decision, kNoRow});
      refuse_below();
    } else if (!_frontier.empty()) {
      const LlHistory::Node common = common_of(_frontier);
      LlHistory::Node parts = LlHistory::kRoot;
      bool deeper = false;
      for (const LlThread& t : _frontier) {
        _sim->each_arm(t.stack, t.arm, [&](LlHistory::Node arm) {
          deeper = arms.depth(arm) > arms.depth(common);
          parts = arm;
          return !deeper;
        });
        if (deeper) break;
      }
      ll_plan_dead_end(arms, common, deeper ? &parts : nullptr, _entries,
                       from());
    }
    return done();
  }

  // Every reading died and none chose an arm: all the arms of the choice
  // still stood among the last readings. A choice below that they went by is
  // refused as well where, after every way to it, all its arms still stood
  // among them too: an episode asked there would read the same text and
  // refuse it. The refusals hold while the descent stays on the way of the
  // readings: any arm of a refused choice keeps it there, and where the
  // generated automaton answers, `LlPlan::went` checks the arm it takes. An
  // episode that tracks lexical choices, a grammar with gap conditions or
  // indentation, and readings that leave a rule with no caller under them
  // keep the one refusal.
  void refuse_below() {
    if constexpr (T.ngapConds != 0 || kIndented<T>) {
      return;
    } else {
      if (_tracked || !_entryRule || _frontierStep != _steps ||
          _sim->stopped() || !_sim->choices(_frontier, _choices))
        return;
      const auto all = [](std::uint16_t decision) {
        const std::uint32_t n = T.atn.state(T.decisions[decision].state).nedges;
        return n < 2 || n > 63 ? std::uint64_t{0}
                               : (std::uint64_t{1} << n) - 1u;
      };
      bool rooted = false;
      for (const LlChoiceArms& k : _choices) {
        if (k.decision >= T.ndecisions || k.arms == 0) continue;
        if (k.decision == _decision && k.at == ctx().at &&
            k.lull == ctx().lull) {
          rooted = k.arms == all(_decision);
          continue;
        }
        const LlTable::Decision& d = T.decisions[k.decision];
        if (d.how == LlPredict::staged)
          _ways.push_back({k.at, k.lull, k.decision, k.arms});
        else if (d.how == LlPredict::adaptive && k.at < _frontierAt &&
                 k.arms == all(k.decision) && T.modeMulti[d.mode] == 0)
          _below.push_back({k.at, k.lull, k.decision, kNoRow, false, true});
      }
      if (!rooted || _below.empty()) {
        _below.clear();
        _ways.clear();
      }
    }
  }

  std::uint16_t done() {
    if (_resumed) {
      if (!ll_plan_extend<T>(_plan, _entries)) return kLost;
    } else {
      remember();
      // The memo keeps the choice asked; the refusals below are the plan's.
      _entries.insert(_entries.end(), _below.begin(), _below.end());
      _plan = ll_plan_install<T>(ctx(), _entries,
                                 _below.empty() ? nullptr : &_ways);
    }
    return _decided ? _answer : kNoRow;
  }

  // Whether the episode stands inside brackets, where its blanks join lines.
  bool joined() const { return kIndented<T> && _own.at.depth != 0; }

  // The skip configuration a reading's next token is crossed under here --
  // its joined blanks inside brackets only -- and a mode that has it: where
  // the grammar has regions, the first token it reads names it.
  std::pair<std::uint16_t, std::uint16_t> look_of(const LlThread& t) const {
    std::uint16_t mode = T.look_mode(dec());
    if constexpr (T.lookMode != nullptr) {
      if (const std::uint16_t own = ll_read_mode<T>(t.state); own != kNoRow)
        mode = own;
      return {ll_skips_of<T>(mode, joined()), mode};
    } else {
      return {0, mode};
    }
  }

  // `_lost`: the readings that wait for none of the elected terminals and
  // stand in a state of their own -- reduced past the token (not the stations
  // of a reader's reduction), or shifted apart from every reader or parted
  // from it before `gap`. A plain episode tracks neither: it takes them all.
  template <class Of>
  void strays(const std::vector<LlThread>& threads,
              const LlMask<T.termWords>& elected, TextOffset gap, Of&& of) {
    _acting.clear();
    _lost.clear();
    for (std::size_t k = 0; k < threads.size(); ++k) {
      if (!of(k)) continue;
      bool reads = false, asks = false, holds = false;
      for (const LlAtnEdge& edge : T.atn.outgoing(threads[k].state)) {
        asks = asks || edge.kind == LlEdgeKind::atom;
        reads = reads ||
                (edge.kind == LlEdgeKind::atom && elected.test(edge.label));
        holds = holds || edge.kind == LlEdgeKind::condition;
      }
      if (reads || holds)
        _acting.push_back(static_cast<std::uint32_t>(k));
      else if (asks)
        _lost.push_back(k);
    }
    if (!_tracked) return;
    std::erase_if(_lost, [&](std::size_t k) {
      const LlThread& stray = threads[k];
      if (stray.tag != kLlNoReduce)
        return std::any_of(
            _acting.begin(), _acting.end(), [&](std::uint32_t a) {
              return _sim->reduced_from(threads[a].tag, stray.tag);
            });
      const LlHistory& arms = _sim->arms();
      return std::any_of(_acting.begin(), _acting.end(), [&](std::uint32_t a) {
        if (threads[a].kernel != stray.kernel) return false;
        const auto shared = arms.depth(arms.common(stray.arm, threads[a].arm));
        return arms.depth(stray.arm) <= shared ||
               arms.event(stray.arm, shared).at >= gap;
      });
    });
  }

  // Whether trivia stands at `at` under the skip configuration `look`.
  bool blank_at(TextOffset at, std::uint16_t look) {
    if (at >= ctx().len) return false;
    std::vector<std::uint16_t> out;
    TextOffset inspected = at;
    bool ended = false;
    const Scan blank =
        ll_look_blank<T>(ctx(), at, look, joined(), out, inspected, ended);
    touch({{}, inspected, ended});
    return blank.term != kInvalidSymbol && blank.end != at;
  }

  // Whether the condition edge is the insertion of an implied token: the only
  // condition position that names a terminal.
  static bool implied_edge(std::uint32_t edge) {
    return T.edgeNode != nullptr && T.nodes[T.edgeNode[edge]].term != kNoRow;
  }

  // Whether trivia stands at `at` under the skip configuration of some reading.
  bool blank_for_any(const std::vector<LlThread>& threads, TextOffset at) {
    std::vector<std::uint16_t> seen;
    for (const LlThread& t : threads) {
      const auto look = look_of(t);
      if (std::find(seen.begin(), seen.end(), look.first) != seen.end())
        continue;
      seen.push_back(look.first);
      if (blank_at(at, look.second)) return true;
    }
    return false;
  }

  // Whether some reading crosses the gap under another skip configuration than
  // the lead's.
  bool crosses_apart(const std::vector<LlThread>& threads) const {
    if constexpr (T.lookMode != nullptr) {
      const auto lead = look_of(threads.front()).first;
      for (const LlThread& t : threads)
        if (look_of(t).first != lead) return true;
    }
    return false;
  }

  // Whether the readings that cross the gap under another skip configuration
  // than the lead's reach the same token: where they do not, the tracked
  // episode reads each group under its own.
  bool one_crossing(const std::vector<LlThread>& threads,
                    const LlMask<T.termWords>& viable, TextOffset from,
                    const LlLookRead& lead,
                    const std::vector<std::uint16_t>& read, const IndentAt& at,
                    TextOffset held) {
    const auto first = look_of(threads.front()).first;
    std::vector<std::uint16_t> seen{first};
    const IndentAt leadAt = _own.at;
    const TextOffset leadHeld = _own.held;
    std::vector<std::uint16_t> again;
    for (const LlThread& t : threads) {
      const auto other = look_of(t);
      if (std::find(seen.begin(), seen.end(), other.first) != seen.end())
        continue;
      seen.push_back(other.first);
      LlLookRead next;
      TextOffset tokenFrom = from;
      _own.at = at;
      _own.held = held;
      const bool found =
          ll_look<T>(ctx(), tokenFrom, viable, again, other.second, &next,
                     &_own, false, dec().mode);
      touch(next);
      if (!found || next.token.begin != lead.token.begin ||
          next.token.end != lead.token.end || again != read) {
        _own.at = leadAt;
        _own.held = leadHeld;
        return false;
      }
    }
    _own.at = leadAt;
    _own.held = leadHeld;
    return true;
  }

  // Whether the readings of one spelling that the lexer ranks apart are each
  // read by some reading: that is a lexical choice, and the tracked episode
  // elects it.
  bool lexical_choice(const std::vector<std::uint16_t>& read, std::size_t top,
                      const std::vector<LlThread>& threads) const {
    if (top == 0) return false;
    bool read_top = false, read_rest = false;
    for (const LlThread& t : threads) {
      for (const LlAtnEdge& e : T.atn.outgoing(t.state)) {
        if (e.kind != LlEdgeKind::atom) continue;
        const auto at = std::find(read.begin(), read.end(), e.label);
        if (at == read.end()) continue;
        (static_cast<std::size_t>(at - read.begin()) < top ? read_top
                                                           : read_rest) = true;
        if (read_top && read_rest) return true;
      }
    }
    return false;
  }

  // Identical futures do not need a second, lexically worse path: the readings
  // at one state with one stack and one arm are the best of them.
  void merge(std::vector<LlThread>& threads, bool mixed) {
    if (mixed)
      std::stable_sort(threads.begin(), threads.end(),
                       [&](const LlThread& a, const LlThread& b) {
                         return _sim->earlier(a, b);
                       });
    _met.clear();
    std::size_t kept = 0;
    for (std::size_t k = 0; k < threads.size(); ++k) {
      const LlThread& t = threads[k];
      bool fresh = false;
      std::uint32_t& at = _met.at(
          LlMeetKey{t.state, _sim->stacks().canon(t.stack), t.root}, fresh);
      if (!fresh) {
        _kernels.unite(t.kernel, threads[at].kernel);
        continue;
      }
      at = static_cast<std::uint32_t>(kept);
      threads[kept++] = t;
    }
    threads.resize(kept);
  }

  Verdict visit(Position& p) {
    const TextOffset from = p.key[0];
    const std::uint32_t lull = p.key[4];
    if constexpr (kIndented<T>) {
      _own.at = {p.key[1], p.key[2]};
      _own.held = p.key[3];
    }
    std::vector<LlThread>& threads = p.threads;
    if (_tracked) merge(threads, p.mixed);
    const bool deferred = T.ngapConds != 0 && _sim->needs_filter(threads);
    // Two readings at one place with one stack: no token further on tells
    // them apart, so the text is ambiguous here. The arms are ordered, and the
    // first that admits it is the reading the grammar states.
    if (!_decided && !p.first && _positions.empty() && !deferred &&
        ll_stuck_apart(*_sim, threads)) {
      decide(preferred(_completed, best(threads)).root);
      std::erase_if(threads,
                    [&](const LlThread& t) { return t.root != _answer; });
      if (threads.empty()) return Verdict::go;
      if (threads.size() == 1 && _sim->standing(threads.front().stack) == 1) {
        _final = threads;
        _early = true;
        return Verdict::done;
      }
    }
    bool anyRead = false;
    for (const LlThread& t : threads)
      anyRead = anyRead || T.atn.has(t.state, LlEdgeKind::atom);
    // Nothing left to read: the readings agree on the whole text, and the
    // arms are ordered -- the first one wins, which is what it wrote.
    if (!deferred && !anyRead) {
      _completed = preferred(_completed, best(threads));
      return Verdict::go;
    }
    const TextOffset gapFrom = p.first ? ctx().at : from;
    return _tracked ? visit_tracked(p, gapFrom, lull)
                    : visit_plain(p, deferred, gapFrom, from, lull);
  }

  // Whether what follows the condition of `edge` reads nothing at the token
  // that stands after the gap, as the descent standing at the condition reads
  // that token: what follows then joins no election. An implied token is out
  // of reach where nothing its condition names or its normal continuation
  // reads begins there, and the condition does not hold on no token at all.
  // Any other condition refuses the token: a literal behind a gap that refuses
  // it would otherwise reserve its spelling from a pattern another reading
  // takes there. The condition may read past what the episode lexes there;
  // those bytes count among the episode's, so the memo answers only text that
  // reads the same.
  bool held_back(std::uint32_t edge, TextOffset from, TextOffset gapFrom) {
    const std::uint16_t node = T.edgeNode[edge];
    if (node == kNoRow) return false;
    const bool implied = T.nodes[node].term != kNoRow;
    LlAhead a = ll_ahead(ctx());
    a.p = from;
    a.head = false;
    Token t = ll_ahead_peek<T>(ctx(), ctx().indentArena, a, T.nodes[node].mode);
    if ((t.sym == kInvalidSymbol) != implied) return false;
    if (implied) t = Token{t.begin, t.begin, kInvalidSymbol};
    LlLookRead sink;
    const bool holds = ll_predict_condition<T>(ctx(), T.atn.edge(edge).label,
                                               t, gapFrom, sink);
    touch(sink);
    return !holds;
  }

  // Whether some reading takes `token`, which an implied position reads where
  // the crossing reads the terminals `read` up to `end`: a longer token is its
  // own, and any other is the crossing's.
  static bool takes(const std::vector<LlThread>& threads, const Token& token,
                    TextOffset end, const std::vector<std::uint16_t>& read) {
    for (const LlThread& t : threads) {
      for (const LlAtnEdge& e : T.atn.outgoing(t.state)) {
        if (e.kind != LlEdgeKind::atom) continue;
        if (token.end > end
                ? e.label == token.sym
                : std::find(read.begin(), read.end(), e.label) != read.end())
          return true;
      }
    }
    return false;
  }

  // Whether some reading waits on an implied token out of reach here.
  bool apart_pending(const std::vector<LlThread>& threads, TextOffset from,
                     TextOffset gapFrom) {
    for (const LlThread& t : threads) {
      const LlAtnState& s = T.atn.state(t.state);
      for (std::uint32_t i = 0; i < s.nedges; ++i)
        if (T.atn.edge(s.edge + i).kind == LlEdgeKind::condition &&
            implied_edge(s.edge + i) && held_back(s.edge + i, from, gapFrom))
          return true;
    }
    return false;
  }

  // The terminals reading `t` reads next, added to `into`: false where none.
  static bool atoms_into(const LlThread& t, LlMask<T.termWords>& into) {
    bool any = false;
    for (const LlAtnEdge& e : T.atn.outgoing(t.state))
      if (e.kind == LlEdgeKind::atom) {
        into.set(e.label);
        any = true;
      }
    return any;
  }

  // What the pending guards of `threads` let a gap read, added to `into`: the
  // terminals they reach where the token there does not hold them back, else
  // the mode's -- the surrounding choice's mode may contain unrelated
  // keywords, which must not reserve their identifier spelling here. `any`: a
  // terminal is already read.
  void guard_terms(const std::vector<LlThread>& threads,
                   LlMask<T.termWords>& into, bool any, TextOffset from,
                   TextOffset gapFrom) {
    _sim->possible_terms(
        threads,
        [&](std::uint16_t term) {
          into.set(term);
          any = true;
        },
        [&](std::uint32_t edge) { return held_back(edge, from, gapFrom); });
    if (any) return;
    const auto mode = T.look_mode(dec());
    for (std::uint16_t term = 1; term < T.nterms; ++term)
      if (T.mode_admits(mode, term)) into.set(term);
  }

  Verdict visit_plain(Position& p, bool deferred, TextOffset gapFrom,
                      TextOffset from, std::uint32_t lull) {
    auto& scratch = ll_scratch<T.termWords>();
    LlMask<T.termWords>& viable = scratch.viable;
    std::vector<std::uint16_t>& read = scratch.read;
    std::vector<LlThread>& threads = p.threads;
    Crossing x{0, 0, gapFrom, lull, {}, 0};
    std::size_t top = 0;
    // The token in hand was elected by the descent's alphabet, which takes in
    // what follows an insertion: read again where none is within reach.
    const bool again =
        p.first && deferred && apart_pending(threads, from, gapFrom);
    if (p.first && !again) {
      x.begin = ctx().la.begin;
      x.end = ctx().la.end;
      read = _firstTerms;
      top = _firstTop;
    } else {
      viable.reset();
      bool any = false;
      for (const LlThread& t : threads) any = atoms_into(t, viable) || any;
      if (deferred) guard_terms(threads, viable, any, from, gapFrom);
      const std::uint16_t look = look_of(threads.front()).second;
      LlLookRead next;
      TextOffset tokenFrom = from;
      const IndentAt preAt = _own.at;
      const TextOffset preHeld = _own.held;
      const bool found = ll_look<T>(ctx(), tokenFrom, viable, read, look, &next,
                                    &_own, false, dec().mode);
      touch(next);
      if (!found) {
        if (crosses_apart(threads)) return Verdict::restart;
        _whole = false;
        note_death(threads, from);
        return Verdict::go;
      }
      if (next.different) return Verdict::restart;
      top = next.top;
      x.begin = next.token.begin;
      x.end = next.token.end;
      if constexpr (T.lookMode != nullptr)
        if (!one_crossing(threads, viable, from, next, read, preAt, preHeld))
          return Verdict::restart;
    }
    if (lexical_choice(read, top, threads)) return Verdict::restart;
    if (x.begin == gapFrom && blank_for_any(threads, gapFrom)) {
      LlMask<T.termWords> elected;
      for (const std::uint16_t term : read)
        if (term < T.nterms) elected.set(term);
      strays(threads, elected, p.gap, [](std::size_t) { return true; });
      if (!_lost.empty()) return Verdict::restart;
    }
    x.at = _own.at;
    x.held = _own.held;
    step(threads, x, read, LlHistory::kRoot);
    return Verdict::go;
  }

  // The readings of every group, each group crossing the gap under the
  // alphabet of its own -- a newline one group reads as a token is trivia to
  // another: a reading that read the token and what its closure grows share
  // one, as an LR state's items do. Groups whose tokens begin at one place
  // choose it once, each keeping the terminals of its own.
  Verdict visit_tracked(Position& p, TextOffset gapFrom, std::uint32_t lull) {
    const TextOffset from = p.key[0];
    std::vector<LlThread>& threads = p.threads;
    // Ordered, since the groups are visited in the order of their keys.
    std::map<std::pair<std::uint32_t, std::uint16_t>, std::uint32_t> groupIndex;
    _groupOf.assign(threads.size(), 0);
    _groupLook.clear();
    for (std::size_t k = 0; k < threads.size(); ++k) {
      const auto look = look_of(threads[k]);
      const auto [at, fresh] = groupIndex.try_emplace(
          std::pair{_kernels.find(threads[k].kernel), look.first},
          static_cast<std::uint32_t>(groupIndex.size()));
      if (fresh) _groupLook.push_back(look.second);
      _groupOf[k] = at->second;
    }
    _members.resize(groupIndex.size());
    for (auto& group : _members) group.clear();
    for (std::size_t k = 0; k < threads.size(); ++k)
      _members[_groupOf[k]].push_back(threads[k]);
    _readingsOf.clear();
    // Groups that cross one gap with one alphabet read the text once.
    auto& seen = _alphabets;
    seen.clear();
    std::vector<std::uint16_t>& read = ll_scratch<T.termWords>().read;
    auto& lengths = _lengths;
    auto& places = _places;
    places.clear();
    places.resize(groupIndex.size());
    for (const auto& indexed : groupIndex) {
      const std::uint32_t g = indexed.second;
      Place& place = places[g];
      for (const LlThread& thread : _members[g])
        atoms_into(thread, place.atoms);
      place.own = place.atoms;
      if (T.ngapConds != 0 && _sim->needs_filter(_members[g]))
        guard_terms(_members[g], place.own, false, from, gapFrom);
      if (p.first) {
        place.next.token = ctx().la;
        place.found = true;
      } else {
        TextOffset tokenFrom = from;
        place.found =
            ll_look<T>(ctx(), tokenFrom, place.own, read, _groupLook[g],
                       &place.next, &_own, false, dec().mode);
        touch(place.next);
        if (!place.found) {
          _whole = false;
          note_death(_members[g], from);
          continue;
        }
      }
      place.begin = p.first ? ctx().la.begin : place.next.token.begin;
      place.open =
          place.begin < ctx().len && !(place.next.token.sym < T.nterms &&
                                       T.terms[place.next.token.sym].laid != 0);
    }
    // What the open places at one offset can read next, whichever group.
    auto& heard = _heard;
    heard.clear();
    const auto hear = [&](const Place& x) {
      if (!x.open) return;
      auto at = std::find_if(heard.begin(), heard.end(),
                             [&](const auto& h) { return h.first == x.begin; });
      if (at == heard.end())
        at = heard.emplace(heard.end(), x.begin, LlMask<T.termWords>{});
      at->second.add(x.atoms);
    };
    for (const Place& other : places) hear(other);
    auto& order = _visitOrder;
    order.clear();
    for (const auto& indexed : groupIndex) order.push_back(indexed.second);
    for (std::size_t turn = 0; turn < order.size(); ++turn) {
      const std::uint32_t g = order[turn];
      const Place place = places[g];
      if (!place.found) continue;
      const TextOffset begin = place.begin;
      if (!place.open) {
        _readingsOf.push_back(
            {place.next.token.sym, begin, place.next.token.end, g});
        auto stood = std::pair{key(begin, lull), LlThreadSetKey{}};
        ll_thread_set_key(*_sim, _members[g], stood.second);
        if (std::find(_ends.begin(), _ends.end(), stood) != _ends.end()) {
          _atEnd = preferred(_atEnd, best(threads));
          _halted = true;
          return Verdict::done;
        }
        _ends.push_back(std::move(stood));
        continue;
      }
      LlMask<T.termWords> alphabet = place.own;
      alphabet.add(std::find_if(heard.begin(), heard.end(), [&](const auto& h) {
                     return h.first == begin;
                   })->second);
      const auto again =
          std::find_if(seen.begin(), seen.end(), [&](const auto& e) {
            return std::get<0>(e) == alphabet && std::get<1>(e) == begin;
          });
      if (again != seen.end()) {
        const auto first = std::get<2>(*again);
        for (std::size_t k = first;
             k < _readingsOf.size() &&
             _readingsOf[k].group == _readingsOf[first].group;
             ++k)
          _readingsOf.push_back({_readingsOf[k].term, begin, _readingsOf[k].end,
                                 g, _readingsOf[k].tier});
      } else {
        TextOffset inspected = begin;
        bool ended = false;
        ll_match_in<T, true>(ctx().src, ctx().len, begin, alphabet, read,
                             &inspected, &ended, nullptr, dec().mode, nullptr,
                             &lengths);
        touch({{}, inspected, ended});
        seen.emplace_back(alphabet, begin, _readingsOf.size());
        for (const auto& reading : lengths)
          _readingsOf.push_back(
              {reading.term, begin, reading.end, g, reading.tier});
      }
      // A token that owns the blank before it is trivia to the readings that
      // wait for none of it: they cross the blank in a group of their own.
      if (begin != gapFrom || !blank_at(gapFrom, _groupLook[g])) continue;
      LlMask<T.termWords> elected, own;
      for (const Reading& r : _readingsOf)
        if (r.group == g && r.term < T.nterms) elected.set(r.term);
      strays(threads, elected, p.gap,
             [&](std::size_t k) { return _groupOf[k] == g; });
      if (_lost.empty()) continue;
      for (const std::size_t k : _lost) atoms_into(threads[k], own);
      Place apart;
      apart.atoms = apart.own = own;
      TextOffset tokenFrom = gapFrom;
      apart.found = ll_look<T>(ctx(), tokenFrom, apart.own, read, _groupLook[g],
                               &apart.next, &_own, false, dec().mode);
      touch(apart.next);
      if (!apart.found || apart.next.token.begin == begin) continue;
      apart.begin = apart.next.token.begin;
      apart.open =
          apart.begin < ctx().len && !(apart.next.token.sym < T.nterms &&
                                       T.terms[apart.next.token.sym].laid != 0);
      hear(apart);
      const auto fresh = static_cast<std::uint32_t>(places.size());
      places.push_back(apart);
      _members.emplace_back();
      _groupLook.push_back(_groupLook[g]);
      for (const std::size_t k : _lost) {
        _groupOf[k] = fresh;
        _members[fresh].push_back(threads[k]);
      }
      order.push_back(fresh);
    }
    std::erase_if(_readingsOf, [&](const Reading& r) {
      return r.term != kEofSymbol && r.term < T.nterms &&
             T.terms[r.term].laid == 0 && !places[r.group].own.test(r.term);
    });
    const auto shiftKey = [](SymbolId term) {
      if constexpr (kIndented<T>)
        return (unsigned{T.terms[term].laid} << 8) |
               static_cast<unsigned>(T.terms[term].delim + 1);
      else
        return 0u;
    };
    const auto rank = [](const Reading& reading) {
      return reading.term < T.nterms
                 ? lex_rank(reading.tier, T.terms[reading.term].trivia)
                 : 0u;
    };
    // A reading is a lexical choice, ranked before any arm: the token the
    // descent's mode selected leads at the first position, then maximal
    // munch without discarding a shorter viable tokenisation, then the
    // lexer's rank. The terminals of one rank at one length are one token,
    // which the arms decide.
    std::sort(
        _readingsOf.begin(), _readingsOf.end(),
        [&](const Reading& a, const Reading& b) {
          if (p.first && (a.end == ctx().la.end) != (b.end == ctx().la.end))
            return a.end == ctx().la.end;
          if (a.end != b.end) return a.end > b.end;
          if (rank(a) != rank(b)) return rank(a) > rank(b);
          if (a.group != b.group) return a.group < b.group;
          if (shiftKey(a.term) != shiftKey(b.term))
            return shiftKey(a.term) < shiftKey(b.term);
          return a.term < b.term;
        });
    // The readings by group and history, each history in the order the
    // readings had. Kept as a copy: a step may grow the positions, `threads`
    // among them.
    auto& histories = _histories;
    histories.clear();
    for (std::size_t k = 0; k < threads.size(); ++k)
      histories.push_back(
          {_groupOf[k], static_cast<std::uint32_t>(k), threads[k]});
    std::sort(histories.begin(), histories.end(),
              [](const Aged& a, const Aged& b) {
                if (a.group != b.group) return a.group < b.group;
                if (a.thread.lex != b.thread.lex)
                  return a.thread.lex < b.thread.lex;
                return a.order < b.order;
              });
    // The sort leaves the readings of one choice, an end and a rank, side by
    // side at their place: the choices of a place are numbered as the walk
    // meets them, and two places are not ranked against each other.
    auto& tails = _tails;
    tails.clear();
    const auto choose = [&](const Reading& r) {
      const auto at =
          std::find_if(tails.begin(), tails.end(),
                       [&](const Tail& t) { return t.begin == r.begin; });
      if (at == tails.end()) {
        tails.push_back({r.begin, r.end, rank(r), 0});
        return std::uint32_t{0};
      }
      if (at->end != r.end || at->rank != rank(r)) {
        at->end = r.end;
        at->rank = rank(r);
        ++at->choice;
      }
      return at->choice;
    };
    bool forked = false;
    for (const Reading& r : _readingsOf) forked = choose(r) != 0 || forked;
    tails.clear();
    auto& original = _original;
    for (std::size_t i = 0; i < _readingsOf.size();) {
      const std::uint32_t choice = choose(_readingsOf[i]);
      const TextOffset begin = _readingsOf[i].begin;
      const TextOffset end = _readingsOf[i].end;
      const std::uint32_t group = _readingsOf[i].group;
      const auto ranked = rank(_readingsOf[i]);
      const auto effect = shiftKey(_readingsOf[i].term);
      original.clear();
      do {
        original.push_back(_readingsOf[i++].term);
      } while (i < _readingsOf.size() && _readingsOf[i].end == end &&
               rank(_readingsOf[i]) == ranked &&
               _readingsOf[i].group == group &&
               shiftKey(_readingsOf[i].term) == effect);
      const Crossing x{begin, end, gapFrom, lull, _own.at, _own.held};
      auto history = std::lower_bound(
          histories.begin(), histories.end(), group,
          [](const Aged& a, std::uint32_t g) { return a.group < g; });
      while (history != histories.end() && history->group == group) {
        const std::uint32_t parentOrder = history->thread.lex;
        _lent.clear();
        for (; history != histories.end() && history->group == group &&
               history->thread.lex == parentOrder;
             ++history)
          _lent.push_back(history->thread);
        read.assign(original.begin(), original.end());
        step(_lent, x, read,
             forked ? _sim->lexes().append(parentOrder, choice) : parentOrder);
      }
    }
    return Verdict::go;
  }

  // The readings of one history cross the token `read` spells over
  // [x.begin, x.end): those that the conditions refuse die, those that read
  // it stand past it, and wait for the text there.
  void step(std::vector<LlThread>& threads, const Crossing& x,
            std::vector<std::uint16_t>& read, LlHistory::Node order) {
    _stepping.swap(threads);
    if constexpr (T.ngapConds != 0) {
      _sim->at(x.gapFrom, x.lull);
      _before = _stepping;
      LlLookRead sink;
      // An implied token is inserted only where no reading takes the token
      // its position reads: a normal continuation blocks the insertion, even
      // one that fails later. The readings cross without insertions first,
      // then again with those that nothing they read blocks.
      _insertions.clear();
      bool first = true;
      const auto crosses = [&](std::uint16_t condition, std::uint32_t edge) {
        const Token token{x.begin, x.end, read.front()};
        if (!implied_edge(edge))
          return ll_predict_condition<T>(ctx(), condition, token, x.gapFrom,
                                         sink);
        const auto at =
            std::find_if(_insertions.begin(), _insertions.end(),
                         [&](const auto& i) { return i.first == edge; });
        if (!first) return at != _insertions.end();
        const Token own =
            ll_normal_token<T>(ctx(), token, T.nodes[T.edgeNode[edge]].mode);
        if (at == _insertions.end() &&
            ll_predict_condition<T>(ctx(), condition, own, x.gapFrom, sink))
          _insertions.emplace_back(edge, own);
        return false;
      };
      _sim->filter(_stepping, crosses);
      std::erase_if(_insertions, [&](const auto& i) {
        return takes(_stepping, i.second, x.end, read);
      });
      if (!_insertions.empty()) {
        first = false;
        _stepping = _before;
        _sim->filter(_stepping, crosses);
      }
      touch(sink);
      if (_stepping.empty()) {
        note_death(_before, x.begin);
        return;
      }
      // Once the consumed prefix leaves only one arm, that is the construct
      // the author started. A guard farther inside it belongs to the descent
      // and its local repair; looking through it here could discard the whole
      // construct before that repair gets to read the sound suffix.
      if (!_tracked) decide(ll_settled_arm(_stepping));
    }
    if constexpr (kIndented<T>) {
      // A guard may narrow a broad physical EOF to the pending dedent.
      if (read.front() == kEofSymbol &&
          ll_end_dedents<T>(_own.arena, x.at, false) &&
          ll_dedent_over_end<T>(_stepping))
        read.assign(1, kLaid<T, kLaidDedent>);
    }
    // What one group shifts through one action is one LR state, read under
    // one alphabet: two reductions before one token are a conflict, and its
    // stacks part.
    if (++_round == 0) {
      _actions.assign(_actions.size(), {});
      _round = 1;
    }
    _origins.clear();
    for (const LlThread& t : _stepping) {
      std::uint32_t kernel = t.kernel;
      LlHistory::Node lex = t.lex;
      if (_tracked) {
        lex = order;
        const std::size_t slot = t.tag == kLlNoReduce ? 0u : t.tag;
        if (slot >= _actions.size()) _actions.resize(slot + 1);
        Action& action = _actions[slot];
        if (action.round != _round) action = {_round, _kernels.make()};
        kernel = action.kernel;
      }
      _sim->advance(t, read.data(), read.size(), lex, kernel, _origins);
    }
    if (_origins.empty()) {
      // Refusing EOF is a failed reading too. Choosing its first arm would
      // commit an optional operator before recovery can price the closer the
      // caller owes. Preserve only the decisions all failed readings share.
      if constexpr (T.ngapConds != 0)
        note_death(_before, x.begin);
      else
        note_death(_stepping, x.begin);
      return;
    }
    std::uint32_t lull = 0;
    if constexpr (kIndented<T>) lull = x.end != x.begin ? 0 : x.lull + 1;
    _sim->at(x.end, lull);
    _sim->close(_origins, _grown);
    if (_tracked) {
      for (const auto& [a, b] : _sim->joins()) _kernels.unite(a, b);
      _sim->joins().clear();
      // EOF can complete the entry while an unfinished shorter reading still
      // shares this position. That success must not become a failed second
      // EOF step and lose to the repetition's recovery fallback.
      if (_entryRule && read.front() == kEofSymbol && x.begin == ctx().len &&
          x.end == ctx().len)
        for (const LlThread& thread : _grown) {
          const LlAtnState& s = T.atn.state(thread.state);
          if (s.kind == LlAtnKind::ruleStop && s.rule == 0 &&
              thread.stack == kLlCtxNone)
            _provenEnd =
                preferred(_provenEnd, {thread.arm, thread.lex, thread.root});
        }
    }
    if constexpr (kIndented<T>) {
      _own.at = x.at;
      _own.held = x.held;
      indent_shift<T>(_own.arena, _own.at, {x.begin, x.end, read.front()});
      if (x.end != x.begin) _own.held = x.end;
    }
    Position& there = insert(key(x.end, lull));
    there.gap = std::min(there.gap, x.gapFrom);
    there.threads.insert(there.threads.end(), _grown.begin(), _grown.end());
    if constexpr (kIndented<T>) {
      _own.at = x.at;
      _own.held = x.held;
    }
  }

  LlContext& ctx() const { return *_cp; }
  const LlTable::Decision& dec() const { return T.decisions[_decision]; }

  LlContext* _cp = nullptr;
  std::uint16_t _decision = 0;
  std::optional<LlExactWalk<T>> _walk;
  LlEpisodeSim* _sim = nullptr;
  LlLookIndent _own;
  bool _tracked = false;
  std::vector<std::uint16_t> _firstTerms;
  std::size_t _firstTop = 0;
  std::vector<Position> _positions;
  std::vector<std::vector<LlThread>> _spare;
  std::vector<LlPlanEntry> _entries;
  // The refusals below the choice asked, and the ways they hold on.
  std::vector<LlPlanEntry> _below;
  std::vector<LlPlanWay> _ways;
  std::vector<LlChoiceArms> _choices;
  std::vector<LlThread> _frontier, _final, _stepping, _before, _origins, _grown,
      _lent;
  // The implied tokens a step inserts: each edge, with the token it reads.
  std::vector<std::pair<std::uint32_t, Token>> _insertions;
  // Whether the last laid path met a reading or a candidate.
  bool _stood = false;
  TextOffset _frontierAt = 0;
  // The step that met the frontier: where it is the last step, the frontier
  // is every reading that was left.
  std::uint64_t _frontierStep = 0;
  bool _decided = false, _early = false, _halted = false;
  std::uint16_t _answer = kNoRow;
  Candidate _completed, _atEnd, _provenEnd;
  Kernels _kernels;
  LlStamped<LlMeetKey, LlMeetHash> _met;
  std::vector<std::pair<Key, LlThreadSetKey>> _ends;
  std::vector<std::uint32_t> _groupOf, _acting;
  std::vector<std::size_t> _lost;
  std::vector<std::uint16_t> _groupLook;
  std::vector<std::vector<LlThread>> _members;
  std::vector<Reading> _readingsOf;
  // The rest of what a reading step of tracked groups makes, kept between
  // steps for its room and cleared where it is made.
  std::vector<std::tuple<LlMask<T.termWords>, TextOffset, std::size_t>>
      _alphabets;
  std::vector<LlLexReading> _lengths;
  std::vector<Place> _places;
  std::vector<std::pair<TextOffset, LlMask<T.termWords>>> _heard;
  std::vector<std::uint32_t> _visitOrder;
  std::vector<Aged> _histories;
  std::vector<Tail> _tails;
  std::vector<std::uint16_t> _original;
  // The kernel a step made for each reduce tag, valid in the round it was
  // made; tags start at one, and slot zero is the thread with none.
  struct Action {
    std::uint32_t round = 0, kernel = 0;
  };
  std::vector<Action> _actions;
  std::uint32_t _round = 0;
  std::uint64_t _steps = 0;
  // The episode kept after its choice was made: the parse and the place it
  // was kept in, the plan entries it has laid, and the choice asked of it.
  struct Want {
    TextOffset at;
    std::uint32_t lull;
    std::uint16_t decision;
  };
  bool _held = false, _resumed = false, _entryRule = false;
  const LlEpisodeCache* _heldBy = nullptr;
  std::uint32_t _heldRepairs = 0;
  const char* _heldSrc = nullptr;
  TextOffset _heldAt = 0;
  LlPlan::Laid _plan = 0;
  LlHistory::Node _common = LlHistory::kRoot, _laidAt = LlHistory::kRoot;
  Want _want{};
  // How far into the text the episode looked, and whether it saw the end.
  LlEpisodeMemo* _kept = nullptr;
  Question _q{};
  TextOffset _inspected = 0, _chosenAt = 0;
  bool _ended = false, _chosenEnded = false, _whole = true;
};

// One engine for all the episodes of a thread: they run one at a time, and the
// room each leaves is the next one's.
template <const auto& T>
inline LlEpisode<T>& ll_episode_engine() {
  thread_local LlEpisode<T> engine;
  return engine;
}

}  // namespace cursiv
