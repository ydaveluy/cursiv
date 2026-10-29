// What may be written at a point, answered from the stamps the descent left
// on the tree and nothing else. A position is the whole key: what the program
// can read once that position is read is a build-time question, and the
// lexer is what says whether the answer can be spliced into the text.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/engines/ll/runtime/LlSpine.hpp"
#include "cursiv/engines/ll/runtime/LlTail.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/runtime/ll_spine.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"
#include "cursiv/engines/shared/runtime/token_end_from.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Tokens a replay takes when no record narrows it further.
inline constexpr unsigned kLlReplayLeaves = 32;

// A token the descent read and a replay can read again: a repair's own
// records carry no position, and a hidden run never had one. A word read as
// the one it misspells was read for real, and carries its position.
inline bool ll_replayable(const Cst& cst, StampLayout stamp,
                          const CstLeaf& leaf) {
  return !leaf.hidden && leaf.begin != leaf.end &&
         !(leaf.recovered && stamp.state(cst[leaf.id]) == 0);
}

// A token the repair invented, which the descent read like any other.
inline bool ll_invented(const CstLeaf& leaf) {
  return leaf.recovered && !leaf.hidden && leaf.begin == leaf.end;
}

// A dedent the parse laid: no width, and it closed a level.
template <const auto& T>
inline bool ll_dedent(const Cst& cst, StampLayout stamp, const CstLeaf& leaf) {
  if constexpr (kLaid<T, kLaidDedent> == 0) {
    return false;
  } else {
    const std::uint16_t pos = stamp.state(cst[leaf.id]);
    return !leaf.hidden && leaf.begin == leaf.end && pos != 0 &&
           pos <= T.nafter && T.nodes[pos - 1].term == kLaid<T, kLaidDedent>;
  }
}

// Where the gap in front of `offset` begins: the end of the last leaf before
// it that is not trivia, the text the parse dropped included.
inline TextOffset ll_gap_from(const Cst& cst, TextOffset offset) {
  for (NodeCount rank = cst.leaves_before(offset); rank-- > 0;) {
    const CstLeaf leaf = cst.leaf(rank);
    if (!leaf.hidden && leaf.begin != leaf.end) return leaf.end;
  }
  return 0;
}

// The last such token that starts before `offset`, or the last dedent laid at
// it where a line starts: that line is read at the level the dedent leaves.
// `rank` is where it stands.
template <const auto& T>
inline CstLeaf ll_leaf_before(const Cst& cst, StampLayout stamp,
                              TextOffset offset, NodeCount& rank) {
  rank = cst.leaves_before(offset);
  if constexpr (kLaid<T, kLaidDedent> != 0) {
    const auto held = [] { return TextOffset{0}; };
    for (NodeCount at =
             indent_line_start(cst.text().data(), offset, held) ? rank : 0;
         at-- > 0;) {
      const CstLeaf leaf = cst.leaf(at);
      if (leaf.begin != offset || leaf.end != offset) break;
      if (ll_dedent<T>(cst, stamp, leaf)) {
        rank = at;
        return leaf;
      }
    }
  }
  while (rank-- > 0) {
    const CstLeaf leaf = cst.leaf(rank);
    if (leaf.begin < offset && ll_replayable(cst, stamp, leaf)) return leaf;
  }
  return {};
}

// Whether the operator opening at `hop`, of a step that groups nothing, would
// chain behind the operand the last token ends: the innermost join of its
// climb that the token ends and that binds no tighter is of its step. The
// climb takes no such operator, as the table engine's cell refuses it.
template <const auto& T>
inline bool ll_chains_nothing(const Cst& cst, StampLayout stamp,
                              const LlSpine& spine, const CstLeaf& leaf,
                              std::uint16_t hop) {
  if constexpr (T.nodeStep == nullptr) return false;
  const std::uint32_t step = hop != kNoRow ? T.nodeStep[hop] : 0u;
  if ((step & 1u) == 0 || !leaf) return false;
  const auto records = spine.records();
  bool joined = false;
  for (std::size_t i = records.size(); i-- > 0;) {
    // The token ends the record: no later leaf, invented or not, is in it.
    NodeId last = records[i];
    do --last;
    while (cst[last].subtreeSize != 1 || cst[last].hidden);
    if (last != leaf.id) break;
    std::uint32_t op = 0;
    for (const NodeId kid : cst.reverse_children(records[i])) {
      const std::uint16_t pos =
          cst[kid].subtreeSize == 1 ? stamp.state(cst[kid]) : 0;
      if (pos != 0 && pos <= T.nafter &&
          (T.nodeStep[pos - 1] >> 16) == (step >> 16))
        op = T.nodeStep[pos - 1];
    }
    // One climb: the step's number orders the rest. Past a join, a record
    // that joins nothing ends the chain.
    if (op == 0 ? joined : (op >> 1) <= (step >> 1))
      return op != 0 && (op >> 1) == (step >> 1);
    joined = joined || op != 0;
  }
  return false;
}

template <const auto& T>
inline std::uint16_t ll_call_at(std::uint32_t ret) {
  return ret < T.atn.nstates ? T.returnNode[ret] : kNoRow;
}

// What one reading of the stack names, folded into what the others named. The
// first answer stands; a second that differs ends the question, since a member
// two readings disagree about is a guess, and an editor shown a guess cannot
// tell it from an answer.
struct LlAgree {
  ElementId site = kNoElement;
  std::string_view member;
  bool seen = false;
  bool split = false;
  // A path ran out of frames: the stack below the last one is not known.
  bool floor = false;
  unsigned frames = 0;

  bool fold(const StampedGrammar& grammar, ElementId s, std::string_view m) {
    if (!seen) {
      seen = true;
      site = s;
      member = m;
      return true;
    }
    if (split) return false;
    if (member == m &&
        (site == s || (grammar.rowType(site) == grammar.rowType(s) &&
                       grammar.rowNarrow(site) == grammar.rowNarrow(s))))
      return true;
    split = true;
    return false;
  }
};

