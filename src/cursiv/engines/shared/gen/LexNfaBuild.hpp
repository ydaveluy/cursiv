// Thompson construction and lexical searches shared by both engines.
// The caller owns the text-pool limits.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexKeywordCut.hpp"
#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

inline int lex_push_relay(LexNfaPool& n, LexRelayBuild r, int resumeOut) {
  r.resumeOut = resumeOut;
  n.relays.push_back(r);
  return static_cast<int>(n.relays.size()) - 1;
}

template <class Cfg, class Intern>
struct LexNfaBuild {
  LexNfaPool& n;
  Cfg& g;
  Intern internName;
  std::vector<std::uint8_t> calls = lex_call_ancestors(g);

  // What a branch of a terminal reads, as a mask: the tiers its readings rank
  // at.
  static constexpr unsigned kPattern = 1, kLiteral = 2, kKeyword = 4;

  unsigned tiers_of(std::uint16_t node) const {
    const LexNode& e = g.lex[node];
    if (e.kind == LexKind::alt) {
      auto out = 0u;
      for (unsigned i = 0; i < e.b; ++i) out |= tiers_of(g.lexKid[e.a + i]);
      return out;
    }
    if (const auto own = LexKeywordCut::shape(g, node); own.any && own.all)
      return kKeyword;
    if (e.kind != LexKind::seq) return kPattern;
    if (e.min == 1) return kLiteral;
    auto core = 0u;
    std::uint16_t only = 0;
    for (unsigned i = 0; i < e.b; ++i) {
      const auto kid = g.lexKid[e.a + i];
      if (is_condition(g.lex[kid].kind)) continue;
      ++core;
      only = kid;
    }
    return core == 1 ? tiers_of(only) : kPattern;
  }

