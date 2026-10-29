// What of the graph reads the grammar: a vertex's actions, a reduction's goto,
// the replay of the elected derivation on the context's own stack, and the
// repair searched from the readings that froze. Recognition writes no CST.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrExit.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/lr/runtime/LrGssGraph.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/lr/runtime/lr_elect.hpp"
#include "cursiv/engines/lr/runtime/lr_lex_exact.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_read.hpp"
#include "cursiv/engines/lr/runtime/lr_refusal.hpp"
#include "cursiv/engines/lr/runtime/lr_repair_write.hpp"
#include "cursiv/engines/lr/runtime/lr_search.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/lr/table/LrActionKind.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Stacks under one frozen reading a search sets out from, at most.
inline constexpr std::size_t kLrGssStacks = 512;
// The stack frames an episode's graph first holds.
inline constexpr std::uint32_t kLrSeedWindow = 32;

// Whether one stack below `vertex` takes `term` after its plain reductions,
// as `lr_graph_takes` walks the graph.
template <const auto& A, class Holds>
bool lr_gss_takes(LrGssGraph& graph, LrGssGraph::Id vertex, SymbolId term,
                  Holds&& holds) {
  using Id = LrGssGraph::Id;
  return lr_graph_takes(
      vertex, [&](Id v) { return graph.reading(v).run.state; },
      [&](Id v, auto&& f) { graph.each_below(v, f); },
      [&](std::uint32_t top, std::uint32_t& prod, std::uint32_t& len) {
        const int act = lr_climb_act<A>(top, term, holds, prod);
        if (act >= 0) return act;
        if (!lr_reduce_allowed<A>(prod, holds)) return 0;
        if (kLrGuarded<A> && A.cfg.prods[prod].guarded != 0) return 1;
        len = A.cfg.prods[prod].len;
        return -1;
      },
      [&](std::uint32_t from, std::uint32_t prod) {
        return lr_reduce_target<A>(from, prod, kLrNoFirst, holds);
      });
}

template <const auto& A>
class LrGss {
 public:
  // Reads what the look-ahead left open, from the context as it stands, and
  // writes on it the derivation it elects -- or, where every reading froze, the
  // repair a search elects from them.
  CURSIV_NOINLINE static LrExit read(LrContext& c, LrGssGraph& g) {
    c.stop.check();
    LrGss graph(c, g);
    ++c.stats.splits;
    const Ending end = graph.episode();
    c.stats.tableElections += g.tabled();
    c.stats.alternatives = std::max(
        c.stats.alternatives, static_cast<std::uint32_t>(g.alternatives()));
    switch (end) {
      case Ending::accepted:
        graph.say_bounded();
        return LrExit::accept;
      case Ending::frozen: {
        const LrExit exit = graph.repair();
        graph.say_bounded();
        return exit;
      }
      case Ending::handed:
        break;
    }
    graph.say_bounded();
    return lr_resume(c);
  }

 private:
  using Id = LrGssGraph::Id;
  using Reading = LrGssGraph::Reading;
  using Step = LrGssGraph::Step;
  using Pop = LrGssGraph::Pop;
  static constexpr Id none = LrGssGraph::none;

  using Shift = LrGssGraph::Shift;

  // Accepting the text, handing it back to the drive, or every reading frozen.
  enum class Ending : std::uint8_t { accepted, handed, frozen };

  LrGss(LrContext& context, LrGssGraph& graph)
      : _c(context), _g(graph), _shifts(graph.shifts()) {}