// The member every reading agrees on. A path up the stack gives the first call
// that names one, and a path that reaches the top naming none is a reading
// too: `NULL` under a select is the operand of a null test on one reading and
// the item's own value on the other, and neither is the answer. A context
// reached twice is read once: a stack that forks at every level is as large as
// its graph, not as its routes.
template <const auto& T>
inline bool ll_member_up(const StampedGrammar& grammar, LlSimulation& sim,
                         std::uint32_t ctx, LlAgree& out) {
  struct Cursor {
    std::uint32_t ctx;
    unsigned next;
  };
  const LlCtxPool& pool = sim.contexts();
  std::vector<Cursor> path{{ctx, 0}};
  std::unordered_set<std::uint32_t> seen;
  bool forked = false;
  while (!path.empty()) {
    const Cursor at = path.back();
    if (at.ctx == kLlCtxNone || at.ctx == kLlCtxApart) {
      out.floor = out.floor || at.ctx == kLlCtxNone;
      if (!out.fold(grammar, kNoElement, {})) return false;
      path.pop_back();
      continue;
    }
    const unsigned count = pool.count(at.ctx);
    if (at.next == count) {
      path.pop_back();
      continue;
    }
    forked = forked || count > 1;
    ++out.frames;
    const std::uint16_t outer = ll_call_at<T>(pool.ret(at.ctx, at.next));
    const ElementId above = outer != kNoRow ? T.nodes[outer].row : kNoElement;
    const std::string_view named = grammar.rowFeature(above);
    const std::uint32_t parent = pool.parent(at.ctx, at.next);
    if (at.next + 1 == count)
      path.pop_back();
    else
      ++path.back().next;
    if (!named.empty()) {
      if (!out.fold(grammar, above, named)) return false;
    } else if (!forked || seen.insert(parent).second) {
      path.push_back({parent, 0});
    }
  }
  return true;
}

// The widths a replay takes in turn, each wider than the last: a few tokens,
// then back to the first token of each record the last token stands inside,
// the innermost first -- the frame that opened a record is still open at the
// caret -- and at last the whole text. The records give the stack, and the
// first width is none.
class LlReplayWidth {
 public:
  LlReplayWidth(const Cst& cst, StampLayout stamp, const LlSpine& spine,
                NodeCount end)
      : _cst(cst), _stamp(stamp), _spine(spine), _rank(end) {}

  unsigned next() {
    if (_round++ == 0) return _last = 0;
    const unsigned wider = _held != 0 ? std::exchange(_held, 0u) : record();
    if (!_few && _last < kLlReplayLeaves &&
        (wider == 0 || wider > kLlReplayLeaves)) {
      _few = true;
      _held = wider;
      return _last = kLlReplayLeaves;
    }
    return _last = wider != 0 ? wider : _cst.leaf_count();
  }

 private:
  unsigned record() {
    if (!_opened) {
      _opened = true;
      _level = _spine.records().size();
    }
    while (_level > 0) {
      const NodeCount stop =
          _cst.leaves_before(_cst[_spine.records()[--_level]].begin);
      while (_rank > stop)
        if (ll_replayable(_cst, _stamp, _cst.leaf(--_rank))) ++_seen;
      if (_seen > _last + 1u) return _seen - 1u;
    }
    return 0;
  }

  const Cst& _cst;
  StampLayout _stamp;
  const LlSpine& _spine;
  std::size_t _level = 0;
  NodeCount _rank;
  unsigned _seen = 0;
  unsigned _last = 0;
  unsigned _held = 0;
  unsigned _round = 0;
  bool _opened = false;
  bool _few = false;
};

enum class LlReplay { settled, shallow, broken };

// The calls a rule is entered through, up to the document's own entry:
// `exact` where it gets there. A rule with several callers is entered through
// the one the records of the tree allow -- `known` names the rules of those
// that stand open, the innermost first, and a caller owes as many as the
// groups around its call -- and where more than one still does, the stack
// stops below it. Without `trusted` it stops at the first rule with several.
template <const auto& T>
inline std::uint32_t ll_callers(LlSimulation& sim, std::uint32_t state,
                                std::uint16_t node,
                                std::span<const std::uint16_t> known,
                                bool trusted, bool& exact) {
  struct Walk {
    std::span<const std::uint16_t> known;
    std::unordered_map<std::uint64_t, unsigned> memo;
    unsigned budget = 512;

    static unsigned owed(std::uint32_t caller) {
      const std::uint16_t call = ll_call_at<T>(caller);
      return call != kNoRow ? T.nodes[call].covers : 0u;
    }
    bool holds(std::size_t used, unsigned count, std::uint16_t rule) const {
      if (used + count > known.size()) return false;
      for (unsigned k = 0; k < count; ++k)
        if (known[used + k] != rule) return false;
      return true;
    }
    // How many ways, two at most, a frame of `rule` is entered up to the
    // document with every record accounted for, `used` of them already.
    unsigned ways(std::uint16_t rule, std::size_t used) {
      if (rule == 0) return used == known.size() ? 1u : 0u;
      const std::uint64_t key = (std::uint64_t{rule} << 32) | used;
      const auto [at, fresh] = memo.try_emplace(key, 2u);
      if (!fresh || budget == 0) return at->second;
      --budget;
      unsigned total = 0;
      const std::uint32_t base = T.atn.callerBase[rule];
      for (std::uint32_t i = base; total < 2 && i < T.atn.callerBase[rule + 1];
           ++i) {
        const std::uint32_t caller = T.atn.callerAt[i];
        const std::uint16_t above = T.atn.state(caller).rule;
        if (holds(used, owed(caller), above))
          total += ways(above, used + owed(caller));
      }
      return memo[key] = std::min(total, 2u);
    }
  };
  std::vector<std::uint32_t> chain;
  const auto climb = [&](bool narrow) {
    Walk walk{known, {}};
    chain.clear();
    exact = false;
    std::uint16_t rule = T.atn.state(state).rule;
    std::size_t used = 0;
    bool open = narrow && walk.holds(0, T.nodes[node].covers, rule);
    if (open) used = T.nodes[node].covers;
    bool chose = false;
    // The climb is a function of where it stands: coming back to a position it
    // has stood on is a cycle that never reaches the entry.
    struct Stand {
      std::uint16_t rule;
      std::size_t used;
      bool open;
      bool operator==(const Stand&) const = default;
    };
    Stand mark{rule, used, open};
    unsigned span = 1, stepped = 0;
    for (;;) {
      if (rule == 0) {
        exact = true;
        break;
      }
      const std::uint32_t base = T.atn.callerBase[rule];
      const std::uint32_t count = T.atn.callerBase[rule + 1] - base;
      if (count == 0) break;
      std::uint32_t pick = T.atn.callerAt[base];
      if (count > 1) {
        unsigned viable = 0;
        for (std::uint32_t i = 0; open && i < count; ++i) {
          const std::uint32_t caller = T.atn.callerAt[base + i];
          const std::uint16_t above = T.atn.state(caller).rule;
          const unsigned owes = Walk::owed(caller);
          if (walk.holds(used, owes, above) &&
              walk.ways(above, used + owes) != 0) {
            ++viable;
            pick = caller;
          }
        }
        if (viable == 1) {
          const std::uint16_t call = ll_call_at<T>(pick);
          if (call != kNoRow && T.nodes[call].head) {
            // A wrapper's record may reach past the caret, so it cannot settle
            // what these calls choose after one child: replay from before them.
            const std::uint16_t above = T.atn.state(pick).rule;
            for (std::uint32_t i = 0; i < count; ++i) {
              const std::uint32_t other = T.atn.callerAt[base + i];
              if (other == pick || T.atn.state(other).rule != above) continue;
              const std::uint16_t sibling = ll_call_at<T>(other);
              if (sibling != kNoRow && T.nodes[sibling].head) {
                viable = 2;
                break;
              }
            }
          }
        }
        if (viable != 1) break;
        chose = true;
      }
      const unsigned owes = Walk::owed(pick);
      const std::uint16_t above = T.atn.state(pick).rule;
      if (open && walk.holds(used, owes, above))
        used += owes;
      else
        open = false;
      chain.push_back(pick);
      rule = above;
      const Stand now{rule, used, open};
      if (now == mark) break;
      if (++stepped == span) mark = now, span *= 2, stepped = 0;
    }
    return !chose || (open && (!exact || used == known.size()));
  };
  if (!trusted || !climb(true)) climb(false);
  std::uint32_t ctx = kLlCtxNone;
  for (std::size_t i = chain.size(); i-- > 0;)
    ctx = sim.contexts().push(ctx, chain[i]);
  return ctx;
}

