// Whether some text one terminal's graph matches is the start of a text
// another's matches, or the whole of it: a walk over pairs of NFA subsets,
// each pair once. The walk says how sure it is: it may stop where a text runs
// on through a relay it does not follow.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

// The bytes the edges of a subset read.
inline ByteSet lex_subset_reads(const LexNfaPool& n, const ByteSet* sets,
                                const std::vector<int>& sub) {
  ByteSet out;
  for (const int x : sub) {
    const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
    if (st.set < 0) continue;
    ByteSet on = sets[st.set];
    if (st.minus >= 0) on.andnot(sets[st.minus]);
    out |= on;
  }
  return out;
}

// The closure of where the edges of `from` that `takes` lead, sorted.
template <class Takes>
std::vector<int> lex_nfa_leads(const LexNfaPool& n,
                               const std::vector<int>& from, Takes&& takes) {
  std::vector<int> work;
  for (const int x : from) {
    const LexNfaState& s = n.s[static_cast<std::size_t>(x)];
    if (s.set >= 0 && takes(s)) work.push_back(s.t1);
  }
  std::vector<int> to = n.eclose(work);
  std::sort(to.begin(), to.end());
  return to;
}

inline std::vector<int> lex_nfa_step(const LexNfaPool& n, const ByteSet* sets,
                                     const std::vector<int>& from,
                                     unsigned char c) {
  return lex_nfa_leads(n, from, [&](const LexNfaState& s) {
    return sets[s.set].has(c) && (s.minus < 0 || !sets[s.minus].has(c));
  });
}

// The union of `lex_nfa_step` over every byte of `want`.
inline std::vector<int> lex_nfa_step_any(const LexNfaPool& n,
                                         const ByteSet* sets,
                                         const std::vector<int>& from,
                                         const ByteSet& want) {
  return lex_nfa_leads(n, from, [&](const LexNfaState& s) {
    ByteSet on = sets[s.set];
    if (s.minus >= 0) on.andnot(sets[s.minus]);
    return on.intersects(want);
  });
}

// The subsets the sorted subset `sub` may stand for at one position: the
// runtime resumes the walk at an assertion's continuation, at that position,
// only where the assertion holds, and the walk does not read what it tests.
// So each assertion is entered or not, a continuation's own included; `sub`
// comes first, and each subset once, sorted. Past `kMost` subsets, `sub`
// stands alone and `unsure` is set: they double with each assertion.
inline std::vector<std::vector<int>> lex_assertion_worlds(const LexNfaPool& n,
                                                          std::vector<int> sub,
                                                          bool& unsure) {
  constexpr std::size_t kMost = 64;
  std::vector<std::vector<int>> out;
  out.push_back(std::move(sub));
  std::set<std::vector<int>> seen;
  for (std::size_t w = 0; w < out.size(); ++w)
    for (std::size_t i = 0; i < out[w].size(); ++i) {
      const int r = n.s[static_cast<std::size_t>(out[w][i])].relay;
      if (r < 0) continue;
      const LexRelayBuild& relay = n.relays[static_cast<std::size_t>(r)];
      if (relay.row.predicate == kNoLexNode) continue;
      std::vector<int> held = out[w];
      const std::vector<int> rest = n.eclose(relay.resumeOut);
      held.insert(held.end(), rest.begin(), rest.end());
      std::sort(held.begin(), held.end());
      held.erase(std::unique(held.begin(), held.end()), held.end());
      if (held.size() == out[w].size()) continue;
      if (seen.empty()) seen.insert(out.front());
      if (!seen.insert(held).second) continue;
      if (out.size() == kMost) {
        unsure = true;
        out.resize(1);
        return out;
      }
      out.push_back(std::move(held));
    }
  return out;
}

// The longest start of `lit` that its first `k` bytes and `c` end on.
inline std::size_t lex_lit_follow(std::string_view lit, std::size_t k,
                                  unsigned char c) {
  for (std::size_t m = std::min(k + 1, lit.size()); m > 0; --m)
    if (static_cast<unsigned char>(lit[m - 1]) == c &&
        lit.substr(0, m - 1) == lit.substr(k + 1 - m, m - 1))
      return m;
  return 0;
}

// Where a walk stands: a sorted subset, and the balanced runs of nested relays
// it reads, each as its relay and the length of the opener and of the closer
// its bytes end on, sorted. A run is followed while no opener deepens it: the
// texts it reads are some of those its relay reads, never others.
using LexWalkSide = std::pair<std::vector<int>, std::vector<std::uint32_t>>;

// A run follows a nested relay whose opener and closer are one literal each.
inline bool lex_runs_followed(const LexRelayBuild& r) {
  return !r.close.empty() && r.open.size() < 256 && r.close.size() < 256;
}