  Ending episode() {
    Reading start;
    start.run.state = _c.stack.top().state;
    start.at = _c.committed;
    start.virt = _c.virt;
    start.indent = _c.indent;
    // A position several terminals match at is read again from the cursor.
    const bool position = _c.split == LrSplit::position;
    _c.split = LrSplit::none;
    if (position) {
      start.pendEnd = start.at;
    } else {
      start.la = _c.la;
      start.laMode = _c.laMode;
      start.pendEnd = _c.pendEnd;
    }
    // The graph holds the stack's top frames only, as many as its reductions
    // reach: a reduction below them reads the episode again, seeded wider.
    bool collapsed = false;
    for (std::uint32_t window = kLrSeedWindow;;) {
      if constexpr (A.cold.electSym.size() != 0)
        _g.seed(_c.stack, start, A.cold.electSym, window);
      else
        _g.seed(_c.stack, start, {}, window);
      _c.stats.seeded += _c.stack.size() - _g.seed_base();
      collapsed = false;
      TextOffset openedAt = start.at;
      while (!_g.underflow()) {
        _c.stop.poll();
        Pop pop;
        Id last;
        if (_g.next_path(pop, last)) {
          finish(pop, last);
          continue;
        }
        Id v;
        TextOffset at;
        if (!next_vertex(v, at)) break;
        _g.settle(at);
        if (_g.hand_back(v, openedAt)) {
          collapsed = true;
          break;
        }
        _g.take_vertex();
        openedAt = std::max(openedAt, at);
        open(v);
      }
      if (!_g.underflow()) break;
      const std::uint32_t depth = _c.stack.size();
      window = window > depth / 4 ? depth : window * 4;
    }
    if (!collapsed && !_g.close()) return Ending::frozen;
    const auto chosen = _g.elected();
    for (const Id s : chosen) {
      _c.stop.poll();
      replay(_g.step(s));
    }
    _c.stop.check();
    if (!collapsed) lr_flush_trivia<A>(_c);
    return collapsed ? Ending::handed : Ending::accepted;
  }

  // The span of the frozen readings whose stacks a search could not all take,
  // said once the episode has written its path.
  void say_bounded() {
    if (_g.cut() == 0) return;
    lr_search_settle(_c, *_c.search);
    RepairNote note{};
    note.kind = DiagnosticKind::bounded;
    note.begin = _g.cut_begin();
    note.end = std::max(_g.cut_end(), _c.committed);
    _c.notes.push_back(std::move(note));
  }

  // Every reading froze: one search sets out from those no rival read past, as
  // from an episode's frozen readings, and the one it elects is written. A
  // single one is the flat search's.
  LrExit repair() {
    _c.stop.check();
    ++_c.stats.episodeSearches;
    const auto sources = _g.sources(kLrGssStacks);
    if (sources.empty()) return LrExit::error;
    if (sources.size() == 1) {
      install(sources[0]);
      return LrExit::pause;
    }
    LrSearch& s = *_c.search;
    if (s.degraded) {
      install(sources[0]);
      return lr_search_degraded<A>(_c, s);
    }
    lr_search_reset(s);
    s.paidBase = _c.cost;
    std::vector<LrStack> frames(sources.size());
    for (std::uint32_t k = 0; k < sources.size(); ++k) {
      _c.stop.poll();
      const Reading& r = sources[k].reading;
      const Frozen at{r.at, r.la, r.laMode, r.virt, r.indent};
      const NodeCount len = frames_of(sources[k], frames[k]);
      lr_search_root(s, at, frames[k], len, k, true);
    }
    bool exhausted = false;
    bool raced = false;
    const std::uint32_t elected = lr_search_run<A>(_c, s, exhausted, raced);
    install(sources[elected == kLrNoConfig
                        ? 0
                        : lr_search_root_of(s.links, s.configs[elected].link)]);
    return lr_search_take<A>(_c, s, elected, exhausted, raced, false);
  }

  // What a search reads of a frozen reading: its position and token in hand.
  struct Frozen {
    TextOffset committed;
    Token la;
    std::uint16_t laMode;
    LrVirtualAt virt;
    IndentAt indent;
  };

  // The frames under `s`: the context's own up to where the graph took over,
  // then one per graph frame, its records past the tape. The records' count.
  NodeCount frames_of(const LrGssGraph::Source& s, LrStack& out) {
    const NodeCount tape = _c.tape.len();
    // The frames under the graph, then the seed's that the stack keeps.
    const std::size_t base = _g.seed_base();
    for (std::size_t f = 0; f < base; ++f) out.push(_c.stack[f]);
    std::size_t i = 0;
    while (i < s.vertices.size() && s.vertices[i] == i &&
           base + i < _c.stack.size())
      out.push(_c.stack[base + i++]);
    NodeCount past = 0;
    for (const std::size_t kept = base + i; i < s.vertices.size(); ++i) {
      const Reading& r = _g.reading(s.vertices[i]);
      const NodeCount open = base + i == kept && kept < _c.stack.size()
                                 ? _c.stack[kept].open
                                 : tape + past;
      out.push(LrFrame{static_cast<std::uint16_t>(r.run.state), kNoElement,
                       open, r.at, kLrNoWord});
      past += _g.tokens(s.forests[i - 1]);
    }
    return tape + past;
  }