// The readings after the tokens before rank `end`, each on the stack the
// descent stood on. The replay reads the last `window` of them from the state
// the stamp of the first states, or all of them from the document's start;
// `exact` says whether the stack it ends on reaches the document's. A reading
// that returns past the first one's rule has left that stack, and the first
// token's conditions may read the text a candidate replaces: both are
// `shallow`, and a wider replay knows more. A token no reading can take is
// `broken`. The stack under the first token is read off the records of the
// last token; `bound` false leaves it unknown. The steps from the first whose
// conditions reach the caret on are kept in `tail`. The last `loose` tokens
// are read as their terminals at every position that takes one, not only at
// the position the descent took.
template <const auto& T>
inline LlReplay ll_replay(LlSimulation& sim, const Cst& cst, StampLayout stamp,
                          const LlSpine& spine, NodeCount end, unsigned window,
                          bool bound, bool& exact, LlTail& tail,
                          unsigned loose = 0) {
  std::vector<CstLeaf> taken;
  NodeCount rank = end;
  unsigned read = 0;
  while (rank > 0 && read <= window) {
    const CstLeaf leaf = cst.leaf(--rank);
    if (ll_replayable(cst, stamp, leaf)) {
      taken.push_back(leaf);
      ++read;
    } else if (ll_invented(leaf) || ll_dedent<T>(cst, stamp, leaf)) {
      taken.push_back(leaf);
    }
  }
  exact = read <= window;
  if constexpr (T.ngapConds == 0) (void)tail;
  std::size_t next = taken.size();
  if (exact) {
    sim.seed(T.atnStart, kLlCtxNone);
  } else {
    const CstLeaf first = taken.back();
    const std::uint16_t pos = stamp.state(cst[first.id]);
    if (pos == 0 || pos > T.nafter || T.nodeAfter[pos - 1] == 0xFFFFFFFFu)
      return LlReplay::broken;
    // A condition asked of the first token or before it is not asked again: a
    // wider replay answers where one reads the text a candidate replaces.
    if constexpr (T.ngapConds != 0)
      if (bound && tail.covers(T.condReach, first, tail.onward + read))
        return LlReplay::shallow;
    const std::uint32_t state = T.nodeAfter[pos - 1];
    std::uint32_t stack = kLlCtxNone;
    if (bound) {
      std::vector<NodeId> around;
      std::vector<std::uint16_t> rules;
      ll_ancestors_near(cst, spine.records(), first.id, around);
      const bool trusted = ll_stamped_rules(cst, stamp, around, rules);
      stack = ll_callers<T>(sim, state, pos - 1, rules, trusted, exact);
    }
    sim.seed(state, stack);
    --next;
    if (bound && !exact && sim.at_unbound_return()) return LlReplay::shallow;
  }
  const std::string_view text = cst.text();
  std::vector<LlConfig> held;
  std::vector<std::pair<std::uint16_t, bool>> asked;
  // The tokens that stand between a step and the caret, itself among them.
  unsigned ahead = tail.onward;
  for (std::size_t i = 0; i < next; ++i)
    ahead += taken[i].begin != taken[i].end ? 1u : 0u;
  while (next-- > 0) {
    const CstLeaf leaf = taken[next];
    const TextOffset gapFrom = ll_gap_from(cst, leaf.begin);
    const bool laid = leaf.begin == leaf.end;
    [[maybe_unused]] const unsigned between = laid ? 0u : ahead--;
    const std::uint16_t pos = stamp.state(cst[leaf.id]);
    if (pos == 0 || pos > T.nafter) return LlReplay::broken;
    const bool any = next < loose && !laid;
    const auto reads = [&](std::uint32_t edge) {
      if (!any) return T.edgeNode[edge] == pos - 1;
      const LlAtnEdge& e = T.atn.edge(edge);
      const std::uint16_t hop = T.edgeNode[edge];
      return e.kind == LlEdgeKind::atom && e.label == T.nodes[pos - 1].term &&
             hop != kNoRow && T.nodes[hop].term != kNoRow;
    };
    if (laid) {
      if constexpr (T.ngapConds != 0) {
        const SymbolId term = T.nodes[pos - 1].term;
        if (term == kNoRow) return LlReplay::broken;
        if (!tail.steps.empty())
          tail.steps.push_back(
              {Token{leaf.begin, leaf.begin, term}, gapFrom, kNoRow, term, {}});
        sim.filter([](std::uint16_t) { return true; });
        if (bound && !exact && sim.at_unbound_return())
          return LlReplay::shallow;
      }
      if (!sim.step_edges(reads)) return LlReplay::broken;
      if (bound && !exact && sim.at_unbound_return()) return LlReplay::shallow;
      continue;
    }
    bool crossed = false;
    if constexpr (T.ngapConds != 0) {
      if (T.nodes[pos - 1].term == kNoRow) return LlReplay::broken;
      const Token token{leaf.begin, leaf.end, T.nodes[pos - 1].term};
      const bool kept =
          !tail.steps.empty() || tail.covers(T.condReach, leaf, between);
      if (kept && tail.steps.empty())
        tail.before.assign(sim.configs().begin(), sim.configs().end());
      // The tokens a phrase reads past its own are its own, which the leaves
      // between it and the caret need not bound: one that reads the text a
      // candidate replaces keeps its step too.
      bool reaches = false;
      asked.clear();
      if (sim.needs_filter()) {
        held.assign(sim.configs().begin(), sim.configs().end());
        crossed = sim.filter([&](std::uint16_t condition) {
          TextOffset seen = 0;
          const bool holds = ll_condition_text<T>(text, condition, token,
                                                  gapFrom, false, &seen);
          reaches = reaches || tail.reaches(seen, text.size());
          asked.emplace_back(condition, holds);
          return holds;
        }) && sim.step_edges(reads);
        if (!crossed) sim.load(held);
      }
      if (!kept && reaches && T.condReach.phrase && tail.steps.empty())
        tail.before = held;
      if (kept || (reaches && T.condReach.phrase))
        tail.steps.push_back({token, gapFrom,
                              static_cast<std::uint16_t>(pos - 1),
                              kInvalidSymbol, asked});
    }
    if (!crossed && !sim.step_edges(reads)) return LlReplay::broken;
    if (bound && !exact && sim.at_unbound_return()) return LlReplay::shallow;
  }
  return LlReplay::settled;
}

