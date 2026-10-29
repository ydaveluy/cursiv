// The NFA one terminal's spelling is built into, before the subsets make a
// DFA of it. A refusal is recorded as a hole and narrowed later, so a state's
// exits are not all known when it is minted.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

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
  // accept stood behind without it.
  int plainBound = -1;
  bool strayBound = false;
  // The token is handed to this relay here, or -1.
  int relay = -1;
  // An accept that a literal's branch reads.
  bool lit = false;
  // The edge consumes a lead byte that starts no scalar where the rest of one
  // follows: the refusal node naming that rest, else -1. The automaton keeps
  // what follows it in a state of its own until the rest is ruled out.
  int stray = -1;
  std::uint16_t strayTerm = 0;
  // The guard this hole stands for, or -1.
  int guard = -1;

  int plain() const { return strayBound ? plainBound : bound; }
};

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
  std::vector<LexNfaState> s;
  std::vector<LexNfaGuard> guards;
  std::vector<LexRelayBuild> relays;
  std::uint16_t term = 0;  // the terminal being built, for its refusals

  int push(const LexNfaState& st) {
    if (s.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw LowerRefused(LowerFail::tokenGraphTooLarge);
    s.push_back(st);
    return static_cast<int>(s.size()) - 1;
  }
  int chars(int set, int next) { return push({.set = set, .t1 = next}); }
  int chars_stray(int set, int word, int next) {
    return push({.set = set, .t1 = next, .stray = word, .strayTerm = term});
  }
  int split(int a, int b) { return push({.t1 = a, .t2 = b}); }
  int match(int term, bool lit = false) {
    return push({.accept = term, .lit = lit});
  }
  int relay(int k) { return push({.relay = k}); }
  // One sink per terminal and class: two literals of one terminal ending on
  // the same boundary land in one DFA state, as they do on a plain match.
  int match_bounded(int term, int bound, bool lit = false) {
    return sink({.accept = term, .bound = bound, .lit = lit});
  }
  int match_strayed(int term, int bound, int plain, bool lit = false) {
    return sink({.accept = term,
                 .bound = bound,
                 .plainBound = plain,
                 .strayBound = true,
                 .lit = lit});
  }
  // An epsilon state whose target is filled in later: a run's walk states are
  // mutually reachable, so no build order can hand them all a known exit.
  int hole() { return push({}); }

  // A guard waits until loops have their exits.
  int guard(std::uint16_t node, int out) {
    for (const LexNfaGuard& gd : guards)
      if (gd.node == node && gd.out == out) return gd.hole;
    const int h = push({.guard = static_cast<int>(guards.size())});
    guards.push_back({h, out, node, term});
    return h;
  }

  // Thompson repetition: `body(out)` builds one occurrence backwards.
  template <class Body>
  int repeat(const LexNode& e, int out, Body&& body) {
    int tail = out;
    if (e.max == 0xffffffffu) {
      tail = split(-1, out);
      const int head = body(tail);
      s[static_cast<std::size_t>(tail)].t1 = head;
    } else {
      for (std::uint32_t i = e.max; i > e.min; --i)
        tail = split(body(tail), out);
    }
    for (std::uint32_t i = 0; i < e.min; ++i) tail = body(tail);
    return tail;
  }

  // What `roots` reach without consuming, in depth-first preorder, once each.
  std::vector<int> eclose(std::span<const int> roots) const {
    if (_seen.size() < s.size()) _seen.resize(s.size(), 0);
    if (++_pass == 0) {
      std::fill(_seen.begin(), _seen.end(), 0u);
      _pass = 1;
    }
    std::vector<int> set;
    for (const int root : roots) {
      if (root < 0) continue;
      _todo.assign(1, root);
      while (!_todo.empty()) {
        const int at = _todo.back();
        _todo.pop_back();
        if (_seen[static_cast<std::size_t>(at)] == _pass) continue;
        _seen[static_cast<std::size_t>(at)] = _pass;
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

  // The walk of a run, read off the table the lowering computed: one state per
  // length of closer already spelled. The content is exactly what has not
  // completed the closer, so the run stops at the first one -- longest and
  // shortest match then answer the same. Where the end of the text closes it
  // too, every state short of the closer ends it there as well.
  template <class Cfg>
  int through(const Cfg& g, const LexNode& e, int out) {
    const unsigned len = static_cast<unsigned>(e.min);
    std::vector<int> q(len + 1);
    q[len] = out;
    for (unsigned i = 0; i < len; ++i) q[i] = hole();
    const int end =
        e.max != 0 ? guard(g.lexKid[e.b + len * (len + 1)], out) : -1;
    for (unsigned i = 0; i < len; ++i) {
      int chain = end;
      for (unsigned d = 0; d <= len; ++d) {
        const std::uint16_t set = g.lexKid[e.b + i * (len + 1) + d];
        if (set == 0xffff) continue;
        const int step = chars(static_cast<int>(set), q[d]);
        chain = chain < 0 ? step : split(step, chain);
      }
      s[static_cast<std::size_t>(q[i])].t1 = chain;
    }
    return q[0];
  }

 private:
  int sink(const LexNfaState& want) {
    const auto [at, fresh] = _sinks.try_emplace(
        {want.accept, want.bound, want.plainBound, want.strayBound, want.lit},
        static_cast<int>(s.size()));
    return fresh ? push(want) : at->second;
  }

  // The sinks minted so far, by what they accept.
  std::map<std::tuple<int, int, int, bool, bool>, int> _sinks;
  // One mark per state, stamped by the pass that reached it.
  mutable std::vector<std::uint32_t> _seen;
  mutable std::vector<int> _todo;
  mutable std::uint32_t _pass = 0;
};

}  // namespace cursiv