  // The frozen reading `s` as the context's own: its derivation written, its
  // token in hand.
  void install(const LrGssGraph::Source& s) {
    _c.stop.check();
    for (const Id step : _g.derivation(s.forests)) {
      _c.stop.poll();
      replay(_g.step(step));
    }
    const Reading& r = s.reading;
    lay_pending(r);
    _c.pendEnd = r.pendEnd;
    _c.la = r.la;
    _c.laMode = r.laMode;
    _c.virt = r.virt;
    _c.indent = r.indent;
  }

  void open(Id v) {
    unsigned candidates = 1;
    for (unsigned candidate = 0; candidate < candidates; ++candidate) {
      Reading r = _g.reading(v);
      Step step;
      step.vertex = v;
      // As a reading that owns its levels lexes: an implied token still in
      // hand stands, a line start reads the indentation, and the scan runs
      // under the reading's own bracket depth.
      const auto scan = [&] {
        const std::uint16_t mode = A.mode[r.run.state];
        r.laMode = mode;
        if constexpr (kLrImplied<A>.size() != 0) {
          if (r.virt.pending != 0) {
            r.la = r.virt.in_hand();
            return;
          }
        }
        step.scanned = true;
        if constexpr (kIndented<A.cfg>) {
          if (lr_read_indent<A>(_c, r, mode)) {
            r.pendEnd = r.la.begin;
            return;
          }
        }
        r.la = lr_scan_at<A>(_c, mode, r.at, r.indent.depth);
        [[maybe_unused]] bool refused = false;
        if constexpr (A.has_flags())
          if (r.la.sym < A.cfg.nterms && A.flagged(mode, r.la.sym))
            [[unlikely]]
            refused = refuse(v, r, mode);
        r.pendEnd = r.la.begin;
        if constexpr (A.maxCands > 1) {
          if (!refused && lr_has_rivals<A>(r.laMode, r.la.sym)) {
            ScanRoom<A.maxCands> room;
            const unsigned keep =
                lr_rivals<A>(_c, r.laMode, r.la.begin, room.data());
            if (keep != 0) {
              candidates = keep;
              const SymbolId term = room[candidate].term;
              r.la = Token{r.la.begin, room[candidate].end, term, r.laMode};
              step.ranked = lr_rivals_ranked(room.data(), keep);
              step.lexRank = ~lex_weight(room[candidate].tier);
            }
          }
        }
      };
      const auto actions = lr_read_actions<A>(_c, [&]() -> Reading& { return r; },
                                              scan, [] {});
      step.reading = r;
      if (actions.count == 0) {
        if (actions.lone == kLrNoProd) {
          _g.freeze(v, r);
          continue;
        }
        step.lone = actions.lone;
        reduce(v, std::move(step), actions.lone);
        continue;
      }
      bool lives = false;
      for (unsigned arm = 0; arm < actions.count; ++arm) {
        Step take = step;
        take.action = actions.first + arm;
        take.exact = lr_order_exact<A>(take.action);
        const auto action = lr_shadow_action<A>(take.action);
        if (action.kind == LrActionKind::reduce) {
          if (!lr_reduce_allowed<A>(action.arg, lr_shadow_holds(_c, r))) continue;
          lives = reduce(v, std::move(take), action.arg) || lives;
          continue;
        }
        if (action.kind == LrActionKind::accept) {
          _g.accept_at(v, _g.add(std::move(take)));
          lives = true;
          continue;
        }
        const std::uint32_t to =
            lr_shift_target<A>(action.kind, action.arg, lr_shadow_holds(_c, r));
        if (to == kInvalidSymbol) continue;
        lives = true;
        take.shifts = true;
        take.laid = r.la.sym < A.cfg.nterms && A.cfg.terms[r.la.sym].laid != 0;
        const bool real = !take.laid && r.la.begin < r.la.end;
        const Id s = _g.add(std::move(take));
        Reading next = r;
        next.run.state = to;
        // An implied token stands where the last one ended: the trivia past
        // it is read again behind it, as the drive reads it.
        const bool implied = kLrImplied<A>.size() != 0 && r.virt.implies(r.la);
        next.at = implied ? r.at : r.la.end;
        next.laMode = kNoMode;
        next.la = {};
        next.pendEnd = next.at;
        if constexpr (kIndented<A.cfg>)
          indent_shift<A.cfg>(_c.indentArena, next.indent, r.la);
        if constexpr (kLrConditioned<A>) next.virt.shift(r.la);
        const Id landed = _g.vertex(next);
        lay(landed, v, to, r.la.sym, {}, s, {}, {});
        if (real) _shifts.push_back({landed, r.la});
      }
      if (!lives) _g.freeze(v, r);
    }
  }