// The text past `resume` that a condition asked of a candidate can read.
template <const auto& T>
inline std::string_view ll_condition_tail(const Cst& cst, TextOffset resume) {
  const CondReach& reach = T.condReach;
  const std::string_view text = cst.text();
  std::size_t read = reach.look;
  if (reach.tokens != 0) {
    TextOffset end = static_cast<TextOffset>(text.size());
    if (reach.tokens < kUnboundedTokens) {
      const NodeCount onward = cst.leaves_before(resume);
      end = token_end_from(
          cst, onward < cst.leaf_count() ? cst.leaf(onward).id : cst.size(),
          reach.tokens);
    }
    read = std::max<std::size_t>(read, end > resume ? end - resume : 0u);
  }
  return text.substr(resume, read);
}

// Reads `term` as a token of the text. The assertion that a section ends has
// an atom of its own for the token that closes it, and reads nothing: that
// token is the caller's, and the caller's position is the edge that reads it.
// `mode` takes the alphabet the first position that reads it lexes under.
template <const auto& T>
inline bool ll_step_token(LlSimulation& sim, std::uint16_t term,
                          std::uint16_t* mode = nullptr) {
  return sim.step_edges([&](std::uint32_t edge) {
    const LlAtnEdge& e = T.atn.edge(edge);
    const std::uint16_t hop = T.edgeNode[edge];
    const bool reads = e.kind == LlEdgeKind::atom && e.label == term &&
                       hop != kNoRow && T.nodes[hop].term != kNoRow;
    if (reads && mode != nullptr && *mode == kNoRow)
      *mode = T.nodes[hop].mode;
    return reads;
  });
}

// Whether the token `mode` reads at `from` still ends at `to` with a word typed
// right behind, as `StampedGrammar::ends_before_word` asks it.
template <const auto& T>
inline bool ll_ends_before_word(std::string_view text, std::uint16_t mode,
                                TextOffset from, TextOffset to, SymbolId term) {
  return StampedGrammar::ends_before_word(
      text.substr(from, to - from), term, {T.terms, T.nterms}, T.names,
      [&](std::string_view s) {
        return ll_match_text<T>(s.data(), static_cast<TextOffset>(s.size()),
                                mode, 0);
      });
}

// Whether `take` takes a sample word that `mode` reads whole as `term`, or no
// sample word reads as it: a terminal that spells nothing is tried as the
// words it reads as.
template <const auto& T, class Take>
inline bool ll_sample_takes(SymbolId term, std::uint16_t mode, Take&& take) {
  bool known = false;
  for (const std::string_view word : StampedGrammar::samples) {
    const Scan s = ll_match_text<T>(
        word.data(), static_cast<TextOffset>(word.size()), mode, 0);
    if (s.term != term || s.end != word.size()) continue;
    known = true;
    if (take(word)) return true;
  }
  return !known;
}

// Whether `mode` reads the text from `from` to `to` as trivia and arrives at
// `to`. `tail` takes the last whole token read, `begin` where it starts.
template <const auto& T>
inline bool ll_trivia_to(std::string_view text, std::uint16_t mode,
                         TextOffset from, TextOffset to, TextOffset& begin,
                         Scan& tail) {
  tail = {};
  while (from < to) {
    const Scan s = ll_match_text<T>(
        text.data(), static_cast<TextOffset>(text.size()), mode, from);
    if (s.term == kInvalidSymbol || s.end <= from || !T.terms[s.term].trivia)
      return false;
    if (s.end > to) return ll_ends_before_word<T>(text, mode, from, to, s.term);
    begin = from;
    tail = s;
    from = s.end;
  }
  return true;
}

