// A lead byte stands alone only where the rest of its scalar does not follow.
// The graph states that as a refusal after the lead; here the automaton keeps
// what it reads behind a lead apart until the rest is ruled out, a byte at a
// time: a byte that continues the scalar ends the reading, one that does not
// goes back to the states it would have reached. A match ending short of the
// rest stands unless the rest follows it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

template <class Cfg>
struct LexStrayExpansion {
  LexNfaPool& n;
  Cfg& g;

  LexStrayExpansion(LexNfaPool& pool, Cfg& cfg) : n(pool), g(cfg) {}

  // What follows `q`, once `rest` of a scalar is still to be ruled out and
  // `back` bytes are behind the lead.
  std::map<std::tuple<int, std::vector<std::uint16_t>, unsigned>, int> memo;
  std::uint16_t strayTerm = 0;

  void run() {
    for (std::size_t x = 0; x < n.s.size(); ++x) {
      if (n.s[x].stray < 0) continue;
      const LexNode word = g.lex[static_cast<std::size_t>(n.s[x].stray)];
      strayTerm = n.s[x].strayTerm;
      const std::vector<std::uint16_t> rest(
          g.lexKid.data() + word.b, g.lexKid.data() + word.b + word.min);
      const int to = expand(n.s[x].t1, rest, 1);
      n.s[x].t1 = to;
      n.s[x].stray = -1;
    }
  }

 private:
  std::uint16_t refusing_the_rest(const std::vector<std::uint16_t>& rest,
                                  unsigned back) {
    if (rest.size() == 1) return rest.front();
    LexCodePoints marked;
    marked.ranges.push_back({kBackScalar + back, kBackScalar + back});
    return lex_intern_cps_bound(g, ByteSet{}, marked);
  }

  int expand(int q, const std::vector<std::uint16_t>& rest, unsigned back) {
    struct Pending {
      int q, into;
      std::vector<std::uint16_t> rest;
      unsigned back;
    };
    std::vector<Pending> todo;
    const auto enter = [&](int at, const std::vector<std::uint16_t>& suffix,
                           unsigned behind) {
      if (at < 0) return -1;
      const auto key = std::make_tuple(at, suffix, behind);
      const auto [found, fresh] = memo.try_emplace(key, -1);
      if (fresh) {
        // A zero-width predicate may resume at this very expansion. Give it
        // an epsilon target before following any of the continuation's exits.
        found->second = n.hole();
        todo.push_back({at, found->second, suffix, behind});
      }
      return found->second;
    };
    const int root = enter(q, rest, back);
    while (!todo.empty()) {
      const Pending work = std::move(todo.back());
      todo.pop_back();
      int chain = -1;
      const auto add = [&](int x) {
        chain = chain < 0 ? x : n.split(x, chain);
      };
      const std::vector<std::uint16_t> tail(work.rest.begin() + 1,
                                            work.rest.end());
      for (const int x : n.eclose(work.q)) {
        const LexNfaState st = n.s[static_cast<std::size_t>(x)];
        if (st.set >= 0) {
          ByteSet edge = g.sets[static_cast<std::size_t>(st.set)];
          if (st.minus >= 0)
            edge.andnot(g.sets[static_cast<std::size_t>(st.minus)]);
          const ByteSet next = g.sets[work.rest.front()];
          const ByteSet inside = edge & next;
          ByteSet outside = edge;
          outside.andnot(next);
          if (inside.any() && !tail.empty()) {
            const int to = enter(st.t1, tail, work.back + 1);
            if (to >= 0) add(n.chars(lex_intern_set(g, inside), to));
          }
          if (!outside.any()) continue;
          if (!inside.any()) {
            add(x);
          } else {
            add(n.push({.set = lex_intern_set(g, outside),
                        .t1 = st.t1,
                        .stray = st.stray,
                        .strayTerm = st.strayTerm}));
          }
        } else if (st.accept >= 0) {
          const std::uint16_t refusal = refusing_the_rest(work.rest, work.back);
          add(n.match_strayed(
              st.accept,
              st.bound < 0
                  ? refusal
                  : lex_bound_union(g, static_cast<std::uint16_t>(st.bound),
                                    refusal),
              st.plain(), st.lit));
        } else if (st.relay >= 0) {
          LexRelayBuild assertion =
              n.relays[static_cast<std::size_t>(st.relay)];
          if (assertion.row.predicate == kNoLexNode) {
            lower_refuse(g, LowerFail::refusalIntoRelay, strayTerm);
            continue;
          }
          assertion.resumeOut =
              enter(assertion.resumeOut, work.rest, work.back);
          if (assertion.resumeOut < 0) continue;
          n.relays.push_back(assertion);
          add(n.relay(static_cast<int>(n.relays.size()) - 1));
        }
      }
      n.s[static_cast<std::size_t>(work.into)].t1 = chain;
    }
    return root;
  }
};

}  // namespace cursiv