  // The token of `r`, a flagged winner of `mode`, read again while no stack
  // under `v` takes it.
  CURSIV_NOINLINE bool refuse(Id v, Reading& r, std::uint16_t mode) {
    return lr_lex_refused<A>(
        [&](const Token& t) { return takes(v, t.sym, r); }, mode, r.la,
        [&](const LrSkipped<A>& skip, TextOffset from) {
          return lr_scan_skipping<A>(_c, mode, from, r.indent.depth, skip,
                                     [](TextOffset, const Token*) {});
        });
  }

  // Whether a stack under `v` takes `term` after the reductions the table lays
  // for it, the graph's paths popped as a reduction pops them. What the gap
  // decides counts as taken.
  bool takes(Id v, SymbolId term, const Reading& r) {
    return lr_gss_takes<A>(_g, v, term, lr_shadow_holds(_c, r));
  }

  // The nearest unopened vertex, once the shifts from every place before it
  // are settled: of the tokens shifted from one place, only the one the lexer
  // elects stands.
  bool next_vertex(Id& v, TextOffset& at) {
    while (_g.next_vertex(v, at)) {
      if (_shifts.empty() || !elected_stand(at)) return true;
    }
    return false;
  }

  // Settles the shifts that start before `before`; whether a vertex was
  // retired.
  bool elected_stand(TextOffset before) {
    const Token lead = _shifts.front().token;
    if (std::ranges::all_of(_shifts,
                            [&](const Shift& s) { return s.token == lead; })) {
      if (lead.begin < before) _shifts.clear();
      return false;
    }
    // A tie reads its tokens again: each once, not once per rival.
    const auto weight = [&](Shift& s) {
      if (s.weight == Shift::kUnweighed)
        s.weight = static_cast<std::uint8_t>(lr_token_weight<A>(_c, s.token));
      return s.weight;
    };
    for (Shift& s : _shifts)
      s.beaten = std::ranges::any_of(_shifts, [&](Shift& o) {
        return lr_token_beats(o.token, s.token,
                              [&] { return weight(o) > weight(s); });
      });
    std::vector<Id> rejected;
    for (const Shift& s : _shifts) {
      if (s.token.begin >= before || !s.beaten) continue;
      if (std::ranges::none_of(_shifts, [&](const Shift& o) {
            return o.vertex == s.vertex && !o.beaten;
          }))
        rejected.push_back(s.vertex);
    }
    _g.retire(rejected);
    std::erase_if(_shifts,
                  [&](const Shift& s) { return s.token.begin < before; });
    return !rejected.empty();
  }

  // False where an empty reduction lands nowhere: the reading has no action.
  bool reduce(Id v, Step step, std::uint16_t prod) {
    const Pop pop{v, _g.add(std::move(step)), none, none, prod,
                  A.cfg.prods[prod].len};
    if (pop.left != 0) {
      _g.watch(pop);
      return true;
    }
    return finish(pop, none);
  }

  bool finish(const Pop& pop, Id last) {
    const auto& p = A.cfg.prods[pop.prod];
    const Reading r = _g.step(pop.step).reading;
    const Id v = last == none ? pop.vertex : _g.below(last);
    const auto first =
        last == none ? kLrNoFirst : _g.reading(pop.vertex).run.state;
    const auto to = lr_reduce_target<A>(
        _g.reading(v).run.state, pop.prod, first, lr_shadow_holds(_c, r));
    if (to == kInvalidSymbol) {
      _g.die(r.la.begin);
      return false;
    }
    Reading next = r;
    next.run.state = to;
    if (A.derived.reductions[pop.prod].flushes)
      next.at = std::max(next.at, next.pendEnd);
    const Id goes = _g.vertex(next);
    const auto children = _g.children(pop, last);
    if constexpr (A.cold.electWord.size() != 0) {
      const auto [words, pos] = lr_elect_words<A>(
          pop.prod, _g.child_symbols(), _g.word_room(), _g.word_at_room());
      lay(goes, v, to, A.cfg.nterms + p.lhs, children, pop.step, words, pos);
    } else {
      _g.edge(goes, v, A.cfg.nterms + p.lhs, children, pop.step);
    }
    return true;
  }

