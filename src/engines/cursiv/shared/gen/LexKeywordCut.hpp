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

#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

struct LexKeywordCut {
  // A keyword: the set each byte of its spelling reads in, the class its
  // acceptance may not run into, and the nodes it asserts behind it.
  struct Keyword {
    std::vector<std::uint16_t> sets;
    std::uint16_t bound = kNoSet;
    std::vector<std::uint16_t> after;
    // The terminal it spells: a letter read through a choice of its cases
    // makes one spelling per case, which agree past the letter.
    std::uint16_t term = 0;
  };
  std::vector<Keyword> keywords;
  // NFA state -> the trivia terminal whose graph it stands in, else 0.
  std::vector<std::uint16_t> trivia;
  // NFA state -> `(keyword + 1) << 16 | depth` for each keyword whose
  // spelling it stands in having read `depth` bytes of it.
  std::vector<std::vector<std::uint32_t>> reads;
  bool active = false;
  // The terminals with more keyword spellings than are cut: no trivia is cut
  // beside them, so the lexer refuses trivia that reads on past one.
  std::vector<std::uint16_t> uncut;

  bool whole(std::uint32_t tag) const {
    return (tag & 0xffffu) == keywords[(tag >> 16) - 1].sets.size();
  }

  // The branches of a token graph that consume one keyword, conditions
  // aside: whether there are any, and whether the graph has no other.
  struct Shape {
    bool any = false;
    bool all = true;
  };

  template <class Cfg>
  static Shape shape(const Cfg& g, std::uint16_t node) {
    Shape out;
    out.all = collect(g, node, out, nullptr);
    return out;
  }

  // The same, with the sets their bytes read in, and the longest.
  struct Branches : Shape {
    std::vector<std::uint16_t> sets;
    std::size_t longest = 0;
  };

  template <class Cfg>
  static Branches branches(const Cfg& g, std::uint16_t node) {
    Branches out;
    out.all = collect(g, node, out, &out);
    std::ranges::sort(out.sets);
    out.sets.erase(std::ranges::unique(out.sets).begin(), out.sets.end());
    return out;
  }

