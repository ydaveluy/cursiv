// Where a keyword's spelling stands beside trivia, the trivia goes on only
// behind a relay asserting that no such keyword stands there. The assertion
// asks the keyword's own boundary and conditions.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexCodePoints.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LexKeywordCut {
  // A keyword: the set each byte of its spelling reads in, the class its
  // acceptance may not run into, and the nodes it asserts behind it.
  struct Keyword {
    std::vector<std::uint16_t> sets;
    std::uint16_t bound = kNoSet;
    std::vector<std::uint16_t> after;
  };
  std::vector<Keyword> keywords;
  // NFA state -> the trivia terminal whose graph it stands in, else 0.
  std::vector<std::uint16_t> trivia;
  // NFA state -> `(keyword + 1) << 16 | depth` for each keyword whose
  // spelling it stands in having read `depth` bytes of it.
  std::vector<std::vector<std::uint32_t>> reads;
  bool active = false;

  bool whole(std::uint32_t tag) const {
    return (tag & 0xffffu) == keywords[(tag >> 16) - 1].sets.size();
  }

  // The branches of a token graph that consume one keyword, conditions
  // aside: whether there are any, whether the graph has no other, the sets
  // their bytes read in, and the longest.
  struct Branches {
    bool any = false;
    bool all = true;
    std::vector<std::uint16_t> sets;
    std::size_t longest = 0;
  };

  template <class Cfg>
  static Branches branches(const Cfg& g, std::uint16_t node) {
    const ByteSet leads =
        lex_leads_of(g.wordRanges.data(), g.wordRanges.size());
    Branches out;
    out.all = collect(g, node, leads, out);
    std::ranges::sort(out.sets);
    out.sets.erase(std::ranges::unique(out.sets).begin(), out.sets.end());
    return out;
  }

  template <class Cfg>
  static LexKeywordCut of(const Cfg& g, const LexNfaPool& nfa,
                          const std::vector<int>& startOf) {
    LexKeywordCut cut;
    const std::size_t n = nfa.s.size();
    const ByteSet leads =
        lex_leads_of(g.wordRanges.data(), g.wordRanges.size());
    cut.reads.assign(n, {});
    std::vector<std::uint32_t> into;
    for (std::size_t t = 1; t < startOf.size(); ++t) {
      if (g.terms[t].trivia || startOf[t] < 0) continue;
      const Branches own = branches(g, g.termBuild[t].lex);
      if (!own.any) continue;
      if (into.empty()) into = entering(nfa);
      std::vector<Spelling> found;
      cut.spell(g, nfa, static_cast<int>(t), startOf[t], Spelling{}, leads, own,
                found);
      cut.settle(found, into);
    }
    if (cut.keywords.empty()) return cut;
    cut.trivia.assign(n, 0);
    for (std::size_t t = 1; t < startOf.size(); ++t) {
      if (!g.terms[t].trivia || startOf[t] < 0) continue;
      std::vector<int> todo{startOf[t]};
      while (!todo.empty()) {
        const int x = todo.back();
        todo.pop_back();
        if (x < 0 || cut.trivia[static_cast<std::size_t>(x)] != 0) continue;
        cut.trivia[static_cast<std::size_t>(x)] = static_cast<std::uint16_t>(t);
        cut.active = true;
        todo.push_back(nfa.s[static_cast<std::size_t>(x)].t1);
        todo.push_back(nfa.s[static_cast<std::size_t>(x)].t2);
      }
    }
    return cut;
  }

  // The node asserting that none of the keywords `stands` names, as `reads`
  // tags a state of each, stands at the position: the rest of its spelling,
  // and no scalar of its word class behind it.
  template <class Cfg>
  std::uint16_t gate(Cfg& g, const std::vector<std::uint32_t>& stands) {
    if (const auto at = _gates.find(stands); at != _gates.end())
      return at->second;
    const auto node = [&](LexKind kind, std::uint16_t a, std::uint16_t b = 0) {
      return g.push_lex(LexNode{kind, a, b});
    };
    const auto of_all = [&](LexKind kind,
                            const std::vector<std::uint16_t>& list) {
      if (list.size() == 1) return list[0];
      if (list.size() >= static_cast<std::size_t>(kNoLexNode - g.nlexKid))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const std::uint16_t base = g.nlexKid;
      for (const std::uint16_t kid : list) g.push_lex_kid(kid);
      return node(kind, base, static_cast<std::uint16_t>(list.size()));
    };
    std::vector<std::uint16_t> alts;
    for (const std::uint32_t tag : stands) {
      const Keyword& word = keywords[(tag >> 16) - 1];
      const std::uint16_t follows =
          word.bound != kNoSet ? node(LexKind::chars, word.bound) : kNoLexNode;
      std::vector<std::uint16_t> parts;
      for (std::size_t d = tag & 0xffffu; d < word.sets.size(); ++d)
        parts.push_back(node(LexKind::chars, word.sets[d]));
      if (follows != kNoLexNode) {
        if (stands.size() == 1 && parts.empty() && word.after.empty())
          return _gates.emplace(stands, node(LexKind::positive, follows))
              .first->second;
        parts.push_back(node(LexKind::negative, follows));
      }
      parts.insert(parts.end(), word.after.begin(), word.after.end());
      alts.push_back(of_all(LexKind::seq, parts));
    }
    return _gates
        .emplace(stands, node(LexKind::negative, of_all(LexKind::alt, alts)))
        .first->second;
  }

 private:
  std::map<std::vector<std::uint32_t>, std::uint16_t> _gates;

  struct Spelling {
    Keyword word;
    std::vector<int> read;
    // Its steps, the first past its last byte, and the count at its first
    // relay.
    std::vector<std::pair<int, int>> edges;
    std::size_t tail = 0;
    std::size_t asserted = SIZE_MAX;
    int end = -1;
    std::size_t steps = 0;
  };

  // How many states lead into each state, by an exit or a relay's resumption.
  static std::vector<std::uint32_t> entering(const LexNfaPool& nfa) {
    std::vector<std::uint32_t> into(nfa.s.size(), 0);
    for (const LexNfaState& st : nfa.s) {
      if (st.t1 >= 0) ++into[static_cast<std::size_t>(st.t1)];
      if (st.t2 >= 0 && st.t2 != st.t1) ++into[static_cast<std::size_t>(st.t2)];
      if (st.relay >= 0) {
        const int resume =
            nfa.relays[static_cast<std::size_t>(st.relay)].resumeOut;
        if (resume >= 0) ++into[static_cast<std::size_t>(resume)];
      }
    }
    return into;
  }

  // The spellings of terminal `t` from NFA state `x`, one per alternative. A
  // spelling reads only the sets of `own`, ends where `t` accepts, and counts
  // only if its boundary is the word class. An assertion before its last byte
  // is not behind it, and ends the spelling.
  template <class Cfg>
  void spell(const Cfg& g, const LexNfaPool& nfa, int t, int x, Spelling walk,
             const ByteSet& leads, const Branches& own,
             std::vector<Spelling>& found) {
    for (; x >= 0 && walk.steps <= nfa.s.size(); ++walk.steps) {
      const LexNfaState& st = nfa.s[static_cast<std::size_t>(x)];
      if (st.accept >= 0) {
        if (st.accept == t && !walk.read.empty() && walk.asserted > walk.tail &&
            bounded(g, st, walk, leads))
          found.push_back(std::move(walk));
        return;
      }
      if (st.minus >= 0) return;
      int next = -1;
      if (st.relay >= 0) {
        const LexRelayBuild& r = nfa.relays[static_cast<std::size_t>(st.relay)];
        if (walk.read.empty() || r.row.predicate == kNoLexNode ||
            r.row.recognition != kNoLexNode)
          return;
        walk.word.after.push_back(r.row.predicate);
        walk.asserted = std::min(walk.asserted, walk.edges.size());
        next = r.resumeOut;
      } else if (st.set >= 0) {
        if (walk.read.size() >= own.longest ||
            !std::ranges::binary_search(own.sets,
                                        static_cast<std::uint16_t>(st.set)))
          return;
        walk.word.sets.push_back(static_cast<std::uint16_t>(st.set));
        walk.read.push_back(x);
        next = walk.end = st.t1;
        walk.tail = walk.edges.size();
      } else {
        if (st.t2 >= 0) {
          Spelling other = walk;
          other.edges.emplace_back(x, st.t2);
          spell(g, nfa, t, st.t2, std::move(other), leads, own, found);
        }
        next = st.t1;
      }
      if (next >= 0) walk.edges.emplace_back(x, next);
      x = next;
    }
  }

  // Whether `walk` ends on the word class: the bound of `accept`, or a
  // refusal behind the spelling.
  template <class Cfg>
  static bool bounded(const Cfg& g, const LexNfaState& accept, Spelling& walk,
                      const ByteSet& leads) {
    if (accept.bound >= 0 &&
        lex_word_set(g, static_cast<std::uint16_t>(accept.bound), leads))
      walk.word.bound = static_cast<std::uint16_t>(accept.bound);
    return walk.word.bound != kNoSet ||
           std::ranges::any_of(walk.word.after, [&](std::uint16_t node) {
             return word_refusal(g, node, leads);
           });
  }

  // A state another branch of the terminal leads to as well is not the
  // keyword's, nor is anything past it: only what the spellings alone reach
  // is cut.
  void settle(const std::vector<Spelling>& found,
              const std::vector<std::uint32_t>& into) {
    std::map<int, std::set<int>> from;
    for (const Spelling& w : found)
      for (const auto& [a, b] : w.edges) from[b].insert(a);
    const auto alone = [&](int x) {
      return from[x].size() == into[static_cast<std::size_t>(x)];
    };
    for (const Spelling& w : found) {
      if (w.tail >= w.edges.size() || !alone(w.edges[w.tail].second)) continue;
      const auto tag = static_cast<std::uint32_t>(keywords.size() + 1) << 16;
      for (std::size_t d = 0; d < w.read.size(); ++d)
        reads[static_cast<std::size_t>(w.read[d])].push_back(
            tag | static_cast<std::uint32_t>(d));
      reads[static_cast<std::size_t>(w.end)].push_back(
          tag | static_cast<std::uint32_t>(w.read.size()));
      keywords.push_back(w.word);
    }
  }

  template <class Cfg>
  static bool word_refusal(const Cfg& g, std::uint16_t node,
                           const ByteSet& leads) {
    const LexNode& e = g.lex[node];
    return e.kind == LexKind::nguard && e.a == kNoSet && e.min == 1 &&
           lex_word_set(g, g.lexKid[e.b], leads);
  }

  template <class Cfg>
  static void leaves(const Cfg& g, std::uint16_t node,
                     std::vector<std::uint16_t>& out) {
    const LexNode& e = g.lex[node];
    if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i) leaves(g, g.lexKid[e.a + i], out);
    } else if (e.kind != LexKind::positive && e.kind != LexKind::negative) {
      out.push_back(node);
    }
  }

  // Whether `node` consumes keywords alone; `out` hears those branches.
  template <class Cfg>
  static bool collect(const Cfg& g, std::uint16_t node, const ByteSet& leads,
                      Branches& out) {
    std::vector<std::uint16_t> read;
    leaves(g, node, read);
    std::size_t core = read.size();
    bool word = false;
    while (core != 0 && g.lex[read[core - 1]].kind == LexKind::nguard)
      if (word_refusal(g, read[--core], leads)) word = true;
    if (core == 1 && g.lex[read[0]].kind == LexKind::alt) {
      const LexNode& e = g.lex[read[0]];
      bool all = e.b != 0;
      for (unsigned i = 0; i < e.b; ++i)
        all = collect(g, g.lexKid[e.a + i], leads, out) && all;
      return all;
    }
    if (core == 0 || !word) return false;
    for (std::size_t i = 0; i < core; ++i)
      if (g.lex[read[i]].kind != LexKind::chars) return false;
    for (std::size_t i = 0; i < core; ++i) out.sets.push_back(g.lex[read[i]].a);
    out.any = true;
    out.longest = std::max(out.longest, core);
    return true;
  }
};

}  // namespace cursiv