  // The edge into `goes`, in state `to`: where the table skipped pass-throughs
  // into it, the node of `symbol` stands under the one `to` stood after.
  void lay(Id goes, Id from, std::uint32_t to, std::uint32_t symbol,
           std::span<const Id> children, Id step,
           std::span<const std::uint32_t> words,
           std::span<const std::uint16_t> pos) {
    if constexpr (A.cold.electSym.size() != 0) {
      const std::uint32_t run = lr_elect_stands<A>(to, symbol);
      if (run != kLrNoRun) {
        const std::uint32_t b = A.cold.electChainAt[run];
        const std::uint32_t n = A.cold.electChainAt[run + 1u] - b;
        _g.edge(goes, from, symbol, children, step, words, pos,
                A.cold.electSym[to],
                std::span(A.cold.electChainWord).subspan(b, n),
                std::span(A.cold.electChainPos).subspan(b, n));
        return;
      }
    }
    _g.edge(goes, from, symbol, children, step, words, pos);
  }

  void replay(const Step& step) {
    const Reading& r = step.reading;
    if (_c.stack.top().state != r.run.state || _c.committed != r.at)
      throw std::logic_error("cursiv: GSS replay left the elected derivation");
    if (step.scanned) lay_pending(r);
    _c.pendEnd = r.pendEnd;
    _c.la = r.la;
    _c.laMode = r.laMode;
    _c.virt = r.virt;
    if (step.lone != kLrNoProd) {
      if (!lr_write_reduce<A>(_c, step.lone, lr_shadow_holds(_c, r)))
        throw std::logic_error("cursiv: GSS replay refused reduction");
      return;
    }
    LrWriteReading w = lr_write_reading(_c);
    if (!lr_write_apply<A>(_c, w, step.action))
      throw std::logic_error("cursiv: GSS replay refused action");
    _c.virt = w.virt;
    _c.indent = w.indent;
    _c.laMode = w.laMode;
  }

  // The trivia from the cursor to `r`'s token, pending as the drive leaves it.
  // `Apart`: the token's stack refused a flagged terminal that beats trivia on
  // the way, which the walk skips as it meets it.
  template <bool Apart = false>
  void lay_pending(const Reading& r) {
    _c.pending.clear();
    TextOffset at = _c.committed;
    const auto mode = A.mode[r.run.state];
    [[maybe_unused]] LrSkipped<A> skip{};
    [[maybe_unused]] const typename LrLexTape<A>::Skipping tape{.skip = &skip};
    while (at < r.la.begin) {
      if (lr_skips<A>(mode, static_cast<unsigned char>(_c.src[at]))) {
        ++at;
        continue;
      }
      Scan t;
      if constexpr (Apart)
        t = lex_match(tape, _c.src, _c.len, mode, at);
      else
        t = _c.drive->walk(_c.src, _c.len, mode, at);
      const bool trivia =
          t.end > at && (lr_is_trivia<A>(t.term) ||
                         lr_joins<A>(r.indent.depth, t.term));
      if (!trivia) {
        if constexpr (Apart) {
          if (t.end > at && A.flagged(mode, t.term)) {
            skip.set(t.term);
            continue;
          }
        } else if constexpr (A.has_flags()) {
          return lay_pending_apart(r);
        }
        throw std::logic_error("cursiv: GSS replay trivia mismatch");
      }
      if (A.cfg.terms[t.term].recorded)
        _c.pending.push_back(Token{at, t.end, t.term});
      at = t.end;
    }
  }

  CURSIV_NOINLINE void lay_pending_apart(const Reading& r) {
    lay_pending<true>(r);
  }

  LrContext& _c;
  LrGssGraph& _g;
  std::vector<Shift>& _shifts;
};

}  // namespace cursiv
