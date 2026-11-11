// The NFA one terminal's spelling is built into, before the subsets make a
// DFA of it. A refusal is recorded as a hole and narrowed later, so a state's
// exits are not all known when it is created.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/utils/RoundMarks.hpp"

namespace cursiv::engine {

struct LexNfaState {
  int set = -1;  // charset index consumed, -1 for an epsilon state
  int t1 = -1;
  int t2 = -1;
  int accept = -1;  // terminal id
  // A class subtracted from `set`: what a zero-width refusal in front of this
  // edge leaves of it. A DFA has no non-consuming edge, so the refusal is
  // pushed onto the edges that follow it.
  int minus = -1;
  // ...and on an accept, the same refusal one byte past the token: the match
  // stands only if the byte after it is outside this class.
  int bound = -1;
  // ...and where that refusal is a scalar's rest, `plainBound` is the bound the
  // accept would have without it.
  int plainBound = -1;
  bool loneLeadBound = false;
  // The token is handed to this relay here, or -1.
  int relay = -1;
  // An accept that a literal's branch reads.
  bool lit = false;
  // The edge consumes a lead byte that starts no scalar where the rest of one
  // follows: the refusal node naming that rest, else -1. The automaton keeps
  // what follows it in a state of its own until the rest is ruled out.
  int loneLead = -1;
  std::uint16_t loneLeadTerm = 0;
  // The guard this hole stands for, or -1.
  int guard = -1;

  int plain() const { return loneLeadBound ? plainBound : bound; }
};

// The bytes a consuming state reads: its set, less what a refusal subtracts.
inline ByteSet lex_edge_bytes(const ByteSet* sets, const LexNfaState& st) {
  ByteSet on = sets[st.set];
  if (st.minus >= 0) on.andnot(sets[st.minus]);
  return on;
}

// A relay while the graph is built: its row, and the NFA state the walk
// resumes from past the echo -- a DFA state only once the subsets exist.
struct LexRelayBuild {
  LexRelayGen row;
  int resumeOut = -1;
  // A nested relay's opener and closer, where each side is one literal; empty
  // otherwise.
  std::string open, close;
};

// A refusal recorded while the graph is still being built: `hole` is the state
// standing in for it, `out` the continuation it narrows, `node` the word it
// refuses.
struct LexNfaGuard {
  int hole = -1;
  int out = -1;
  std::uint16_t node = 0;
  std::uint16_t term = 0;
};

struct LexNfaPool {
  // Far past what a table of 65 535 states is built from, and short of the
  // memory a counted repetition takes before any other limit answers.
  static constexpr std::size_t kMostStates = std::size_t{1} << 24;

  std::vector<LexNfaState> s;
  std::vector<LexNfaGuard> guards;
  std::vector<LexRelayBuild> relays;
  std::uint16_t term = 0;  // the terminal being built, for its refusals

  int push(const LexNfaState& st) {
    if (s.size() >= kMostStates)
      throw LowerRefused(LowerFail::tokenGraphTooLarge);
    s.push_back(st);
    return static_cast<int>(s.size()) - 1;
  }
  int chars(int set, int next) { return push({.set = set, .t1 = next}); }
  int chars_lone_lead(int set, int word, int next) {
    return push(
        {.set = set, .t1 = next, .loneLead = word, .loneLeadTerm = term});
  }
  int split(int a, int b) { return push({.t1 = a, .t2 = b}); }
  int match(int accepts, bool lit = false) {
    return push({.accept = accepts, .lit = lit});
  }
  int relay(int k) { return push({.relay = k}); }
  // One sink per terminal and class: two literals of one terminal ending on
  // the same boundary end up in one DFA state, as they do on a plain match.
  int match_bounded(int accepts, int bound, bool lit) {
    return sink({.accept = accepts, .bound = bound, .lit = lit});
  }
  int match_lone_lead(int accepts, int bound, int plain, bool lit = false) {
    return sink({.accept = accepts,
                 .bound = bound,
                 .plainBound = plain,
                 .loneLeadBound = true,
                 .lit = lit});
  }
  // An epsilon state whose target is filled in later: a run's walk states are
  // mutually reachable, so no build order can hand them all a known exit.
  int hole() { return push({}); }

  // A guard waits until loops have their exits.
  int guard(std::uint16_t node, int out) {
    const auto [at, fresh] =
        _guardOf.try_emplace({node, out}, static_cast<int>(s.size()));
    if (!fresh) return at->second;
    const int h = push({.guard = static_cast<int>(guards.size())});
    guards.push_back({h, out, node, term});
    return h;
  }

  // What `roots` reach without consuming, in depth-first preorder, once each.
  std::vector<int> eclose(std::span<const int> roots) const {
    _seen.next();
    std::vector<int> set;
    for (const int root : roots) {
      if (root < 0) continue;
      _todo.assign(1, root);
      while (!_todo.empty()) {
        const int at = _todo.back();
        _todo.pop_back();
        if (!_seen.first(static_cast<std::size_t>(at))) continue;
        set.push_back(at);
        const LexNfaState& st = s[static_cast<std::size_t>(at)];
        if (st.set >= 0) continue;
        if (st.t2 >= 0) _todo.push_back(st.t2);
        if (st.t1 >= 0) _todo.push_back(st.t1);
      }
    }
    return set;
  }
  std::vector<int> eclose(int at) const {
    return eclose(std::span<const int>(&at, 1));
  }
  // The same, ascending; `roots` is one state or several.
  std::vector<int> sorted_closure(const auto& roots) const {
    std::vector<int> set = eclose(roots);
    std::ranges::sort(set);
    return set;
  }

  // The walk of a run up to its first closer, off the lowering's table: one
  // state per length of closer already spelled.
  template <class Cfg>
  int through(const Cfg& g, const LexNode& e, int out) {
    const auto len = static_cast<unsigned>(e.min);
    std::vector<int> q(len + 1);
    q[len] = out;
    for (unsigned i = 0; i < len; ++i) q[i] = hole();
    const int end =
        e.max != 0 ? guard(g.lexChild[e.b + len * (len + 1)], out) : -1;
    for (unsigned i = 0; i < len; ++i) {
      int chain = end;
      for (unsigned d = 0; d <= len; ++d) {
        const std::uint16_t set = g.lexChild[e.b + i * (len + 1) + d];
        if (set == kNoSet) continue;
        const int step = chars(static_cast<int>(set), q[d]);
        chain = chain < 0 ? step : split(step, chain);
      }
      s[static_cast<std::size_t>(q[i])].t1 = chain;
    }
    return q[0];
  }

 private:
  int sink(const LexNfaState& want) {
    const auto [at, fresh] =
        _sinks.try_emplace({want.accept, want.bound, want.plainBound,
                            want.loneLeadBound, want.lit},
                           static_cast<int>(s.size()));
    return fresh ? push(want) : at->second;
  }

  // The sinks created so far, by what they accept.
  std::map<std::tuple<int, int, int, bool, bool>, int> _sinks;
  // The holes of the guards created so far, by the word refused and the
  // continuation it narrows.
  std::map<std::pair<std::uint16_t, int>, int> _guardOf;
  // The states the closure being walked has reached.
  mutable RoundMarks _seen;
  mutable std::vector<int> _todo;
};

}  // namespace cursiv::engine
