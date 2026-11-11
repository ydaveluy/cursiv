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

#include "cursiv/shared/gen/LexKeywordCut.hpp"
#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/utils/RoundMarks.hpp"

namespace cursiv::engine {

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

  // What a branch of a terminal reads, as a mask: the priorities its readings
  // rank at.
  static constexpr unsigned kPattern = 1, kLiteral = 2, kKeyword = 4;

  unsigned priorities_of(std::uint16_t node) const {
    auto out = 0u;
    for (std::vector todo{node}; !todo.empty();) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      const LexNode& e = g.lex[at];
      if (e.kind == LexKind::alt) {
        for (unsigned i = 0; i < e.b; ++i) todo.push_back(g.lexChild[e.a + i]);
        continue;
      }
      if (const auto own = LexKeywordCut::branches(g, at); own.any && own.all) {
        out |= kKeyword;
        continue;
      }
      if (e.kind != LexKind::seq || e.min == 1) {
        out |= e.kind != LexKind::seq ? kPattern : kLiteral;
        continue;
      }
      auto core = 0u;
      std::uint16_t only = 0;
      for (unsigned i = 0; i < e.b; ++i) {
        const auto child = g.lexChild[e.a + i];
        if (is_condition(g.lex[child].kind)) continue;
        ++core;
        only = child;
      }
      if (core == 1)
        todo.push_back(only);
      else
        out |= kPattern;
    }
    return out;
  }

  // Whether every class a node can end on breaks `run`, for the fence in
  // front of a mark: the byte before a run must break its class. False where
  // a class it ends on is not known.
  bool ends_apart(std::uint16_t node, const ByteSet& run) const {
    for (std::vector todo{node}; !todo.empty();) {
      const LexNode& e = g.lex[todo.back()];
      todo.pop_back();
      switch (e.kind) {
        case LexKind::chars:
          if (g.sets[e.a].intersects(run)) return false;
          break;
        case LexKind::seq: {
          unsigned i = e.b;
          while (i > 0 &&
                 g.lex[g.lexChild[e.a + i - 1]].kind == LexKind::nguard)
            --i;
          if (i == 0) return false;
          todo.push_back(g.lexChild[e.a + i - 1]);
          break;
        }
        case LexKind::alt:
          for (unsigned i = 0; i < e.b; ++i)
            todo.push_back(g.lexChild[e.a + i]);
          break;
        default:
          return false;
      }
    }
    return true;
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
    const auto& mark = g.lex[g.lexChild[q.a + at]];
    if (mark.kind != LexKind::mark) return false;
    const auto& run = g.lex[mark.a];
    if (run.kind != LexKind::rep || g.lex[run.a].kind != LexKind::chars)
      return false;
    const auto markSet = g.lex[run.a].a;
    const auto fence = LexRelayGen::literal_size(g, g.lexChild[q.a + at + 1]);
    if (!fence) return false;
    if (!ends_apart(g.lexChild[q.a + at - 1], g.sets[markSet])) return false;
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

  // One relay, from the sequence `q` whose child `at` is the mark and whose
  // next child is the fence; the walk resumes at `resumeOut`.
  int relay(const LexNode& q, unsigned at, int resumeOut, std::uint16_t before,
            std::uint16_t after, std::uint16_t pad, bool closes) {
    RelayShape shape;
    if (!relay_shape(q, at, before, after, pad, shape))
      throw std::logic_error("cursiv: an echo needs recognition");
    auto& row = shape.relay.row;
    row.term = n.term;
    row.priority = _priority;
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
    for (std::vector todo{node}; !todo.empty();) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      if (const LexNode& e = g.lex[at]; e.kind == LexKind::alt) {
        for (unsigned i = e.b; i-- > 0;) todo.push_back(g.lexChild[e.a + i]);
        continue;
      }
      std::string bytes;
      if (!LexRelayGen::literal(g, at, bytes))
        throw std::logic_error("cursiv: a nested delimiter is not a literal");
      out.push_back(std::move(bytes));
    }
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
    r.row.priority = _priority;
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

  Descent<int> recognition(std::uint16_t node, int out) {
    if (!co_await echoes_marked(node)) {
      lower_refuse(g, LowerFail::echoUnmarked, n.term);
      co_return out;
    }
    LexRelayBuild r;
    r.row.term = n.term;
    r.row.priority = _priority;
    r.row.recognition = node;
    const int resume = n.relay(lex_push_relay(n, r, out));
    const auto [bytes, empty] = lex_first_bytes(g, node);
    if (!bytes.any()) co_return resume;
    const int head = n.chars(lex_intern_set(g, bytes), resume);
    co_return empty ? n.split(head, resume) : head;
  }

  bool assertions(std::uint16_t node) const {
    for (std::vector todo{node}; !todo.empty();) {
      const LexNode& e = g.lex[todo.back()];
      todo.pop_back();
      if (e.kind == LexKind::positive || e.kind == LexKind::negative)
        return true;
      if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
        for (unsigned i = 0; i < e.b; ++i) todo.push_back(g.lexChild[e.a + i]);
      } else if (e.kind == LexKind::rep || e.kind == LexKind::mark ||
                 e.kind == LexKind::through_lit ||
                 e.kind == LexKind::through_same) {
        todo.push_back(e.a);
      }
    }
    return false;
  }

  // A repeated word refusal needs the general recognizer; a one-byte class
  // refusal stays native, and a predicate's refusals are its own graph's.
  std::vector<std::uint8_t> wordGuards = std::vector<std::uint8_t>(g.nlex);

  Descent<bool> word_guard(std::uint16_t node) {
    if (wordGuards[node] != 0) co_return wordGuards[node] == 1;
    const LexNode& e = g.lex[node];
    bool answer = false;
    switch (e.kind) {
      case LexKind::nguard:
        answer = e.max == 0 && (e.min != 1 || e.a != kNoSet);
        break;
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b && !answer; ++i)
          answer = co_await word_guard(g.lexChild[e.a + i]);
        break;
      case LexKind::rep:
        if (e.max != 0) answer = co_await word_guard(e.a);
        break;
      case LexKind::mark:
      case LexKind::through_lit:
      case LexKind::through_same:
      case LexKind::through_nested:
        answer = co_await word_guard(e.a);
        break;
      default:
        break;
    }
    wordGuards[node] = answer ? 1 : 2;
    co_return answer;
  }

  // A terminal takes the compact NFA only when its echoes and guards fit.
  // Probe before emitting states or interning relay text.
  std::vector<std::uint8_t> native = std::vector<std::uint8_t>(g.nlex);

  Descent<bool> native_run(std::uint16_t node) {
    if (native[node] != 0) co_return native[node] == 1;
    const LexNode& e = g.lex[node];
    bool answer = true;
    switch (e.kind) {
      case LexKind::seq:
        for (unsigned i = 0; i < e.b && answer; ++i) {
          const auto child = g.lexChild[e.a + i];
          if (g.lex[child].kind == LexKind::same) {
            RelayShape shape;
            answer = i >= 2 &&
                     g.lex[g.lexChild[e.a + i - 2]].min == g.lex[child].min &&
                     relay_shape(e, i - 2, kNoLexNode, kNoLexNode, kNoLexNode,
                                 shape);
          } else {
            answer = co_await native_run(child);
          }
        }
        break;
      case LexKind::through_same: {
        const auto& opener = g.lex[e.a];
        RelayShape shape;
        answer = opener.kind == LexKind::seq && opener.b >= 2 &&
                 g.lex[g.lexChild[opener.a + opener.b - 2]].min == e.max &&
                 relay_shape(
                     opener, opener.b - 2, g.lexChild[e.b], g.lexChild[e.b + 1],
                     e.min > 2 ? g.lexChild[e.b + 2] : kNoLexNode, shape);
        if (answer) answer = co_await native_run(e.a);
        break;
      }
      case LexKind::alt:
        for (unsigned i = 0; i < e.b && answer; ++i)
          answer = co_await native_run(g.lexChild[e.a + i]);
        break;
      case LexKind::rep:
        if (e.max > 1) answer = !co_await word_guard(e.a);
        if (answer) answer = co_await native_run(e.a);
        break;
      case LexKind::mark:
      case LexKind::through_lit:
      case LexKind::through_nested:
        answer = co_await native_run(e.a);
        break;
      case LexKind::same:
      case LexKind::call:
        answer = false;
        break;
      case LexKind::positive:
      case LexKind::negative:
        // A standalone predicate has no token capture environment. A guard
        // reading an outer mark needs recognition of the whole terminal.
        answer = co_await echoes_marked(node);
        break;
      default:
        break;
    }
    native[node] = answer ? 1 : 2;
    co_return answer;
  }

  // A node is visited in the walk whose number it holds. The walks below run
  // once per node of a terminal and never nest, so they share the array, and a
  // new number empties it whole.
  RoundMarks _seen{g.nlex};

  bool guards_read_outer_marks(std::uint16_t node) {
    std::vector<std::uint32_t> marks;
    std::vector<std::uint16_t> guards;
    std::vector<std::uint16_t> todo{node};
    _seen.next();
    while (!todo.empty()) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      if (at == kNoLexNode || !_seen.first(at)) continue;
      const LexNode& e = g.lex[at];
      if (e.kind == LexKind::mark) marks.push_back(e.min);
      if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
        guards.push_back(e.a);
        continue;
      }
      lex_children(g, at, [&](auto child) { todo.push_back(child); }, false);
    }
    if (marks.empty() || guards.empty()) return false;
    _seen.next();
    todo = guards;
    while (!todo.empty()) {
      const std::uint16_t at = todo.back();
      todo.pop_back();
      if (at == kNoLexNode || !_seen.first(at)) continue;
      if (const LexNode& e = g.lex[at];
          (e.kind == LexKind::same &&
           std::ranges::find(marks, e.min) != marks.end()) ||
          (e.kind == LexKind::through_same &&
           std::ranges::find(marks, e.max) != marks.end()))
        return true;
      lex_children(g, at, [&](auto child) { todo.push_back(child); }, false);
    }
    return false;
  }

  // The accept of the terminal being built, and the one its literal branches
  // end on; the priority the branch in hand ranks at.
  int _match = -1;
  int _litMatch = -1;
  std::uint8_t _priority = 0;

  int branch_end(unsigned priorities, int out) {
    if (priorities != kLiteral || out != _match) return out;
    if (_litMatch < 0) _litMatch = n.match(n.s[_match].accept, true);
    return _litMatch;
  }

  // The priority of a branch: one that reads several at once ranks as a
  // pattern.
  static std::uint8_t rank(unsigned priorities) {
    return priorities == kKeyword   ? kKeywordPriority
           : priorities == kLiteral ? kLiteralPriority
                                    : 0;
  }

  // The arms of the choice `node`, each built by the member `build` up to
  // `out`.
  Descent<int> each_arm(std::uint16_t node, int out, auto build) {
    const LexNode& e = g.lex[node];
    int s = co_await (this->*build)(g.lexChild[e.a], out);
    for (unsigned i = 1; i < e.b; ++i) {
      const int arm = co_await (this->*build)(g.lexChild[e.a + i], out);
      s = n.split(arm, s);
    }
    co_return s;
  }

  // One branch of the terminal, to the accept its priority asks for.
  Descent<int> branch(std::uint16_t node, int out) {
    if (g.lex[node].kind == LexKind::alt)
      co_return co_await each_arm(node, out, &LexNfaBuild::branch);
    const auto priorities = priorities_of(node);
    const auto outer = std::exchange(_priority, rank(priorities));
    const int s = co_await run(node, branch_end(priorities, out));
    _priority = outer;
    co_return s;
  }

  Descent<bool> recognized(std::uint16_t node) {
    if ((!calls.empty() && calls[node]) || guards_read_outer_marks(node))
      co_return true;
    co_return !co_await native_run(node);
  }

  // An arm the graph cannot spell is recognized apart from the others, at its
  // own priority.
  Descent<int> arms(std::uint16_t node, int out) {
    const bool apart = co_await recognized(node);
    if (g.lex[node].kind == LexKind::alt && apart)
      co_return co_await each_arm(node, out, &LexNfaBuild::arms);
    if (!apart) co_return co_await branch(node, out);
    const auto outer = std::exchange(_priority, rank(priorities_of(node)));
    const int s = co_await recognition(node, out);
    _priority = outer;
    co_return s;
  }

  int terminal(std::uint16_t node, int out) {
    _match = out;
    _litMatch = -1;
    nativeGuards = assertions(node);
    return descend(arms(node, out));
  }

  using Tags = std::vector<std::uint32_t>;

  // Whether every echo is bound: a mark<> of its tag set on every path to it.
  // A call owns its captures; a lookahead exports none.
  Descent<bool> echoes_marked(std::uint16_t node) {
    _seen.next();
    _seen.first(node);
    for (std::vector roots{node}; !roots.empty();) {
      const std::uint16_t root = roots.back();
      roots.pop_back();
      std::vector<std::uint16_t> callsHere;
      bool bound = true;
      Tags marks;
      co_await marked(root, marks, callsHere, bound);
      if (!bound) co_return false;
      for (const auto call : callsHere)
        if (_seen.first(call)) roots.push_back(call);
    }
    co_return true;
  }

  // The marks set on every path through `at` into `marks`, the calls it makes
  // into `callsHere`; `bound` falls at an echo no mark before it binds.
  Descent<void> marked(std::uint16_t at, Tags& marks,
                       std::vector<std::uint16_t>& callsHere, bool& bound) {
    if (at == kNoLexNode) co_return;
    const LexNode& e = g.lex[at];
    switch (e.kind) {
      case LexKind::call:
        callsHere.push_back(lex_callee(g, e));
        co_return;
      case LexKind::mark:
        co_await marked(e.a, marks, callsHere, bound);
        if (std::ranges::find(marks, e.min) == marks.end())
          marks.push_back(e.min);
        co_return;
      case LexKind::same:
        bound = bound && std::ranges::find(marks, e.min) != marks.end();
        co_return;
      case LexKind::seq:
        for (unsigned i = 0; i < e.b; ++i)
          co_await marked(g.lexChild[e.a + i], marks, callsHere, bound);
        co_return;
      case LexKind::alt: {
        Tags common;
        for (unsigned i = 0; i < e.b; ++i) {
          Tags arm = marks;
          co_await marked(g.lexChild[e.a + i], arm, callsHere, bound);
          if (i == 0) {
            common = std::move(arm);
          } else {
            std::erase_if(common, [&](std::uint32_t tag) {
              return std::ranges::find(arm, tag) == arm.end();
            });
          }
        }
        if (e.b != 0) marks = std::move(common);
        co_return;
      }
      case LexKind::rep: {
        if (e.max == 0) co_return;
        Tags turn = marks;
        co_await marked(e.a, turn, callsHere, bound);
        if (e.min != 0) marks = std::move(turn);
        co_return;
      }
      case LexKind::positive:
      case LexKind::negative: {
        Tags local = marks;
        co_await marked(e.a, local, callsHere, bound);
        co_return;
      }
      case LexKind::through_lit:
      case LexKind::through_nested:
        co_await marked(e.a, marks, callsHere, bound);
        co_return;
      case LexKind::through_same:
        co_await marked(e.a, marks, callsHere, bound);
        bound = bound && std::ranges::find(marks, e.max) != marks.end();
        co_return;
      default:
        co_return;
    }
  }

  static bool is_condition(LexKind kind) {
    return kind == LexKind::positive || kind == LexKind::negative ||
           kind == LexKind::nguard;
  }

  // The child of `q` that is a choice of several priorities, when assertions
  // and refusals alone stand around it, else `q.b`: a condition around a choice
  // stands around each branch.
  unsigned choice_among_conditions(const LexNode& q) const {
    unsigned at = q.b;
    for (unsigned i = 0; i < q.b; ++i) {
      if (is_condition(g.lex[g.lexChild[q.a + i]].kind)) continue;
      if (at != q.b) return q.b;
      at = i;
    }
    if (at == q.b || q.b == 1) return q.b;
    const std::uint16_t choice = g.lexChild[q.a + at];
    if (g.lex[choice].kind != LexKind::alt) return q.b;
    return std::has_single_bit(priorities_of(choice)) ? q.b : at;
  }

  // Whether the walk behind a refusal may read a byte its class decodes at: an
  // edge is narrowed by bytes, so a scalar the class refuses would pass it. A
  // loop whose head is not built yet may read anything.
  bool reads_decoded(const ByteSet& decoded, int out) const {
    if (!decoded.any()) return false;
    std::vector<int> walks{out};
    for (std::size_t i = 0; i < walks.size(); ++i)
      for (const int at : n.eclose(walks[i])) {
        const LexNfaState& st = n.s[static_cast<std::size_t>(at)];
        if (st.set >= 0 ? g.sets[st.set].intersects(decoded)
                        : st.t1 < 0 && st.t2 >= 0)
          return true;
        if (st.guard < 0) continue;
        const int behind = n.guards[static_cast<std::size_t>(st.guard)].out;
        if (std::ranges::find(walks, behind) == walks.end())
          walks.push_back(behind);
      }
    return false;
  }

  // Thompson, built backwards from the fragment's exit.
  Descent<int> run(std::uint16_t node, int out) {
    switch (const LexNode& e = g.lex[node]; e.kind) {
      case LexKind::chars:
        co_return n.chars(e.a, out);
      case LexKind::seq: {
        int s = out;
        unsigned i = e.b;
        if (const unsigned at = choice_among_conditions(e); at < e.b) {
          const LexNode& choice = g.lex[g.lexChild[e.a + at]];
          for (unsigned b = 0; b < choice.b; ++b) {
            const auto child = g.lexChild[choice.a + b];
            const auto priorities = priorities_of(child);
            const auto outer = std::exchange(_priority, rank(priorities));
            int tail = branch_end(priorities, out);
            for (unsigned k = e.b; k-- > at + 1;)
              tail = co_await run(g.lexChild[e.a + k], tail);
            tail = co_await run(child, tail);
            for (unsigned k = at; k-- > 0;)
              tail = co_await run(g.lexChild[e.a + k], tail);
            _priority = outer;
            s = b == 0 ? tail : n.split(tail, s);
          }
          i = 0;
        }
        while (i-- > 0) {
          const auto child = g.lexChild[e.a + i];
          if (g.lex[child].kind == LexKind::nguard && g.lex[child].max != 0) {
            s = n.chars_lone_lead(g.lex[g.lexChild[e.a + i - 1]].a, child, s);
            --i;
            continue;
          }
          if (g.lex[child].kind == LexKind::same) {
            // The echo: the walk stops here and the search resumes at `s`.
            const int k =
                i < 2 || g.lex[g.lexChild[e.a + i - 2]].min != g.lex[child].min
                    ? -1
                    : relay(e, i - 2, s, kNoLexNode, kNoLexNode, kNoLexNode,
                            false);
            if (k < 0)
              throw std::logic_error("cursiv: an echo needs recognition");
            s = n.relay(k);
            continue;
          }
          s = co_await run(child, s);
        }
        co_return s;
      }
      case LexKind::mark:
        co_return co_await run(e.a, out);
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
        co_return out;
      case LexKind::through_same: {
        const auto& opener = g.lex[e.a];
        if (opener.kind != LexKind::seq || opener.b < 2 ||
            g.lex[g.lexChild[opener.a + opener.b - 2]].min != e.max)
          throw std::logic_error("cursiv: an echo needs recognition");
        const int k = relay(opener, opener.b - 2, out, g.lexChild[e.b],
                            g.lexChild[e.b + 1],
                            e.min > 2 ? g.lexChild[e.b + 2] : kNoLexNode, true);
        co_return co_await run(e.a, n.relay(k));
      }
      case LexKind::alt:
        co_return co_await each_arm(node, out, &LexNfaBuild::run);
      case LexKind::through_lit:
        co_return co_await run(e.a, n.through(g, e, out));
      case LexKind::through_nested: {
        const int k =
            nested_relay(g.lexChild[e.b], g.lexChild[e.b + 1], e.max != 0, out);
        co_return co_await run(e.a, n.relay(k));
      }
      case LexKind::positive:
      case LexKind::negative:
      case LexKind::nguard: {
        if (e.kind == LexKind::nguard) {
          const bool oneClass = e.min == 1 && e.a == kNoSet;
          bool crossing = nativeGuards && !oneClass;
          if (nativeGuards && !crossing) {
            for (const int at : n.eclose(out))
              if (n.s[static_cast<std::size_t>(at)].relay >= 0 ||
                  n.s[static_cast<std::size_t>(at)].guard >= 0)
                crossing = true;
          }
          if (!crossing && oneClass)
            crossing = reads_decoded(lex_decode_bytes(g, g.lexChild[e.b]), out);
          if (!crossing) co_return n.guard(node, out);
        } else if (!co_await echoes_marked(node)) {
          throw std::logic_error("cursiv: an echo needs recognition");
        }
        LexRelayBuild r;
        r.row.term = n.term;
        r.row.priority = _priority;
        r.row.predicate = node;
        co_return n.relay(lex_push_relay(n, r, out));
      }
      case LexKind::rep: {
        // Thompson repetition, each occurrence built backwards.
        int tail = out;
        if (e.max == kEndless) {
          tail = n.split(-1, out);
          const int head = co_await run(e.a, tail);
          n.s[static_cast<std::size_t>(tail)].t1 = head;
        } else {
          for (std::uint32_t i = e.max; i > e.min; --i) {
            const int body = co_await run(e.a, tail);
            tail = n.split(body, out);
          }
        }
        for (std::uint32_t i = 0; i < e.min; ++i)
          tail = co_await run(e.a, tail);
        co_return tail;
      }
      case LexKind::call:
        throw std::logic_error("cursiv: a lexical call needs recognition");
    }
    throw std::logic_error(
        "cursiv: a lexical node of no kind the graph spells");
  }
};

}  // namespace cursiv::engine
