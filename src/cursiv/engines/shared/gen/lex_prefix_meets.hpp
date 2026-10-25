// Whether some text one terminal's graph matches is the start of a text
// another's matches, or the whole of it: a walk over pairs of NFA subsets,
// each pair once.
#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
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

// Whether some text leads the graphs of `a` and `b` to subsets `done` takes
// together: a walk over pairs, each once.
template <class Done>
bool lex_pair_search(const LexNfaPool& n, const ByteSet* sets, int a, int b,
                     Done&& done) {
  if (a < 0 || b < 0) return false;
  using Pair = std::pair<std::vector<int>, std::vector<int>>;
  Pair start{n.eclose(a), n.eclose(b)};
  std::sort(start.first.begin(), start.first.end());
  std::sort(start.second.begin(), start.second.end());
  std::vector<Pair> work{start};
  std::set<Pair> seen{std::move(start)};
  for (std::size_t i = 0; i < work.size(); ++i) {
    const Pair at = work[i];
    if (done(at.first, at.second)) return true;
    const ByteSet shared = lex_subset_reads(n, sets, at.first) &
                           lex_subset_reads(n, sets, at.second);
    for (unsigned c = 0; c < 256; ++c) {
      const auto byte = static_cast<unsigned char>(c);
      if (!shared.has(byte)) continue;
      Pair next{lex_nfa_step(n, sets, at.first, byte),
                lex_nfa_step(n, sets, at.second, byte)};
      if (next.first.empty() || next.second.empty()) continue;
      if (seen.insert(next).second) work.push_back(std::move(next));
    }
  }
  return false;
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

inline bool lex_prefix_meets(const LexNfaPool& n, const ByteSet* sets, int a,
                             int b) {
  return lex_pair_search(
      n, sets, a, b, [&](const std::vector<int>& x, const std::vector<int>& y) {
        return lex_accepts(n, x) || lex_recognizes(n, x) ||
               lex_recognizes(n, y);
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