// The sides `side` may stand for at one position: its assertion worlds, each
// with a run started at each nested relay it holds. `unsure` is set where a
// relay reads what the walk does not follow: an echo, a recognizer, a nested
// relay of several literals or of none, and a run whose search starts behind
// its relay or may end with the text, which the walk reads as one that does
// neither.
inline std::vector<LexWalkSide> lex_side_worlds(const LexNfaPool& n,
                                                LexWalkSide side,
                                                bool& unsure) {
  std::vector<LexWalkSide> out;
  for (std::vector<int>& at :
       lex_assertion_worlds(n, std::move(side.first), unsure)) {
    std::vector<std::uint32_t> runs = side.second;
    for (const int x : at) {
      const int r = n.s[static_cast<std::size_t>(x)].relay;
      if (r < 0) continue;
      const LexRelayBuild& relay = n.relays[static_cast<std::size_t>(r)];
      if (r <= 0xffff && lex_runs_followed(relay)) {
        runs.push_back(static_cast<std::uint32_t>(r) << 16);
        unsure |= relay.row.back != 0 || relay.row.eof;
      } else if (relay.row.predicate == kNoLexNode) {
        unsure = true;
      }
    }
    std::sort(runs.begin(), runs.end());
    runs.erase(std::unique(runs.begin(), runs.end()), runs.end());
    out.emplace_back(std::move(at), std::move(runs));
  }
  return out;
}

// The bytes a side reads: any byte, where a run is read.
inline ByteSet lex_side_reads(const LexNfaPool& n, const ByteSet* sets,
                              const LexWalkSide& side) {
  return side.second.empty() ? lex_subset_reads(n, sets, side.first)
                             : lex_not(ByteSet{});
}

// The side past one more byte. A relay's search takes the delimiter that
// starts first, the closer where both start at one byte: a closer ends its
// run unless an opener may start before it, and a run an opener deepens is
// followed no further. `dropped` says some run was left there, its text read
// on by the relay and not by the walk.
inline LexWalkSide lex_side_step(const LexNfaPool& n, const ByteSet* sets,
                                 const LexWalkSide& from, unsigned char c,
                                 bool& dropped) {
  LexWalkSide to{lex_nfa_step(n, sets, from.first, c), {}};
  std::vector<int> resumed;
  for (const std::uint32_t run : from.second) {
    const LexRelayBuild& r = n.relays[run >> 16];
    const std::size_t open = lex_lit_follow(r.open, (run >> 8) & 0xffu, c);
    const std::size_t close = lex_lit_follow(r.close, run & 0xffu, c);
    if (close == r.close.size() && open <= r.close.size()) {
      resumed.push_back(r.resumeOut);
    } else if (close < r.close.size() && open < r.open.size()) {
      to.second.push_back((run & 0xffff0000u) |
                          static_cast<std::uint32_t>((open << 8) | close));
    } else {
      dropped = true;
    }
  }
  if (!resumed.empty()) {
    const std::vector<int> rest = n.eclose(resumed);
    to.first.insert(to.first.end(), rest.begin(), rest.end());
    std::sort(to.first.begin(), to.first.end());
    to.first.erase(std::unique(to.first.begin(), to.first.end()),
                   to.first.end());
  }
  std::sort(to.second.begin(), to.second.end());
  to.second.erase(std::unique(to.second.begin(), to.second.end()),
                  to.second.end());
  return to;
}

// What a pair walk finds: a pair `done` takes, none on a walk of every pair,
// or neither for sure: `done` took a pair only where some assertion was taken
// to hold, or some text ran on where the walk does not follow -- through a
// relay, or past more assertions than it enters.
enum class LexOverlap : std::uint8_t { none, found, incomplete };