  template <class Cfg>
  static LexKeywordCut of(const Cfg& g, const LexNfaPool& nfa,
                          const std::vector<int>& startOf) {
    LexKeywordCut cut;
    const std::size_t n = nfa.s.size();
    cut.reads.assign(n, {});
    std::vector<std::uint32_t> into;
    for (std::size_t t = 1; t < startOf.size(); ++t) {
      if (g.terms[t].trivia || startOf[t] < 0) continue;
      const Branches own = branches(g, g.termBuild[t].lex);
      if (!own.any) continue;
      if (into.empty()) into = entering(nfa);
      // Each word is a spelling, and each choice of cases multiplies them:
      // past a bound, the terminal goes uncut.
      std::vector<Spelling> found;
      std::map<int, std::set<int>> from;
      if (!spell(g, nfa, static_cast<int>(t), startOf[t], own, found, from))
        cut.uncut.push_back(static_cast<std::uint16_t>(t));
      cut.cut_spellings(found, from, into);
    }
    if (cut.keywords.empty()) return cut;
    cut.trivia.assign(n, 0);
    for (std::size_t t = 1; t < startOf.size(); ++t) {
      if (!g.terms[t].trivia || startOf[t] < 0) continue;
      std::vector todo{startOf[t]};
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
      if (list.size() >= static_cast<std::size_t>(kNoLexNode - g.nlexChild))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const std::uint16_t base = g.nlexChild;
      for (const std::uint16_t child : list) g.push_lex_child(child);
      return node(kind, base, static_cast<std::uint16_t>(list.size()));
    };
    std::vector<std::uint16_t> alts;
    // Spellings that differ only in letters already read share one rest,
    // asserted once: its length, sets, bound and assertions.
    std::set<std::pair<std::uint16_t, std::vector<std::uint16_t>>> rests;
    for (const std::uint32_t tag : stands) {
      const Keyword& word = keywords[(tag >> 16) - 1];
      std::vector<std::uint16_t> rest{
          static_cast<std::uint16_t>(word.sets.size() - (tag & 0xffffu))};
      rest.insert(
          rest.end(),
          word.sets.begin() + static_cast<std::ptrdiff_t>(tag & 0xffffu),
          word.sets.end());
      rest.push_back(word.bound);
      rest.insert(rest.end(), word.after.begin(), word.after.end());
      if (!rests.emplace(word.term, std::move(rest)).second) continue;
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
  // The most spellings a keyword is cut for.
  static constexpr std::size_t kSpellings = 1024;

  std::map<std::vector<std::uint32_t>, std::uint16_t> _gates;

  // A spelling, read or being read from `at`. Its edges live in a stack, each
  // naming the one before it: walks that part at a choice share the edges
  // before it.
  struct Spelling {
    int at = -1;
    Keyword word;
    std::vector<int> read;
    std::uint32_t last = kNoEdge;
    // The state past its last byte.
    int end = -1;
    std::size_t steps = 0;
  };
  struct Edge {
    int from = -1, to = -1;
    std::uint32_t before = kNoEdge;
  };
  static constexpr std::uint32_t kNoEdge = 0xffffffffu;

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

  // The spellings of terminal `t` from `start` into `found`, and their edges
  // into `from` by target; false, with none, past `kSpellings`. A spelling
  // reads `own`'s sets, ends on the word class, asserts only after its end.
  template <class Cfg>
  static bool spell(const Cfg& g, const LexNfaPool& nfa, int t, int start,
                    const Branches& own, std::vector<Spelling>& found,
                    std::map<int, std::set<int>>& from) {
    // A walk taken from `todo` finds above its last edge only the edges of
    // walks that are done.
    std::vector<Edge> pool;
    const auto step = [&pool](Spelling& w, int at, int to) {
      pool.push_back({at, to, w.last});
      w.last = static_cast<std::uint32_t>(pool.size() - 1);
    };
    std::vector<Spelling> todo(1);
    todo.back().at = start;
    while (!todo.empty() && found.size() <= kSpellings) {
      Spelling walk = std::move(todo.back());
      todo.pop_back();
      pool.resize(walk.last == kNoEdge ? 0 : walk.last + std::size_t{1});
      for (int x = walk.at;
           x >= 0 && walk.steps <= nfa.s.size() && found.size() <= kSpellings;
           ++walk.steps) {
        const LexNfaState& st = nfa.s[static_cast<std::size_t>(x)];
        if (st.accept >= 0) {
          if (st.accept == t && !walk.read.empty() &&
              bounded(g, st, walk.word)) {
            walk.word.term = static_cast<std::uint16_t>(t);
            for (std::uint32_t e = walk.last; e != kNoEdge; e = pool[e].before)
              from[pool[e].to].insert(pool[e].from);
            found.push_back(std::move(walk));
          }
          break;
        }
        if (st.minus >= 0) break;
        int next = -1;
        if (st.relay >= 0) {
          const LexRelayBuild& r =
              nfa.relays[static_cast<std::size_t>(st.relay)];
          if (walk.read.empty() || r.row.predicate == kNoLexNode ||
              r.row.recognition != kNoLexNode)
            break;
          walk.word.after.push_back(r.row.predicate);
          next = r.resumeOut;
        } else if (st.set >= 0) {
          if (!walk.word.after.empty() || walk.read.size() >= own.longest ||
              !std::ranges::binary_search(own.sets,
                                          static_cast<std::uint16_t>(st.set)))
            break;
          walk.word.sets.push_back(static_cast<std::uint16_t>(st.set));
          walk.read.push_back(x);
          next = st.t1;
          walk.end = st.t1;
        } else if (st.t2 >= 0) {
          // This walk goes on past `t1` once the walk past `t2` is done.
          Spelling other = walk;
          if (st.t1 >= 0) step(walk, x, st.t1);
          step(other, x, st.t2);
          other.at = st.t2;
          walk.at = st.t1;
          ++walk.steps;
          todo.push_back(std::move(walk));
          todo.push_back(std::move(other));
          break;
        } else {
          next = st.t1;
        }
        if (next >= 0) step(walk, x, next);
        x = next;
      }
    }
    if (found.size() <= kSpellings) return true;
    found.clear();
    from.clear();
    return false;
  }

  // Whether `word` ends on the word class: the bound of `accept`, or a
  // refusal behind the spelling.
  template <class Cfg>
  static bool bounded(const Cfg& g, const LexNfaState& accept, Keyword& word) {
    if (accept.bound >= 0 &&
        lex_word_set(g, static_cast<std::uint16_t>(accept.bound)))
      word.bound = static_cast<std::uint16_t>(accept.bound);
    return word.bound != kNoSet ||
           std::ranges::any_of(word.after, [&](std::uint16_t node) {
             return word_refusal(g, node);
           });
  }

  // A state another branch of the terminal leads to as well is not the
  // keyword's, nor is anything past it: only what the spellings alone reach
  // is cut.
  void cut_spellings(const std::vector<Spelling>& found,
                     std::map<int, std::set<int>>& from,
                     const std::vector<std::uint32_t>& into) {
    const auto alone = [&](int x) {
      return from[x].size() == into[static_cast<std::size_t>(x)];
    };
    for (const Spelling& w : found) {
      if (!alone(w.end)) continue;
      if (keywords.size() + 1 >= 0x10000u)
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
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
  static bool word_refusal(const Cfg& g, std::uint16_t node) {
    const LexNode& e = g.lex[node];
    return e.kind == LexKind::nguard && e.a == kNoSet && e.min == 1 &&
           lex_word_set(g, g.lexChild[e.b]);
  }

  template <class Cfg>
  static void leaves(const Cfg& g, std::uint16_t node,
                     std::vector<std::uint16_t>& out) {
    for (std::vector todo{node}; !todo.empty();) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      const LexNode& e = g.lex[at];
      if (e.kind == LexKind::seq) {
        for (unsigned i = e.b; i-- > 0;) todo.push_back(g.lexChild[e.a + i]);
      } else if (e.kind != LexKind::positive && e.kind != LexKind::negative) {
        out.push_back(at);
      }
    }
  }

  // Whether `node` consumes keywords alone; `out` gets those branches, and
  // `spelled`, when given, the sets they read in.
  template <class Cfg>
  static bool collect(const Cfg& g, std::uint16_t node, Shape& out,
                      Branches* spelled) {
    bool all = true;
    for (std::vector todo{node}; !todo.empty();) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      std::vector<std::uint16_t> read;
      leaves(g, at, read);
      std::size_t core = read.size();
      bool word = false;
      while (core != 0 && g.lex[read[core - 1]].kind == LexKind::nguard)
        if (word_refusal(g, read[--core])) word = true;
      if (core == 1 && g.lex[read[0]].kind == LexKind::alt &&
          !cases(g, read[0])) {
        const LexNode& e = g.lex[read[0]];
        all = all && e.b != 0;
        for (unsigned i = 0; i < e.b; ++i) todo.push_back(g.lexChild[e.a + i]);
        continue;
      }
      bool keyword = core != 0 && word;
      for (std::size_t i = 0; i < core && keyword; ++i)
        keyword = g.lex[read[i]].kind == LexKind::chars || cases(g, read[i]);
      if (!keyword) {
        all = false;
        continue;
      }
      out.any = true;
      if (spelled == nullptr) continue;
      std::size_t longest = 0;
      for (std::size_t i = 0; i < core; ++i) {
        const LexNode& e = g.lex[read[i]];
        if (e.kind == LexKind::chars) {
          spelled->sets.push_back(e.a);
          ++longest;
          continue;
        }
        // The cases of a letter take as many bytes.
        for (unsigned k = 0; k < e.b; ++k) {
          std::vector<std::uint16_t> bytes;
          leaves(g, g.lexChild[e.a + k], bytes);
          for (const std::uint16_t b : bytes)
            spelled->sets.push_back(g.lex[b].a);
          if (k == 0) longest += bytes.size();
        }
      }
      spelled->longest = std::max(spelled->longest, longest);
    }
    return all;
  }

  // Whether `node` reads one letter of a case-insensitive literal through a
  // choice of its cases, each a run of bytes.
  template <class Cfg>
  static bool cases(const Cfg& g, std::uint16_t node) {
    const LexNode& e = g.lex[node];
    return e.kind == LexKind::alt && e.min == 1;
  }
};

}  // namespace cursiv::engine