  // Whether every class a node can end on breaks `run`, for the fence in
  // front of a mark: the byte before a run must break its class. False where
  // a class it ends on is not known.
  bool ends_apart(std::uint16_t node, const ByteSet& run) const {
    const LexNode& e = g.lex[node];
    switch (e.kind) {
      case LexKind::chars:
        return !g.sets[e.a].intersects(run);
      case LexKind::seq:
        for (unsigned i = e.b; i-- > 0;) {
          const std::uint16_t kid = g.lexKid[e.a + i];
          if (g.lex[kid].kind == LexKind::nguard) continue;
          return ends_apart(kid, run);
        }
        return false;
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i)
          if (!ends_apart(g.lexKid[e.a + i], run)) return false;
        return true;
      default:
        return false;
    }
  }

  struct RelayShape {
    LexRelayBuild relay;
    std::string before, after;
  };

  // Whether the compact relay can recover the capture from the bytes before
  // its fence.
  bool relay_shape(const LexNode& q, unsigned at, std::uint16_t before,
                   std::uint16_t after, std::uint16_t pad, RelayShape& shape) {
    if (q.kind != LexKind::seq || q.b < 3 || at == 0 || at + 1 >= q.b)
      return false;
    const auto& mark = g.lex[g.lexKid[q.a + at]];
    if (mark.kind != LexKind::mark) return false;
    const auto& run = g.lex[mark.a];
    if (run.kind != LexKind::rep || g.lex[run.a].kind != LexKind::chars)
      return false;
    const auto markSet = g.lex[run.a].a;
    const auto fence = LexRelayGen::literal_size(g, g.lexKid[q.a + at + 1]);
    if (!fence) return false;
    if (!ends_apart(g.lexKid[q.a + at - 1], g.sets[markSet])) return false;
    if ((before != kNoLexNode &&
         !LexRelayGen::literal(g, before, shape.before)) ||
        (after != kNoLexNode && !LexRelayGen::literal(g, after, shape.after)))
      return false;
    auto& row = shape.relay.row;
    row.markSet = markSet;
    row.fence = static_cast<std::uint16_t>(*fence);
    if (pad != kNoLexNode) {
      const LexNode& p = g.lex[pad];
      if (before == kNoLexNode || p.kind != LexKind::rep ||
          g.lex[p.a].kind != LexKind::chars)
        return false;
      if (const ByteSet& padBytes = g.sets[g.lex[p.a].a];
          !LexRelayGen::pad_final(padBytes, g.sets[markSet], run.min == 0,
                                  shape.after))
        return false;
      row.padSet = g.lex[p.a].a;
    }
    return true;
  }

  // One relay, from the sequence `q` whose kid `at` is the mark and whose next
  // kid is the fence; the walk resumes at `resumeOut`.
  int relay(const LexNode& q, unsigned at, int resumeOut, std::uint16_t before,
            std::uint16_t after, std::uint16_t pad, bool closes) {
    RelayShape shape;
    if (!relay_shape(q, at, before, after, pad, shape))
      throw std::logic_error("cursiv: an echo needs recognition");
    auto& row = shape.relay.row;
    row.term = n.term;
    row.tier = _tier;
    row.closes = closes;
    if (before != kNoLexNode) {
      row.beforeOff = internName(g, shape.before);
      row.beforeLen = static_cast<std::uint16_t>(shape.before.size());
    }
    if (after != kNoLexNode) {
      row.afterOff = internName(g, shape.after);
      row.afterLen = static_cast<std::uint16_t>(shape.after.size());
    }
    return lex_push_relay(n, std::move(shape.relay), resumeOut);
  }

  // The shape checked before reification contains only literal leaves.
  void lit_list(std::uint16_t node, std::vector<std::string>& out) const {
    if (const LexNode& e = g.lex[node]; e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i) lit_list(g.lexKid[e.a + i], out);
      return;
    }
    std::string bytes;
    if (!LexRelayGen::literal(g, node, bytes))
      throw std::logic_error("cursiv: a nested delimiter is not a literal");
    out.push_back(std::move(bytes));
  }

  // One relay counting its openers, ending at the closer that balances them.
  int nested_relay(std::uint16_t open, std::uint16_t close, bool eof,
                   int resumeOut) {
    std::vector<std::string> o, c;
    lit_list(open, o);
    lit_list(close, c);
    const bool several = o.size() > 1 || c.size() > 1;
    const auto side = [&](const std::vector<std::string>& list) {
      if (!several) return list.front();
      std::string listed;
      for (const std::string& lit : list) {
        listed += static_cast<char>(lit.size());
        listed += lit;
      }
      return listed;
    };
    const std::string before = side(o), after = side(c);
    LexRelayBuild r;
    r.row.term = n.term;
    r.row.tier = _tier;
    r.row.closes = true;
    r.row.nested = true;
    r.row.beforeOff = internName(g, before);
    r.row.beforeLen = static_cast<std::uint16_t>(before.size());
    r.row.afterOff = internName(g, after);
    r.row.afterLen = static_cast<std::uint16_t>(after.size());
    r.row.several = several;
    r.row.eof = eof;
    if (!several) {
      r.open = o.front();
      r.close = c.front();
    }
    return lex_push_relay(n, r, resumeOut);
  }

  bool nativeGuards = false;

  int recognition(std::uint16_t node, int out) {
    if (!echoes_marked(node)) {
      lower_refuse(g, LowerFail::echoUnmarked, n.term);
      return out;
    }
    LexRelayBuild r;
    r.row.term = n.term;
    r.row.tier = _tier;
    r.row.recognition = node;
    const int resume = n.relay(lex_push_relay(n, r, out));
    const auto [bytes, empty] = lex_first_bytes(g, node);
    if (!bytes.any()) return resume;
    const int head = n.chars(lex_intern_set(g, bytes), resume);
    return empty ? n.split(head, resume) : head;
  }

  bool assertions(std::uint16_t node) const {
    const LexNode& e = g.lex[node];
    if (e.kind == LexKind::positive || e.kind == LexKind::negative) return true;
    if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i)
        if (assertions(g.lexKid[e.a + i])) return true;
    } else if (e.kind == LexKind::rep || e.kind == LexKind::mark ||
               e.kind == LexKind::through_lit ||
               e.kind == LexKind::through_same) {
      return assertions(e.a);
    }
    return false;
  }

  // A repeated word refusal needs the general recognizer; a one-byte class
  // refusal stays native, and a predicate's refusals are its own graph's.
  std::vector<std::uint8_t> wordGuards = std::vector<std::uint8_t>(g.nlex);

  bool word_guard(std::uint16_t node) {
    if (wordGuards[node] != 0) return wordGuards[node] == 1;
    const LexNode& e = g.lex[node];
    bool answer = false;
    switch (e.kind) {
      case LexKind::nguard:
        answer = e.max == 0 && (e.min != 1 || e.a != kNoSet);
        break;
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b && !answer; ++i)
          answer = word_guard(g.lexKid[e.a + i]);
        break;
      case LexKind::rep:
        answer = e.max != 0 && word_guard(e.a);
        break;
      case LexKind::mark:
      case LexKind::through_lit:
      case LexKind::through_same:
      case LexKind::through_nested:
        answer = word_guard(e.a);
        break;
      default:
        break;
    }
    wordGuards[node] = answer ? 1 : 2;
    return answer;
  }

  // A terminal takes the compact NFA only when its echoes and guards fit.
  // Probe before emitting states or interning relay text.
  std::vector<std::uint8_t> native = std::vector<std::uint8_t>(g.nlex);

  bool native_run(std::uint16_t node) {
    if (native[node] != 0) return native[node] == 1;
    const LexNode& e = g.lex[node];
    const bool answer = [&] {
      switch (e.kind) {
        case LexKind::seq:
          for (unsigned i = 0; i < e.b; ++i) {
            const auto kid = g.lexKid[e.a + i];
            if (g.lex[kid].kind == LexKind::same) {
              RelayShape shape;
              if (i < 2 || g.lex[g.lexKid[e.a + i - 2]].min != g.lex[kid].min ||
                  !relay_shape(e, i - 2, kNoLexNode, kNoLexNode, kNoLexNode,
                               shape))
                return false;
            } else if (!native_run(kid)) {
              return false;
            }
          }
          return true;
        case LexKind::through_same: {
          const auto& opener = g.lex[e.a];
          RelayShape shape;
          return opener.kind == LexKind::seq && opener.b >= 2 &&
                 g.lex[g.lexKid[opener.a + opener.b - 2]].min == e.max &&
                 relay_shape(
                     opener, opener.b - 2, g.lexKid[e.b], g.lexKid[e.b + 1],
                     e.min > 2 ? g.lexKid[e.b + 2] : kNoLexNode, shape) &&
                 native_run(e.a);
        }
        case LexKind::alt:
          for (unsigned i = 0; i < e.b; ++i)
            if (!native_run(g.lexKid[e.a + i])) return false;
          return true;
        case LexKind::rep:
          return (e.max <= 1 || !word_guard(e.a)) && native_run(e.a);
        case LexKind::mark:
        case LexKind::through_lit:
        case LexKind::through_nested:
          return native_run(e.a);
        case LexKind::same:
        case LexKind::call:
          return false;
        case LexKind::positive:
        case LexKind::negative:
          // A standalone predicate has no token capture environment. A guard
          // reading an outer mark needs recognition of the whole terminal.
          return echoes_marked(node);
        default:
          return true;
      }
    }();
    native[node] = answer ? 1 : 2;
    return answer;
  }

  bool guards_read_outer_marks(std::uint16_t node) const {
    std::vector<std::uint32_t> marks;
    std::vector<std::uint16_t> guards;
    std::vector<bool> visited(g.nlex);
    const auto collect = [&](auto&& self, std::uint16_t at) {
      if (at == kNoLexNode || visited[at]) return;
      visited[at] = true;
      const LexNode& e = g.lex[at];
      if (e.kind == LexKind::mark) marks.push_back(e.min);
      if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
        guards.push_back(e.a);
        return;
      }
      detail::lex_children(g, at, [&](auto kid) { self(self, kid); }, false);
    };
    collect(collect, node);
    if (marks.empty() || guards.empty()) return false;
    visited.assign(visited.size(), false);
    const auto reads = [&](auto&& self, std::uint16_t at) {
      if (at == kNoLexNode || visited[at]) return false;
      visited[at] = true;
      if (const LexNode& e = g.lex[at];
          (e.kind == LexKind::same &&
           std::ranges::find(marks, e.min) != marks.end()) ||
          (e.kind == LexKind::through_same &&
           std::ranges::find(marks, e.max) != marks.end()))
        return true;
      bool found = false;
      detail::lex_children(
          g, at,
          [&](auto kid) {
            if (!found) found = self(self, kid);
          },
          false);
      return found;
    };
    return std::ranges::any_of(guards,
                               [&](auto at) { return reads(reads, at); });
  }

  // The accept of the terminal being built, and the one its literal branches
  // end on; the tier the branch in hand ranks at.
  int _match = -1;
  int _litMatch = -1;
  std::uint8_t _tier = 0;

  int branch_end(unsigned tiers, int out) {
    if (tiers != kLiteral || out != _match) return out;
    if (_litMatch < 0) _litMatch = n.match(n.s[_match].accept, true);
    return _litMatch;
  }

  template <class Build>
  int in_tier(unsigned tiers, Build&& build) {
    // A branch that reads several tiers at once ranks as a pattern.
    const std::uint8_t tier = tiers == kKeyword   ? kTierKeyword
                              : tiers == kLiteral ? kTierLiteral
                                                  : 0;
    const auto outer = std::exchange(_tier, tier);
    const int s = build();
    _tier = outer;
    return s;
  }

  // One branch of the terminal, to the accept its tier asks for.
  int branch(std::uint16_t node, int out) {
    if (const LexNode& e = g.lex[node]; e.kind == LexKind::alt) {
      int s = branch(g.lexKid[e.a], out);
      for (unsigned i = 1; i < e.b; ++i)
        s = n.split(branch(g.lexKid[e.a + i], out), s);
      return s;
    }
    const auto tiers = tiers_of(node);
    return in_tier(tiers, [&] { return run(node, branch_end(tiers, out)); });
  }

  bool recognized(std::uint16_t node) {
    return (!calls.empty() && calls[node]) || guards_read_outer_marks(node) ||
           !native_run(node);
  }

  // An arm the graph cannot spell is recognized apart from the others, at its
  // own tier.
  int arms(std::uint16_t node, int out) {
    if (const LexNode& e = g.lex[node];
        e.kind == LexKind::alt && recognized(node)) {
      int s = arms(g.lexKid[e.a], out);
      for (unsigned i = 1; i < e.b; ++i)
        s = n.split(arms(g.lexKid[e.a + i], out), s);
      return s;
    }
    if (recognized(node))
      return in_tier(tiers_of(node), [&] { return recognition(node, out); });
    return branch(node, out);
  }

  int terminal(std::uint16_t node, int out) {
    _match = out;
    _litMatch = -1;
    nativeGuards = assertions(node);
    return arms(node, out);
  }

  // A call owns its captures; a lookahead reads the current ones and exports
  // none. An echo is bound where a mark<> of its tag was set on every path to
  // it, so a mark in one arm or in a loop that may not turn binds nothing
  // after.
  bool echoes_marked(std::uint16_t node) const {
    using Tags = std::vector<std::uint32_t>;
    std::vector<bool> callsChecked(g.nlex);
    const auto scope = [&](auto&& self, std::uint16_t root) {
      std::vector<std::uint16_t> callsHere;
      bool bound = true;
      const auto walk = [&](auto&& walkSelf, std::uint16_t at, Tags& marks) {
        if (at == kNoLexNode) return;
        const LexNode& e = g.lex[at];
        switch (e.kind) {
          case LexKind::call:
            callsHere.push_back(lex_callee(g, e));
            return;
          case LexKind::mark:
            walkSelf(walkSelf, e.a, marks);
            if (std::ranges::find(marks, e.min) == marks.end())
              marks.push_back(e.min);
            return;
          case LexKind::same:
            bound = bound && std::ranges::find(marks, e.min) != marks.end();
            return;
          case LexKind::seq:
            for (unsigned i = 0; i < e.b; ++i)
              walkSelf(walkSelf, g.lexKid[e.a + i], marks);
            return;
          case LexKind::alt: {
            Tags common;
            for (unsigned i = 0; i < e.b; ++i) {
              Tags arm = marks;
              walkSelf(walkSelf, g.lexKid[e.a + i], arm);
              if (i == 0) {
                common = std::move(arm);
              } else {
                std::erase_if(common, [&](std::uint32_t tag) {
                  return std::ranges::find(arm, tag) == arm.end();
                });
              }
            }
            if (e.b != 0) marks = std::move(common);
            return;
          }
          case LexKind::rep: {
            if (e.max == 0) return;
            Tags turn = marks;
            walkSelf(walkSelf, e.a, turn);
            if (e.min != 0) marks = std::move(turn);
            return;
          }
          case LexKind::positive:
          case LexKind::negative: {
            Tags local = marks;
            walkSelf(walkSelf, e.a, local);
            return;
          }
          case LexKind::through_lit:
          case LexKind::through_nested:
            walkSelf(walkSelf, e.a, marks);
            return;
          case LexKind::through_same:
            walkSelf(walkSelf, e.a, marks);
            bound = bound && std::ranges::find(marks, e.max) != marks.end();
            return;
          default:
            return;
        }
      };
      Tags marks;
      walk(walk, root, marks);
      if (!bound) return false;
      for (const auto call : callsHere)
        if (!callsChecked[call]) {
          callsChecked[call] = true;
          if (!self(self, call)) return false;
        }
      return true;
    };
    callsChecked[node] = true;
    return scope(scope, node);
  }

  static bool is_condition(LexKind kind) {
    return kind == LexKind::positive || kind == LexKind::negative ||
           kind == LexKind::nguard;
  }

  // The kid of `q` that is a choice of several tiers, when assertions and
  // refusals alone stand around it, else `q.b`: a condition around a choice
  // stands around each branch.
  unsigned choice_among_conditions(const LexNode& q) const {
    unsigned at = q.b;
    for (unsigned i = 0; i < q.b; ++i) {
      if (is_condition(g.lex[g.lexKid[q.a + i]].kind)) continue;
      if (at != q.b) return q.b;
      at = i;
    }
    if (at == q.b || q.b == 1) return q.b;
    const std::uint16_t choice = g.lexKid[q.a + at];
    if (g.lex[choice].kind != LexKind::alt) return q.b;
    return std::has_single_bit(tiers_of(choice)) ? q.b : at;
  }

  // Thompson, built backwards from the fragment's exit.
  int run(std::uint16_t node, int out) {
    switch (const LexNode& e = g.lex[node]; e.kind) {
      case LexKind::chars:
        return n.chars(e.a, out);
      case LexKind::seq: {
        int s = out;
        unsigned i = e.b;
        if (const unsigned at = choice_among_conditions(e); at < e.b) {
          const LexNode& choice = g.lex[g.lexKid[e.a + at]];
          for (unsigned b = 0; b < choice.b; ++b) {
            const auto kid = g.lexKid[choice.a + b];
            const auto tiers = tiers_of(kid);
            const int arm = in_tier(tiers, [&] {
              int tail = branch_end(tiers, out);
              for (unsigned k = e.b; k-- > at + 1;)
                tail = run(g.lexKid[e.a + k], tail);
              tail = run(kid, tail);
              for (unsigned k = at; k-- > 0;)
                tail = run(g.lexKid[e.a + k], tail);
              return tail;
            });
            s = b == 0 ? arm : n.split(arm, s);
          }
          i = 0;
        }
        while (i-- > 0) {
          const auto kid = g.lexKid[e.a + i];
          if (g.lex[kid].kind == LexKind::nguard && g.lex[kid].max != 0) {
            s = n.chars_stray(g.lex[g.lexKid[e.a + i - 1]].a, kid, s);
            --i;
            continue;
          }
          if (g.lex[kid].kind == LexKind::same) {
            // The echo: the walk stops here and the search resumes at `s`.
            const int k = i < 2 || g.lex[g.lexKid[e.a + i - 2]].min !=
                                       g.lex[kid].min
                              ? -1
                              : relay(e, i - 2, s, kNoLexNode, kNoLexNode,
                                      kNoLexNode, false);
            if (k < 0)
              throw std::logic_error("cursiv: an echo needs recognition");
            s = n.relay(k);
            continue;
          }
          s = run(kid, s);
        }
        return s;
      }
      case LexKind::mark:
        return run(e.a, out);
      case LexKind::same:
        throw std::logic_error("cursiv: an echo needs recognition");
      case LexKind::indent:
      case LexKind::dedent:
      case LexKind::implied:
      case LexKind::skip:
      case LexKind::token:
      case LexKind::gap_has:
      case LexKind::gap_is:
      case LexKind::end_mark:
        lower_refuse(g, LowerFail::indentShape, n.term);
        return out;
      case LexKind::through_same: {
        const auto& opener = g.lex[e.a];
        if (opener.kind != LexKind::seq || opener.b < 2 ||
            g.lex[g.lexKid[opener.a + opener.b - 2]].min != e.max)
          throw std::logic_error("cursiv: an echo needs recognition");
        const int k = relay(opener, opener.b - 2, out, g.lexKid[e.b],
                            g.lexKid[e.b + 1],
                            e.min > 2 ? g.lexKid[e.b + 2] : kNoLexNode, true);
        return run(e.a, n.relay(k));
      }
      case LexKind::alt: {
        int s = run(g.lexKid[e.a], out);
        for (unsigned i = 1; i < e.b; ++i)
          s = n.split(run(g.lexKid[e.a + i], out), s);
        return s;
      }
      case LexKind::through_lit:
        return run(e.a, n.through(g, e, out));
      case LexKind::through_nested: {
        const int k = nested_relay(g.lexKid[e.b], g.lexKid[e.b + 1],
                                  e.max != 0, out);
        return run(e.a, n.relay(k));
      }
      case LexKind::positive:
      case LexKind::negative:
      case LexKind::nguard: {
        if (e.kind == LexKind::nguard) {
          bool crossing = nativeGuards && (e.min != 1 || e.a != kNoSet);
          if (nativeGuards && !crossing) {
            for (const int at : n.eclose(out))
              if (n.s[static_cast<std::size_t>(at)].relay >= 0 ||
                  n.s[static_cast<std::size_t>(at)].guard >= 0)
                crossing = true;
          }
          if (!crossing) return n.guard(node, out);
        } else if (!echoes_marked(node)) {
          throw std::logic_error("cursiv: an echo needs recognition");
        }
        LexRelayBuild r;
        r.row.term = n.term;
        r.row.tier = _tier;
        r.row.predicate = node;
        return n.relay(lex_push_relay(n, r, out));
      }
      case LexKind::rep:
        return n.repeat(e, out, [&](int tail) { return run(e.a, tail); });
      case LexKind::call:
        throw std::logic_error("cursiv: a lexical call needs recognition");
    }
    throw std::logic_error(
        "cursiv: a lexical node of no kind the graph spells");
  }
};

}  // namespace cursiv