template <const auto& T>
inline void ll_next_features_at(const StampedGrammar& grammar, const Cst& cst,
                                TextOffset at, TextOffset resume,
                                std::vector<NextFeature>& out) {
  if (!StampedGrammar::caret_in(cst, at, resume)) return;
  const StampLayout stamp = grammar.stamp();
  NodeCount rank = 0;
  const CstLeaf leaf = ll_leaf_before<T>(cst, stamp, at, rank);
  const std::uint16_t pos = leaf ? stamp.state(cst[leaf.id]) : 0;
  if (pos > T.nafter) return;
  const std::uint32_t seed = pos == 0 ? T.atnStart : T.nodeAfter[pos - 1];
  if (seed == 0xFFFFFFFFu) return;
  const std::string_view text = cst.text();

  // A literal is answered only where the lexer accepts it spliced in, and the
  // lexer is the judge, since the boundary is the literal's own. Left: glued
  // to the leaf, the leaf's token re-lexed with the candidate behind it must
  // still end where it did. Right: the candidate, lexed where the text
  // resumes at `resume`, must match its whole spelling and nothing more.
  const SymbolId leafTerm = pos != 0 ? T.nodes[pos - 1].term : kNoRow;
  // A token the parse laid is no text a lexer reads again; a word a repair
  // read as another is the author's, and what is typed behind it may glue to
  // it.
  const bool glued = leaf && leaf.end == at &&
                     leafTerm != kLaid<T, kLaidIndent> &&
                     leafTerm != kLaid<T, kLaidDedent>;
  // An alphabet no position named is no alphabet: the walk reads under the
  // one that admits everything rather than off the end of the table.
  const auto alphabet = [&](std::uint16_t m) {
    return m < T.nmodes ? m : T.errMode;
  };
  const std::uint16_t leafMode =
      alphabet(pos != 0 ? T.nodes[pos - 1].mode : T.errMode);
  // The gap in front of a region's first token is its caller's, so a token is
  // not always reached across the gap its own position crosses.
  std::uint16_t afterGap = kNoRow;
  if (T.nodeAfterGap != nullptr)
    afterGap = pos == 0 ? T.tailMode : T.nodeAfterGap[pos - 1];
  // A comment, hidden or ignored, that ends at the caret and closes on
  // nothing of its own goes on over what is typed there: `//` then `entity` is
  // one comment. A candidate is offered where the comment read again with it
  // behind still ends here; the leaves the tape laid of no width behind it do
  // not hide it. It is read under the alphabet of the last token, and where
  // there is none under the candidate's.
  NodeCount behind = cst.leaves_before(at);
  while (behind > 0 && cst.leaf(behind - 1).begin == cst.leaf(behind - 1).end)
    --behind;
  const CstLeaf prior = behind > 0 ? cst.leaf(behind - 1) : CstLeaf{};
  const TextOffset gapFrom = ll_gap_from(cst, at);
  // What the blanks and comments between the last token and the caret come
  // to under the alphabet of one candidate: whether they are crossed to reach
  // the caret -- a caret inside a comment is part of it -- and the comment
  // that ends there, if one does.
  struct Comment {
    std::uint16_t key = 0;
    bool crossed = true;
    bool present = false;
    std::uint16_t mode = 0;
    TextOffset begin = 0;
    Scan scan{};
  };
  std::vector<Comment> comments;
  const auto comment_in = [&](std::uint16_t candidate) -> const Comment& {
    for (const Comment& seen : comments)
      if (seen.key == candidate) return seen;
    Comment found{candidate, true, false, leaf ? leafMode : candidate};
    if (prior.hidden && !prior.recovered && prior.end == at) {
      const Scan s =
          ll_match_text<T>(text.data(), prior.end, found.mode, prior.begin);
      if (s.term != kInvalidSymbol && s.end == prior.end) {
        found.present = true;
        found.begin = prior.begin;
        found.scan = s;
      }
    } else if constexpr (!kIndented<T>) {
      TextOffset begin = 0;
      Scan last{};
      const TextOffset from = prior ? prior.end : 0;
      found.crossed = ll_trivia_to<T>(text, candidate, from, at, begin, last);
      if (!prior.recovered &&
          (found.mode == candidate
               ? found.crossed
               : ll_trivia_to<T>(text, found.mode, from, at, begin, last)) &&
          last.term != kInvalidSymbol && last.end == at && last.end > begin &&
          !ll_ends_before_word<T>(text, found.mode, begin, at, last.term)) {
        found.present = true;
        found.begin = begin;
        found.scan = last;
      }
    }
    return comments.emplace_back(found);
  };
  std::string splice;
  const auto spliced = [&](TextOffset from, std::string_view spelling,
                           std::uint16_t mode) {
    return StampedGrammar::spliced(
        splice, text, from, at, resume, spelling, [&](TextOffset& halt) {
          return ll_match_text<T>(splice.data(),
                                  static_cast<TextOffset>(splice.size()), mode,
                                  0, halt);
        });
  };
  const auto comment_ends = [&](const Comment& comment,
                                std::string_view spelling) {
    const Scan s = spliced(comment.begin, spelling, comment.mode);
    return s.term == comment.scan.term && s.end == at - comment.begin;
  };
  // A candidate with no spelling is typed as some word its terminal reads:
  // the comment lets it through where the word ends it, and where no sample
  // word is read the terminal is left as offered.
  const auto comment_lets = [&](std::string_view spelling, SymbolId label,
                                std::uint16_t mode, std::uint16_t gap) {
    const Comment& comment = comment_in(gap);
    if (!comment.present) return true;
    if (!spelling.empty()) return comment_ends(comment, spelling);
    return ll_sample_takes<T>(label, mode, [&](std::string_view word) {
      return comment_ends(comment, word);
    });
  };
  // A mode that holds several readings of one text elects one of them: the
  // terminal asked is read there only if the text reads as it too, its guard
  // may refuse. `s` is what `spliced` read last.
  const auto reads_as = [&](const Scan& s, SymbolId term, std::uint16_t mode) {
    if (s.term == kInvalidSymbol) return false;
    if (term == kNoRow || s.term == term) return true;
    if (T.modeMulti == nullptr || T.modeMulti[mode] == 0) return false;
    std::vector<std::uint16_t> rivals;
    LlMask<T.termWords> readable;
    readable.assign(T.modeTerms + std::size_t{mode} * T.termWords,
                    kLlVisibleTerms<T>);
    const TextOffset end =
        ll_match_in<T>(splice.data(), static_cast<TextOffset>(splice.size()), 0,
                       readable, rivals, nullptr, nullptr, nullptr, mode);
    return end == s.end && std::ranges::find(rivals, term) != rivals.end();
  };
  // The terminals some reading at the caret takes: the lexer chooses once
  // among them for every reading.
  LlMask<T.termWords> everyone;
  const auto lexes_in_place = [&](std::string_view spelling, std::uint16_t mode,
                                  SymbolId label) {
    const Scan s = spliced(at, spelling, mode);
    if (!reads_as(s, label, mode) || s.end != spelling.size()) return false;
    std::vector<std::uint16_t> chosen;
    const TextOffset end = ll_match_in<T>(
        splice.data(), static_cast<TextOffset>(splice.size()), 0, everyone,
        chosen, nullptr, nullptr, nullptr, mode);
    return end == s.end && std::ranges::find(chosen, label) != chosen.end();
  };
  // The verdict belongs to a terminal and the alphabet it is read under, not
  // to each reading that offers it. A terminal of several spellings fits where
  // one of them would.
  std::unordered_map<std::uint32_t, bool> spliced_in;
  const auto fits = [&](SymbolId label, std::uint16_t mode,
                        std::span<const std::string_view> spellings) {
    const auto [slot, fresh] =
        spliced_in.try_emplace(std::uint32_t{label} << 16 | mode, false);
    if (fresh) {
      const std::uint16_t gap = afterGap != kNoRow ? alphabet(afterGap) : mode;
      const auto fits_as = [&](std::string_view spelling) {
        return comment_lets(spelling, label, mode, gap) &&
               (spelling.empty() || lexes_in_place(spelling, mode, label));
      };
      slot->second =
          comment_in(gap).crossed &&
          (spellings.empty() ? fits_as({})
                             : std::ranges::any_of(spellings, fits_as));
    }
    return slot->second;
  };

  // The closure the prediction runs, run here instead: what the graph can
  // read once that position is read is the same question a choice asks, and
  // asking it twice in two ways is two answers to keep in step.
  const TextOffset lead = leaf ? ll_gap_from(cst, leaf.begin) : 0;
  std::string conditioned;
  std::string_view past;
  if constexpr (T.ngapConds != 0) past = ll_condition_tail<T>(cst, resume);
  // A replay that ends on a stack it only knows from some token on cannot say
  // what the readings that return past that token go on to read: those are
  // asked again from further back.
  const NodeCount before = !leaf ? 0
                           : leaf.begin == leaf.end
                               ? rank
                               : cst.leaves_before(leaf.begin);
  const LlSpine spine(cst, leaf);
  LlReplayWidth widths(cst, stamp, spine, before);
  for (unsigned window = widths.next();; window = widths.next()) {
    thread_local LlSimulation sim(T.atn);
    sim.forget();
    conditioned.clear();
    LlTail tail;
    tail.edge = at;
    tail.onward = leaf ? 1u : 0u;
    bool exact = true;
    LlReplay replayed =
        ll_replay<T>(sim, cst, stamp, spine, before, window, true, exact, tail);
    if (replayed == LlReplay::shallow) continue;
    bool widens = !exact;
    bool beyond = false;
    if (replayed == LlReplay::broken) {
      tail.steps.clear();
      replayed =
          ll_replay<T>(sim, cst, stamp, spine, before, 0, false, exact, tail);
      widens = false;
    }
    // THE LAST TOKEN'S POSITION IS THE ONE READING THE DESCENT SETTLED ON, and
    // where the text stops before the arms part, that reading is a guess: the
    // descent had to name one. Reading the token again from the position
    // BEFORE it keeps every arm that admits it, and a caret answers from all
    // of them.
    bool stood = replayed == LlReplay::settled && !leaf;
    std::vector<LlConfig> held;
    Scan again{};
    bool lexed = false;
    std::vector<std::uint16_t> readings;
    std::vector<std::uint16_t> readingMode;
    // What the configurations before the last token admit, and whether the
    // replay read the token from them.
    LlMask<T.termWords> wanted;
    bool weighable = false;
    // A condition asked of `buffer`. The tokens a phrase reads past its own
    // are its own, which the leaves of the tape need not bound: `ranOut` where
    // one read to the end of the text a candidate's buffer keeps.
    bool ranOut = false;
    const auto condition_on = [&](std::string_view buffer,
                                  std::uint16_t condition, const Token& token,
                                  TextOffset from, TextOffset* read = nullptr) {
      TextOffset seen = 0;
      const bool holds =
          ll_condition_text<T>(buffer, condition, token, from, false, &seen);
      if (T.condReach.phrase && seen >= buffer.size()) ranOut = true;
      if (read != nullptr) *read = seen;
      return holds;
    };
    // A condition of the last token that reads the text a candidate replaces
    // is the candidate's to answer: on the document every reading stands, and
    // each candidate reads the token again on its own text.
    bool deferred = false;
    // The last token read again from `start`, once as each terminal it can
    // be, its conditions asked of `buffer`, which begins at `base`. `alt` names
    // the reading a configuration follows: 0 the one the descent settled on,
    // `k + 1` the k-th of `readings`.
    const auto stand = [&](const std::vector<LlConfig>& start,
                           std::string_view buffer, TextOffset base,
                           bool keep) {
      std::vector<LlConfig> tagged;
      const auto ask = [&](std::uint16_t reading, LlTail::Step* kept,
                           std::uint16_t tag) {
        tagged.assign(start.begin(), start.end());
        for (LlConfig& config : tagged) config.alt = tag;
        sim.load(tagged);
        if constexpr (T.ngapConds != 0)
          if (lexed) {
            sim.filter([&](std::uint16_t condition) {
              TextOffset seen = 0;
              const bool holds = condition_on(
                  buffer, condition,
                  Token{leaf.begin - base, leaf.end - base, reading},
                  lead - base, &seen);
              const bool defers =
                  kept != nullptr && tail.reaches(seen, text.size());
              if (kept != nullptr) kept->asked.emplace_back(condition, holds);
              deferred = deferred || defers;
              return holds || defers;
            });
            beyond = beyond || (widens && sim.at_unbound_return());
          }
      };
      const auto note = [&](std::uint16_t reading) -> LlTail::Step* {
        if (!keep) return nullptr;
        LlTail::Step& step = tail.stood.emplace_back();
        step.token = Token{leaf.begin, leaf.end, reading};
        step.gapFrom = lead;
        return &step;
      };
      std::vector<LlConfig> landed;
      for (std::size_t k = 0; k < readings.size(); ++k) {
        ask(readings[k], note(readings[k]), static_cast<std::uint16_t>(k + 1));
        std::uint16_t read_in = kNoRow;
        if (!ll_step_token<T>(sim, readings[k], &read_in)) continue;
        if (read_in != kNoRow && readings[k] != leafTerm)
          readingMode[k] = alphabet(read_in);
        for (const LlConfig& config : sim.configs())
          if (std::ranges::find(landed, config) == landed.end())
            landed.push_back(config);
      }
      if (!landed.empty()) {
        sim.load(landed);
        return true;
      }
      ask(again.term, lexed ? note(again.term) : nullptr, 0);
      return sim.step_edges(
          [&](std::uint32_t edge) { return T.edgeNode[edge] == pos - 1; });
    };
    if (replayed == LlReplay::settled && leaf) {
      held.assign(sim.configs().begin(), sim.configs().end());
      again = ll_match_text<T>(text.data(), leaf.end, T.errMode, leaf.begin);
      lexed = again.end == leaf.end && again.term != kInvalidSymbol;
      if (lexed) {
        sim.possible_terms([&](std::uint16_t term) {
          if (term != kEofSymbol) wanted.set(term);
        });
        if (ll_match_in<T>(text.data(), leaf.end, leaf.begin, wanted,
                           readings) != leaf.end)
          readings.clear();
      }
      readingMode.assign(readings.size(), leafMode);
      stood = stand(held, text, 0, true);
      weighable = lexed && stood;
    }
    if (!stood) {
      sim.seed(seed, kLlCtxNone);
      widens = false;
      tail.steps.clear();
    }
    if (widens && sim.at_unbound_return()) continue;
    std::vector<LlConfig> guarded;
    std::vector<LlConfig> candidates;
    std::vector<std::int8_t> allowed;
    std::vector<std::vector<LlConfig>> survivors;
    if constexpr (T.ngapConds != 0) {
      guarded.assign(sim.configs().begin(), sim.configs().end());
      sim.filter([](std::uint16_t) { return true; });
      beyond = beyond || (widens && sim.at_unbound_return());
      candidates.assign(sim.configs().begin(), sim.configs().end());
      allowed.assign(T.nterms, -1);
      survivors.resize(T.nterms);
    }
    // The conditions of the replay's last steps that read as far as the text a
    // candidate replaces are asked again on the text with it spliced in.
    const bool redoable =
        T.ngapConds != 0 &&
        (!tail.steps.empty() || deferred ||
         (stood && leaf && tail.covers(T.condReach, leaf, 1u)));
    const TextOffset reread =
        !tail.steps.empty() ? tail.steps.front().gapFrom : lead;
    const auto redo = [&](std::string_view buffer, TextOffset base) {
      if (!tail.steps.empty()) sim.load(tail.before);
      for (const LlTail::Step& step : tail.steps) {
        if (step.node == kNoRow) {
          sim.filter([](std::uint16_t) { return true; });
          beyond = beyond || (widens && sim.at_unbound_return());
          if (!ll_step_token<T>(sim, step.inserted)) return false;
          continue;
        }
        const auto reads = [&](std::uint32_t edge) {
          return T.edgeNode[edge] == step.node;
        };
        bool crossed = false;
        if (sim.needs_filter()) {
          const std::vector<LlConfig> pending(sim.configs().begin(),
                                              sim.configs().end());
          const bool kept = sim.filter([&](std::uint16_t condition) {
            return condition_on(buffer, condition,
                                Token{step.token.begin - base,
                                      step.token.end - base, step.token.sym},
                                step.gapFrom - base);
          });
          // Where the candidate's text refuses the position the descent
          // took, the token is read at every position that text admits.
          const std::vector<LlConfig> passed(sim.configs().begin(),
                                             sim.configs().end());
          crossed = kept && sim.step_edges(reads);
          if (!crossed && kept) {
            sim.load(passed);
            crossed = ll_step_token<T>(sim, step.token.sym);
          }
          if (!crossed) sim.load(pending);
          beyond = beyond || (widens && sim.at_unbound_return());
        }
        if (!crossed && !sim.step_edges(reads)) return false;
      }
      return stand(tail.steps.empty()
                       ? held
                       : std::vector<LlConfig>(sim.configs().begin(),
                                               sim.configs().end()),
                   buffer, base, false);
    };
    // Whether the conditions of the tail answer on the text a candidate
    // splices in as they did on the document: the readings are then the same.
    const auto unchanged = [&](std::string_view buffer, TextOffset base) {
      const auto alike = [&](const LlTail::Step& step) {
        for (const auto& [condition, holds] : step.asked)
          if (condition_on(buffer, condition,
                           Token{step.token.begin - base, step.token.end - base,
                                 step.token.sym},
                           step.gapFrom - base) != holds)
            return false;
        return true;
      };
      return std::ranges::all_of(tail.stood, alike) &&
             std::ranges::all_of(tail.steps, alike);
    };
    const auto condition_allows = [&](SymbolId term, std::uint16_t mode,
                                      std::span<const std::string_view>
                                          spellings) {
      if constexpr (T.ngapConds == 0) {
        (void)term, (void)mode, (void)spellings;
        return true;
      } else {
        if (allowed[term] >= 0) return allowed[term] != 0;
        const TextOffset base = redoable ? reread : gapFrom;
        const auto admits = [&](bool redone, auto&& holds) {
          if (!redone) sim.load(guarded);
          sim.filter(holds);
          beyond = beyond || (widens && sim.at_unbound_return());
          bool any = false;
          for (const LlConfig& config : sim.configs()) {
            for (const LlAtnEdge& edge : T.atn.outgoing(config.state)) {
              if (edge.kind == LlEdgeKind::atom && edge.label == term) {
                if (std::ranges::find(survivors[term], config) ==
                    survivors[term].end())
                  survivors[term].push_back(config);
                any = true;
                break;
              }
            }
          }
          return any;
        };
        const auto attempt = [&](std::string_view sample) {
          const std::size_t gap = at - base;
          if (conditioned.size() < gap)
            conditioned.assign(text.substr(base, gap));
          conditioned.resize(gap);
          conditioned.append(sample);
          conditioned.append(past);
          const Scan lexed = ll_match_text<T>(
              conditioned.data(), static_cast<TextOffset>(conditioned.size()),
              mode, static_cast<TextOffset>(gap));
          const Token token{static_cast<TextOffset>(gap),
                            lexed.term == term && lexed.end > gap
                                ? lexed.end
                                : static_cast<TextOffset>(gap + sample.size()),
                            term};
          const bool retry =
              redoable && (deferred || !unchanged(conditioned, base));
          const bool redone = retry && redo(conditioned, base);
          if (retry && !redone) return false;
          return admits(redone, [&](std::uint16_t condition) {
            return condition_on(conditioned, condition, token, gapFrom - base);
          });
        };
        // A phrase that read to the end of the text kept is asked again over
        // more of it, what the shorter text let through set aside.
        const auto accepts = [&](std::string_view sample) {
          const std::size_t kept = survivors[term].size();
          const bool wasBeyond = beyond;
          for (;;) {
            ranOut = false;
            const bool answer = attempt(sample);
            if (!ranOut || resume + past.size() >= text.size()) return answer;
            past =
                text.substr(resume, std::max<std::size_t>(16, 2 * past.size()));
            survivors[term].resize(kept);
            beyond = wasBeyond;
          }
        };
        // The candidate is typed as each of its spellings, or as each sample
        // word it reads where it spells none. Every word a reading admits
        // keeps it: the member is what all of them agree on.
        bool answer = false, known = !spellings.empty();
        for (const std::string_view spelling : spellings)
          answer = accepts(spelling) || answer;
        if (!known)
          for (const std::string_view sample : StampedGrammar::samples) {
            const Scan scanned = ll_match_text<T>(
                sample.data(), static_cast<TextOffset>(sample.size()), mode, 0);
            if (scanned.term != term || scanned.end != sample.size()) continue;
            known = true;
            answer = accepts(sample) || answer;
          }
        // A terminal no sample spells is answered by its body where it decides
        // a condition, and kept otherwise.
        if (!known)
          answer = admits(false, [&](std::uint16_t condition) {
            return ll_condition_named<T>(text, condition, Token{at, at, term},
                                         gapFrom) != 0;
          });
        allowed[term] = answer;
        return allowed[term] != 0;
      }
    };
    // One entry per terminal, not per reading: two readings offer one token,
    // and the member is what they agree it fills -- taking whichever was closed
    // first would show a guess with the face of an answer.
    constexpr std::uint16_t kNoSlot = 0xFFFFu;
    std::vector<std::uint16_t> slot_of(T.nterms, kNoSlot);
    std::vector<std::uint16_t> seen;
    std::vector<LlAgree> agreed;
    const auto fold = [&](const LlConfig& c, std::uint32_t edge,
                          LlAgree& into) {
      const std::uint16_t hop = T.edgeNode[edge];
      // Innermost first: a position owes the run its directives state, and
      // the first write of that run is the one the token itself fills.
      const ElementId own = hop != kNoRow ? T.nodes[hop].row : kNoElement;
      const std::string_view named = grammar.rowFeature(own);
      if (!named.empty()) {
        into.fold(grammar, own, named);
        return;
      }
      // ...and where the position names none and no token of its rule
      // stands before it, the call that entered the rule does -- several
      // returns above it, since a rule reached through a choice or a rename
      // writes nothing on the way. A token past the first fills no member
      // its rule's caller states.
      if (hop == kNoRow || !T.nodes[hop].head) {
        into.fold(grammar, kNoElement, {});
        return;
      }
      ll_member_up<T>(grammar, sim, c.ctx, into);
    };
    // A choice still open before the token at the caret read it under every
    // terminal its arms admit there, and the position that takes it reads it
    // again in its own mode: `%` then `-` takes one arm, `%` then `->`
    // another. The arms are the configurations the token is read from, and
    // those the tokens before it lead to read as their terminals anywhere,
    // where the descent named one position for each. With the candidate glued
    // behind it, the token read under all of them must end where it did, if
    // the document's text reads it whole so. The arms are asked once, and only
    // where every terminal together reads the token on past the caret: no
    // alphabet does where they do not.
    LlMask<T.termWords> every;
    every.assign(kLlVisibleTerms<T>.data(), kLlVisibleTerms<T>);
    std::vector<std::uint16_t> rivals;
    bool weighed = false, chosen = false;
    const auto choice_reads = [&](std::string_view spelling) {
      const auto reach = [&](const LlMask<T.termWords>& under) {
        return StampedGrammar::spliced(
            splice, text, leaf.begin, at, resume, spelling,
            [&](TextOffset& halt) {
              return ll_match_in<T>(splice.data(),
                                    static_cast<TextOffset>(splice.size()), 0,
                                    under, rivals, &halt);
            });
      };
      if (!weighable || reach(every) <= at - leaf.begin) return true;
      if (!weighed) {
        weighed = true;
        thread_local LlSimulation arms(T.atn);
        arms.forget();
        LlTail loose;
        loose.edge = at;
        loose.onward = 1u;
        bool whole = true;
        if (ll_replay<T>(arms, cst, stamp, spine, before, kLlReplayLeaves,
                         false, whole, loose,
                         kLlReplayLeaves) == LlReplay::settled)
          arms.possible_terms([&](std::uint16_t term) {
            if (term != kEofSymbol) wanted.set(term);
          });
        chosen =
            ll_match_in<T>(text.data(), static_cast<TextOffset>(text.size()),
                           leaf.begin, wanted, rivals) == leaf.end;
      }
      return !chosen || reach(wanted) == at - leaf.begin;
    };
    // The token before the caret read again with the candidate glued behind it
    // must end where it did, as the reading the candidate follows.
    const auto glue_reads = [&](std::string_view spelling,
                                std::uint16_t branch) {
      const SymbolId term = branch == 0 ? leafTerm : readings[branch - 1];
      const std::uint16_t read_in =
          branch == 0 ? leafMode : readingMode[branch - 1];
      // A word the repair read as another is no token of its reading: read
      // under the alphabet that admits everything, the candidate only must
      // not make one word with it.
      if (leaf.recovered) {
        const Scan s = spliced(leaf.begin, spelling, T.errMode);
        return s.term == kInvalidSymbol || s.end <= at - leaf.begin;
      }
      const Scan s = spliced(leaf.begin, spelling, read_in);
      return reads_as(s, term, read_in) && s.end == at - leaf.begin &&
             choice_reads(spelling);
    };
    // Glued to the token before the caret, a name can read as one word with
    // it.
    std::unordered_map<std::uint64_t, bool> glued_with;
    const auto glues = [&](std::uint16_t branch, SymbolId label,
                           std::uint16_t mode,
                           std::span<const std::string_view> spellings) {
      if (!glued) return true;
      const auto [slot, fresh] = glued_with.try_emplace(
          std::uint64_t{branch} << 32 | std::uint32_t{label} << 16 | mode,
          false);
      const auto reads = [&](std::string_view s) {
        return glue_reads(s, branch);
      };
      if (fresh)
        slot->second = spellings.empty()
                           ? ll_sample_takes<T>(label, mode, reads)
                           : std::ranges::any_of(spellings, reads);
      return slot->second;
    };
    const auto alphabet_at = [&](std::uint32_t edge) {
      const std::uint16_t hop = T.edgeNode[edge];
      return alphabet(hop != kNoRow ? T.nodes[hop].mode : T.errMode);
    };
    const auto offers = [&](const LlAtnEdge& e, std::uint32_t edge) {
      if (e.kind != LlEdgeKind::atom || e.label == kEofSymbol ||
          ll_chains_nothing<T>(cst, stamp, spine, leaf, T.edgeNode[edge]))
        return false;
      const std::uint16_t mode = alphabet_at(edge);
      const auto spellings = grammar.terminalSpellings(e.label);
      return fits(e.label, mode, spellings) &&
             condition_allows(e.label, mode, spellings);
    };
    const auto follows = [&](const LlConfig& c, const LlAtnEdge& e,
                             std::uint32_t edge) {
      return glues(c.alt, e.label, alphabet_at(edge),
                   grammar.terminalSpellings(e.label));
    };
    const auto& frontier = T.ngapConds != 0 ? candidates : sim.configs();
    everyone.reset();
    spliced_in.clear();
    for (const LlConfig& c : frontier)
      for (const LlAtnEdge& e : T.atn.outgoing(c.state))
        if (e.kind == LlEdgeKind::atom && e.label != kEofSymbol)
          everyone.set(e.label);
    for (const LlConfig& c : frontier) {
      const LlAtnState& q = T.atn.state(c.state);
      for (std::uint32_t i = 0; i < q.nedges; ++i) {
        const LlAtnEdge& e = T.atn.edge(q.edge + i);
        if (!offers(e, q.edge + i)) continue;
        if constexpr (T.ngapConds == 0)
          if (!follows(c, e, q.edge + i)) continue;
        auto& k = slot_of[e.label];
        if (k == kNoSlot) {
          k = static_cast<std::uint16_t>(seen.size());
          seen.push_back(e.label);
          agreed.emplace_back();
        }
        if constexpr (T.ngapConds == 0) fold(c, q.edge + i, agreed[k]);
      }
    }
    // Under conditions the member is what the readings they leave agree on:
    // the closure had joined those with the readings they refuse in one stack.
    if constexpr (T.ngapConds != 0)
      for (std::size_t k = 0; k < seen.size(); ++k)
        for (const LlConfig& c : survivors[seen[k]]) {
          const LlAtnState& q = T.atn.state(c.state);
          for (std::uint32_t i = 0; i < q.nedges; ++i) {
            const LlAtnEdge& e = T.atn.edge(q.edge + i);
            if (e.label == seen[k] && offers(e, q.edge + i) &&
                follows(c, e, q.edge + i))
              fold(c, q.edge + i, agreed[k]);
          }
        }
    if (widens &&
        (beyond || std::any_of(agreed.begin(), agreed.end(),
                               [](const LlAgree& a) { return a.floor; })))
      continue;
    for (std::size_t k = 0; k < seen.size(); ++k) {
      // A term no surviving reading follows is no candidate.
      if (!agreed[k].seen) continue;
      const bool named = !agreed[k].split;
      const ElementId site = named ? agreed[k].site : kNoElement;
      out.push_back(
          NextFeature{seen[k], named ? agreed[k].member : std::string_view{},
                      grammar.rowNarrow(site), grammar.rowType(site)});
    }
    return;
  }
}

}  // namespace cursiv