// Whether some text leads the graphs of `a` and `b` to subsets `done` takes
// together: a walk over pairs, each once, whether or not each assertion on
// the way holds, and through the runs of nested relays it may follow. A pair
// reached past an assertion entered proves nothing: the walk does not read
// whether it holds, nor whether the others assumed with it hold too.
template <class Done>
LexOverlap lex_pair_search(const LexNfaPool& n, const ByteSet* sets, int a,
                           int b, Done&& done) {
  if (a < 0 || b < 0) return LexOverlap::none;
  // The two sides, and whether an assertion was entered on the way.
  using Pair = std::tuple<LexWalkSide, LexWalkSide, bool>;
  std::vector<Pair> work;
  std::set<Pair> seen;
  bool unsure = false;
  const auto enter = [&](LexWalkSide x, LexWalkSide y, bool assumed) {
    const auto xs = lex_side_worlds(n, std::move(x), unsure);
    const auto ys = lex_side_worlds(n, std::move(y), unsure);
    // Each world but the first enters an assertion.
    for (std::size_t i = 0; i < xs.size(); ++i)
      for (std::size_t j = 0; j < ys.size(); ++j)
        if (Pair at{xs[i], ys[j], assumed || i > 0 || j > 0};
            seen.insert(at).second)
          work.push_back(std::move(at));
  };
  std::vector<int> first = n.eclose(a);
  std::vector<int> second = n.eclose(b);
  std::sort(first.begin(), first.end());
  std::sort(second.begin(), second.end());
  enter({std::move(first), {}}, {std::move(second), {}}, false);
  const auto stuck = [](const LexWalkSide& side) {
    return side.first.empty() && side.second.empty();
  };
  for (std::size_t i = 0; i < work.size(); ++i) {
    const auto [p, q, assumed] = work[i];
    // An assumed pair can only leave the walk unsure.
    if (assumed && unsure) continue;
    if (done(p.first, q.first)) {
      if (!assumed) return LexOverlap::found;
      unsure = true;
      continue;
    }
    const ByteSet shared =
        lex_side_reads(n, sets, p) & lex_side_reads(n, sets, q);
    for (unsigned c = 0; c < 256; ++c) {
      const auto byte = static_cast<unsigned char>(c);
      if (!shared.has(byte)) continue;
      bool droppedX = false, droppedY = false;
      LexWalkSide x = lex_side_step(n, sets, p, byte, droppedX);
      LexWalkSide y = lex_side_step(n, sets, q, byte, droppedY);
      // A run left on one side matters where the other reads on, or left a
      // run too.
      unsure |=
          (droppedX && (droppedY || !stuck(y))) || (droppedY && !stuck(x));
      if (stuck(x) || stuck(y)) continue;
      enter(std::move(x), std::move(y), assumed);
    }
  }
  return unsure ? LexOverlap::incomplete : LexOverlap::none;
}

inline bool lex_accepts(const LexNfaPool& n, const std::vector<int>& sub) {
  return std::ranges::any_of(
      sub, [&](int x) { return n.s[static_cast<std::size_t>(x)].accept >= 0; });
}

// A recognizer may consume any continuation beyond its DFA prefix.
inline bool lex_recognizes(const LexNfaPool& n, const std::vector<int>& sub) {
  return std::ranges::any_of(sub, [&](int x) {
    const int r = n.s[static_cast<std::size_t>(x)].relay;
    return r >= 0 &&
           n.relays[static_cast<std::size_t>(r)].row.recognition != kNoLexNode;
  });
}

// Whether some text `a` matches is the start of one `b` matches, or the whole
// of it. A recognizer on either side, which may read on to any text, leaves
// the walk unsure.
inline LexOverlap lex_prefix_meets(const LexNfaPool& n, const ByteSet* sets,
                                   int a, int b) {
  return lex_pair_search(
      n, sets, a, b, [&](const std::vector<int>& x, const std::vector<int>&) {
        return lex_accepts(n, x);
      });
}

// The subsets a walk from `from` stands in once it has read one of the
// sequences whole, each once; `dies` says some byte of one led nowhere.
inline std::vector<std::vector<int>> lex_scalar_ends(
    const LexNfaPool& n, const ByteSet* sets, std::vector<int> from,
    const std::vector<LexCodePoints::Seq>& seqs, bool& dies) {
  std::sort(from.begin(), from.end());
  std::map<std::vector<int>, int> ids;
  std::vector<std::vector<int>> subs;
  std::map<std::pair<int, unsigned>, int> steps;
  const auto intern = [&](std::vector<int> s) {
    if (s.empty()) return -1;
    const auto [at, fresh] = ids.try_emplace(s, static_cast<int>(subs.size()));
    if (fresh) subs.push_back(std::move(s));
    return at->second;
  };
  dies = false;
  std::vector<int> ends;
  const int root = intern(std::move(from));
  if (root < 0) return {};
  for (const LexCodePoints::Seq& q : seqs) {
    std::vector<int> cur{root};
    for (unsigned k = 0; k < q.len && !cur.empty(); ++k) {
      std::vector<int> next;
      for (const int s : cur)
        for (unsigned b = q.lo[k]; b <= q.hi[k]; ++b) {
          auto [at, fresh] = steps.try_emplace({s, b}, -1);
          if (fresh)
            at->second = intern(lex_nfa_step(
                n, sets, subs[static_cast<std::size_t>(s)],
                static_cast<unsigned char>(b)));
          if (at->second < 0)
            dies = true;
          else if (std::find(next.begin(), next.end(), at->second) ==
                   next.end())
            next.push_back(at->second);
        }
      cur = std::move(next);
    }
    for (const int s : cur)
      if (std::find(ends.begin(), ends.end(), s) == ends.end())
        ends.push_back(s);
  }
  std::vector<std::vector<int>> out;
  for (const int s : ends) out.push_back(subs[static_cast<std::size_t>(s)]);
  return out;
}

}  // namespace cursiv
