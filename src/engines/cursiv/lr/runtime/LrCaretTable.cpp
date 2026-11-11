// The caret's own reading of the tape: the answer named in LrCaretTable.hpp,
// compiled once for every language.

#include "cursiv/lr/runtime/LrCaretTable.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/runtime/LrGssIndex.hpp"
#include "cursiv/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/lr/runtime/lr_lookahead.hpp"
#include "cursiv/lr/runtime/lr_refusal.hpp"
#include "cursiv/lr/table/LrProduction.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/LexByteRun.hpp"
#include "cursiv/shared/runtime/token_end_from.hpp"
#include "cursiv/shared/table/CondReach.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/utils/white_space.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

namespace {

// A repair can root an invented record outside the entry subtree, so walk
// the full leaf order. `behind` follows the answer; `skipped` marks given-up
// text.
CstLeaf read_leaf_before(const Cst& cst, StampLayout stamp, NodeId& behind,
                         bool* skipped) {
  while (behind > 0) {
    const CstLeaf leaf = cst.as_leaf(behind = cst.leaf_before(behind));
    const bool gaveup = leaf.recovered && stamp.state(cst[leaf.id]) == 0;
    if (gaveup && skipped != nullptr) *skipped = true;
    if (leaf.hidden) continue;
    // A repair's leaf answers nothing, with one exception: the skip carries no
    // state and the invented token has no width, but a word read as the one it
    // misspells was shifted for real and carries where that shift went.
    if (leaf.begin == leaf.end || gaveup) continue;
    return leaf;
  }
  return {};
}

// What a reading added on its way: two readings that stand in different
// blocks or need different implied tokens are not one node.
struct CaretContext {
  // The implied tokens inserted where the candidate stands.
  decltype(LrVirtualAt::inserted) implied = 0;
  IndentAt indent;
  // The reading of the leaf attached to the caret it descends from.
  SymbolId reading = 0;
  // The mode that read that leaf on the way here: the same terminal read in
  // another mode may take in what is typed behind it.
  std::uint16_t mode = kNoMode;
  // The issues it took of conditions left to the candidates: a link of the
  // replay's chains, 0 for none.
  std::uint32_t assumes = 0;
  // The reductions taken before that mode shifted the leaf, whose states read
  // it first in their own modes: a link of the replay's pass chains, 0 for
  // none.
  std::uint32_t passes = 0;
  bool operator==(const CaretContext&) const = default;
};

// The parser's stacks rebuilt over the tape with the final tables: nodes at one
// position share state, offset and lookahead, keeping every compatible caller.
// Each closure saturates, so no budget bounds it.
class CaretStacks {
  using Id = std::size_t;
  static constexpr Id kNoParent = static_cast<Id>(-1);
  static constexpr TextOffset kNoEdge = ~TextOffset{0};
  static constexpr std::uint32_t kContradicted = ~std::uint32_t{0};
  // The predecessors past the first, in the pool: a block is relocated when it
  // fills, so a frame's own storage doubles.
  struct Block {
    std::uint32_t at;
    std::uint32_t size;
    std::uint32_t cap;
  };
  // What a candidate's closure adds and the replay takes back.
  struct Mark {
    std::size_t frames;
    std::size_t blocks;
    std::size_t pool;
  };
  using Context = CaretContext;
  struct Frame {
    std::uint16_t state;
    std::uint16_t laMode;
    TextOffset at;
    NodeCount rank;
    Token la;
    Context context{};
    // Predecessors in the order they came, the first inline and the others in
    // a block of the replay's pool.
    Id parent = kNoParent;
    Id more = kNoParent;
  };
  // One cache line: the replay creates and compares frames by the thousand.
  static_assert(sizeof(Frame) <= 64, "a caret frame fits one cache line");
  // Only nodes created by this stage are indexed; its initial heads remain
  // distinct. Hashes select candidates, whose frame fields compare exactly.
  struct Stage {
    std::size_t rank = 0;
    std::vector<Id> active;
    LrGssIndex index;
    std::size_t seeds = 0;
    // The predecessors past the first of each node it created.
    FlatMap<std::uint8_t> edges;
    bool anyEdge = false;
    void reset(std::size_t r, std::span<const Id> heads = {}) {
      rank = r;
      active.assign(heads.begin(), heads.end());
      index.clear();
      seeds = heads.size();
      if (std::exchange(anyEdge, false)) edges.reset();
    }
    std::span<const Id> nodes() const {
      return std::span(active).subspan(seeds);
    }
  };

 public:
  // Past `edge` stands the text each candidate replaces.
  CaretStacks(const LrCaretTable& table, std::string_view text,
              TextOffset edge = kNoEdge)
      : _table(table), _text(text), _edge(edge), _room(table.maxCands) {
    for (std::size_t i = 1; i < table.terms.size(); ++i) {
      const auto t = static_cast<SymbolId>(i);
      const auto kind = table.terms[t].synthetic;
      if (kind == kSyntheticIndent) _indent = t;
      if (kind == kSyntheticDedent) _dedent = t;
      if (kind == kSyntheticImplied) _implied.push_back(t);
      if (table.terms[t].delim != 0) _bracketed = true;
    }
    for (const CondReach& r : table.condReach) {
      _widest.look = std::max(_widest.look, r.look);
      _widest.tokens = std::max(_widest.tokens, r.tokens);
    }
    _frames.push_back(
        Frame{.state = 0, .laMode = kDeadState, .at = 0, .rank = 0, .la = {}});
    _heads.push_back(0);
  }

  // A trivia token, by where it starts.
  struct Trivia {
    TextOffset begin = 0;
    Scan scan;
  };

  // The trivia token that ends at `to`, which a word typed there may go on from,
  // when the tokens from `from` run to it.
  bool comment_before(std::uint16_t mode, TextOffset from, TextOffset to,
                      Trivia& comment) {
    Trivia last;
    if (!trivia_scan(mode, from, to, false, &last,
                     [](const Token&) { return false; }) ||
        last.scan.end != to || last.scan.end <= last.begin)
      return false;
    comment = last;
    return true;
  }

  // `virt` names the token where the indentation answered the line start
  // before any lexer read it.
  bool consume(const CstLeaf& leaf, SymbolId virt) {
    forget_scans();
    _next.reset(_rank + 1);
    _leafReads.clear();
    const bool edge = leaf.end == _edge;
    // A token of the text that ends at the edge is read first in the modes of
    // the states that reduce before it is shifted.
    _passing = edge && leaf.begin != leaf.end;
    if (_passing) {
      _edgeHeads = _heads;
      _edgeGapFrom = _gapFrom;
    }
    _tape = &leaf;
    close(
        [&](Id head, const Token& held) -> std::span<const Token> {
          const Frame& frame = _frames[head];
          // An indent stands behind the form feeds of its line's blanks.
          if (virt != kInvalidSymbol &&
              (frame.at == leaf.begin ||
               (virt == _indent && frame.at < leaf.begin &&
                indent_run(_text.data(), frame.at, leaf.begin).stop ==
                    leaf.begin)) &&
              frame.laMode != _table.mode[frame.state]) {
            _virt = Token{leaf.begin, leaf.end, virt};
            return {&_virt, 1};
          }
          return reading(head, held);
        },
        [&](Id from, const LrAction& action, const Token& token,
            std::uint32_t assumes) {
          // A reduction may re-lex in another mode, so only a shift must match
          // the recorded extent. A leaf ending where the candidates stand
          // keeps, per head, its mode and reductions.
          if (token.begin == leaf.begin && token.end == leaf.end) {
            const LeafRead read{_table.mode[_frames[from].state], token.sym};
            if (std::ranges::find(_leafReads, read) == _leafReads.end())
              _leafReads.push_back(read);
            add(_next, action.arg, token.end, from, {}, kDeadState,
                Context{
                    .indent = levels_after(_frames[from].context.indent, token),
                    .mode = edge ? read.mode : kNoMode,
                    .assumes = assumes,
                    .passes = edge ? _frames[from].context.passes : 0});
          }
        });
    _tape = nullptr;
    _passing = false;
    _heads.swap(_next.active);
    ++_rank;
    if (leaf.begin != leaf.end) _gapFrom = leaf.end;
    return !_heads.empty();
  }

  // A complete record stepped over as one frame: the goto its reduction takes,
  // from the one node that shifts its first token. Two such nodes are two
  // readings, and the step is declined.
  bool jump(const Token& first, std::span<const std::uint16_t> lhs,
            TextOffset end, TextOffset gapFrom) {
    forget_scans();
    const Mark saved = mark();
    _next.reset(_rank + 1);
    Id shifter = kNoParent;
    bool several = false;
    close(
        [&](Id head, const Token&) -> std::span<const Token> {
          const Frame& frame = _frames[head];
          if (!trivia_to(head, _table.mode[frame.state], frame.at, first.begin))
            return {};
          return {&first, 1};
        },
        [&](Id from, const LrAction&, const Token&, std::uint32_t) {
          if (shifter != kNoParent && shifter != from) several = true;
          shifter = from;
        });
    // One goto among the record's candidate nonterminals, told apart by which
    // the state under the record can take. A guard goes everywhere it may.
    int target = -1;
    if (shifter != kNoParent && !several) {
      unsigned targets = 0;
      for (const std::uint16_t nt : lhs)
        _table.each_target(
            _table.go(_frames[shifter].state, nt), [](unsigned) { return 1u; },
            [&](std::uint32_t to) {
              target = targets++ == 0 ? static_cast<int>(to) : -1;
            });
    }
    if (target < 0) {
      rollback(saved);
      return false;
    }
    add(_next, static_cast<std::uint16_t>(target), end, shifter, {}, kDeadState,
        Context{.indent = _frames[shifter].context.indent,
                .assumes = _frames[shifter].context.assumes});
    _heads.swap(_next.active);
    ++_rank;
    _gapFrom = gapFrom;
    return true;
  }

  // Text the parser dropped: the stack stands, the position moves past it, and
  // the token is read again there. Where no reading refuses it, the readings
  // stand.
  bool skip(TextOffset begin, TextOffset end) {
    _headsBefore.assign(_heads.begin(), _heads.end());
    if (!errors()) _heads.swap(_headsBefore);
    _next.reset(_rank);
    for (const Id head : _heads) copy(_next, head, end);
    _heads.swap(_next.active);
    if (begin != end) _gapFrom = end;
    return !_heads.empty();
  }

  // Whether the last candidate's closure asked a condition of the text it
  // would be typed in.
  bool asked() const { return _asked; }
  // Whether the conditions the next candidates ask are kept, each with its
  // answer.
  void keep_answers(bool keep) {
    _keeping = keep;
    _answers.clear();
  }
  // Whether those the last candidate asked answer the same were it typed as
  // `word`: its closure is then the same.
  bool unchanged(SymbolId term, TextOffset at, TextOffset to,
                 std::string_view word) {
    condition_text(at, to, word);
    typed_as(term, word);
    const bool known = std::exchange(_known, !word.empty());
    const bool allHold = std::ranges::all_of(_answers, [&](const Answer& a) {
      return spliced(a.gap, a.from, a.token) == a.holds;
    });
    _known = known;
    return allHold;
  }

  std::size_t rank() const { return _rank; }
  // One reading whose states from the start state up are `states`, its top
  // read to `at`: the stacks a replay starting there stands on.
  void seed(std::span<const std::uint16_t> states, TextOffset at,
            TextOffset gapFrom) {
    assert(_frames.size() == 1 && _heads.size() == 1 && _rank == 0);
    for (const std::uint16_t state : states) {
      _frames.push_back(Frame{
          .state = state, .laMode = kDeadState, .at = at, .rank = 0, .la = {}});
      _frames.back().parent = _heads.front();
      _heads.front() = _frames.size() - 1;
    }
    _rank = 1;
    _gapFrom = gapFrom;
  }
  bool indented() const { return _indent != 0 || _dedent != 0 || _bracketed; }
  // Whether the conditions asked up to the end of record `id` read no byte a
  // candidate replaces, so the record keeps what the tape decided.
  bool keeps_tape(const Cst& cst, NodeId id) const {
    const TextOffset end = cst[id].end;
    if (_edge == kNoEdge || _table.cond == nullptr) return true;
    if (end >= _edge || end + std::uint64_t{_widest.look} > _edge) return false;
    if (_widest.tokens == 0) return true;
    return _widest.tokens < kUnboundedTokens &&
           token_end_from(cst, id + 1u, _widest.tokens + 1u, kNoEdge) <= _edge;
  }
  // The text a candidate's conditions read past it ends at or before `end`.
  void read_through(TextOffset end) { _readEnd = end; }
  std::uint16_t phrase_tokens() const { return _widest.tokens; }
  // A token of the record `jump` stepped over still moves the levels and
  // brackets, which a rule may leave open for its caller to close. The head
  // the jump created is fresh and alone, so its levels may change in place.
  void carry(const Token& t) {
    if (!moves(t.sym)) return;
    IndentAt& at = _frames[_heads.front()].context.indent;
    at = levels_after(at, t);
  }
  bool reads_blanks_to(TextOffset at) {
    return std::ranges::any_of(_heads, [&](Id head) {
      const Frame& f = _frames[head];
      return f.at <= at && trivia_to(head, _table.mode[f.state], f.at, at);
    });
  }
  // The modes the last leaf was read in, with the terminal each read it as:
  // those of the states that shifted it, since the reductions between two
  // leaves move the mode.
  struct LeafRead {
    std::uint16_t mode;
    SymbolId term;
    bool operator==(const LeafRead&) const = default;
  };
  std::span<const LeafRead> leaf_reads() const { return _leafReads; }
  // Whether a reading that read the leaf at the edge takes `token`, read where
  // that leaf stands: the stacks before it are asked of the document, as the
  // replay asked them, and a candidate's text begins behind it.
  bool edge_takes(const Token& token) {
    if (_edgeHeads.empty()) return true;
    const bool candidate = std::exchange(_candidate, false);
    const bool asWritten = std::exchange(_asWritten, true);
    const TextOffset gapFrom = std::exchange(_gapFrom, _edgeGapFrom);
    const bool taken = std::ranges::any_of(
        _edgeHeads, [&](Id head) { return takes(head, token); });
    _gapFrom = gapFrom;
    _asWritten = asWritten;
    _candidate = candidate;
    return taken;
  }
  // `la`, a winner of `mode`, read again by `rescan(skip, from)` while the
  // mode flags it and `taken` refuses it, as the drive reads it.
  template <class Taken, class Rescan>
  bool refused(std::uint16_t mode, Token& la, Taken&& taken, Rescan&& rescan) {
    if (_table.flagged == nullptr) return false;
    _skip.clear();
    return lr_refused(
        [&](SymbolId sym) {
          return sym < _table.terms.size() && _table.flagged(mode, sym);
        },
        taken, la, [&](const Token& t) {
          _skip.push_back(t.sym);
          return rescan(std::span<const SymbolId>(_skip), t.begin);
        });
  }
  // The readings that tie with `winner`, which `refused` left: each the stacks
  // take goes to `sink` until it answers false. `weight` is that of the token
  // `rescan` last read.
  template <class Taken, class Rescan, class Sink>
  void refused_ties(std::uint16_t mode, const Token& winner,
                    const unsigned& weight, Taken&& taken, Rescan&& rescan,
                    Sink&& sink) {
    if (_table.modeMulti[mode] != 1 || _table.scan == nullptr ||
        _table.maxCands < 2 || winner.sym >= _table.terms.size())
      return;
    lr_refused_ties(
        winner, _table.terms.size(),
        [&](const Token& t) {
          _skip.push_back(t.sym);
          return rescan(std::span<const SymbolId>(_skip), t.begin);
        },
        [&](const Token&) { return weight; },
        [&](SymbolId sym) { return _table.flagged(mode, sym); }, taken, sink);
  }
  // A reduction the leaf at the edge was read through before its shift: the
  // state that took it, whose mode read the leaf first, and the rule.
  struct Pass {
    std::uint16_t state;
    std::uint16_t prod;
    std::uint32_t rest;
  };
  // Whether `visit` holds of every pass of chain `passes`.
  template <class Visit>
  bool each_pass(std::uint32_t passes, Visit&& visit) const {
    for (; passes != 0; passes = _passes[passes].rest)
      if (!visit(_passes[passes].state, _passes[passes].prod)) return false;
    return true;
  }
  // Each head names the reading of the last leaf it shifted, or the tape's
  // where it shifted none: a candidate attached to that leaf is judged by it.
  void name_readings(SymbolId recorded) {
    for (const Id head : _heads) {
      const SymbolId term = _table.shiftTerm[_frames[head].state];
      _frames[head].context.reading = term != kInvalidSymbol ? term : recorded;
    }
  }

  // Replayed as the repair read it: the table acts on the recorded terminal,
  // reductions first, until a shift reaches the recorded state; conditions
  // are asked of the token that reading asked them of.
  bool repair(SymbolId term, std::uint16_t target, TextOffset begin,
              TextOffset end) {
    if (!errors()) return false;
    _next.reset(_rank + 1);
    const bool inserted = begin == end;
    const Token pinned{begin, end, term};
    IndentAt reached;
    close<true>(
        [&](Id head, const Token& held) -> std::span<const Token> {
          const Frame& frame = _frames[head];
          if (inserted ? held.begin < begin ||
                             (held.begin > begin &&
                              !trivia_to(head, _table.mode[frame.state], begin,
                                         held.begin))
                       : held.begin != begin)
            return {};
          reached =
              inserted && term == _indent && _indent != 0
                  ? IndentAt{indent_invent(_levels, frame.context.indent.head),
                             frame.context.indent.depth}
                  : levels_after(frame.context.indent, pinned);
          return {&pinned, 1};
        },
        [&](Id head, const LrAction& action, const Token&,
            std::uint32_t assumes) {
          if (action.arg == target) {
            const Frame& frame = _frames[head];
            add(_next, target, end, head, inserted ? frame.la : Token{},
                inserted ? frame.laMode : kDeadState,
                Context{.indent = reached, .assumes = assumes});
          }
        });
    _heads.swap(_next.active);
    ++_rank;
    if (begin != end) _gapFrom = end;
    return !_heads.empty();
  }

  bool unwind(std::size_t from, std::uint16_t state, TextOffset end) {
    if (!errors()) return false;
    _next.reset(_rank);
    for (const Id error : _heads) {
      const Token held = _frames[error].la;
      const auto mode = _frames[error].laMode;
      // The repair keeps the reading's levels, whatever frames it gives up,
      // and the issues that reading took.
      const IndentAt indent = _frames[error].context.indent;
      const auto assumes = _frames[error].context.assumes;
      next_walk();
      _seen.resize(_frames.size());
      // An unwind abandons at least one frame; the current head itself
      // cannot be the retained ancestor.
      _todo.clear();
      each_parent(_frames[error], [&](Id parent) { _todo.push_back(parent); });
      // Every ancestor the repair may have retained: two of one rank and
      // state are two readings, and both stand. Ranks only fall toward the
      // root, so the walk stops below the region.
      while (!_todo.empty()) {
        const Id head = _todo.back();
        _todo.pop_back();
        if (_seen[head] == _epoch || _frames[head].rank < from) continue;
        _seen[head] = _epoch;
        // copy() may grow _frames; the parent walk still needs this frame.
        const Frame frame = _frames[head];
        if (frame.rank == from && frame.state == state)
          copy(_next, head, end, held.sym == kInvalidSymbol ? Token{} : held,
               held.sym == kInvalidSymbol ? kDeadState : mode, indent, assumes);
        each_parent(frame, [&](Id parent) { _todo.push_back(parent); });
      }
    }
    _heads.swap(_next.active);
    return !_heads.empty();
  }

  // What the indentation hands over before any candidate on the caret's
  // line, the dedents or the indent the drive reads at its start: the heads
  // are the readings that then read the candidate itself.
  void indentation(TextOffset at, TextOffset held) {
    if (_indent == 0 && _dedent == 0) return;
    _ready.clear();
    close_shared(
        [&](Id head, const Token& token) -> std::span<const Token> {
          if (_frames[head].laMode != kDeadState && token.sym != 0 &&
              (token.sym == _indent || token.sym == _dedent))
            return {&token, 1};
          if (!level_at(head, at, held, _virt)) {
            _ready.push_back(head);
            return {};
          }
          return {&_virt, 1};
        },
        [&](Id from, const LrAction& action, const Token& token,
            std::uint32_t assumes) {
          Context context = _frames[from].context;
          context.indent = levels_after(context.indent, token);
          context.assumes = assumes;
          add(_closure, action.arg, token.end, from, {}, kDeadState, context);
        });
    std::sort(_ready.begin(), _ready.end());
    _ready.erase(std::unique(_ready.begin(), _ready.end()), _ready.end());
    _heads.assign(_ready.begin(), _ready.end());
  }

  // Every terminal some head or the implied tokens it may be handed lead to:
  // the candidates `next` then judges one by one.
  std::vector<SymbolId> terminals(TextOffset at) {
    std::vector<std::uint64_t> reach;
    const auto collect = [&](Id id) {
      const auto follow = _table.follow(_frames[id].state);
      if (reach.size() < follow.size()) reach.resize(follow.size(), 0);
      for (unsigned k = 0; k < follow.size(); ++k) reach[k] |= follow[k];
    };
    for (const Id head : _heads) collect(head);
    if (!_implied.empty()) {
      const Mark saved = mark();
      _impliedTokens.clear();
      for (const auto sym : _implied)
        _impliedTokens.push_back(Token{at, at, sym});
      _loose = true;
      close_shared(
          [&](Id head, const Token& held) -> std::span<const Token> {
            if (_frames[head].laMode != kDeadState && implied_of(held.sym) >= 0)
              return {&held, 1};
            return _impliedTokens;
          },
          [&](Id from, const LrAction& action, const Token&, std::uint32_t) {
            add(_closure, action.arg, _frames[from].at, from);
          });
      _loose = false;
      for (const Id id : _closure.nodes()) collect(id);
      rollback(saved);
    }
    std::vector<SymbolId> terms;
    for (unsigned k = 0; k < reach.size(); ++k)
      for (auto bits = reach[k]; bits != 0; bits &= bits - 1) {
        const auto term =
            static_cast<SymbolId>(k * 64u + std::countr_zero(bits));
        if (term != 0 && !_table.terms[term].trivia && !_table.synthetic(term))
          terms.push_back(term);
      }
    return terms;
  }

  // The scans a candidate reads are the text's, whatever the candidate: they
  // are kept from one to the next. The conditions read `word`: the spelling,
  // or for a terminal with no spelling the sample word it is typed as.
  template <class KeepsLeaf, class Allows, class Emit>
  void next(SymbolId term, TextOffset at, TextOffset to,
            std::string_view spelling, std::string_view word,
            KeepsLeaf&& keeps_leaf, Allows&& allows, Emit&& emit) {
    const Mark saved = mark();
    // A typed candidate has its extent: a refusal reads the whole of it.
    const Token token{at, static_cast<TextOffset>(at + word.size()), term};
    _candidate = true;
    _asked = false;
    _outlasted = false;
    _shifted.clear();
    _answers.clear();
    _allowed.clear();
    _known = !word.empty();
    if (_table.cond != nullptr) {
      condition_text(at, to, word);
      typed_as(term, word);
      judge(at);
    }
    close_shared(
        [&](Id head, const Token& held) -> std::span<const Token> {
          const Frame& frame = _frames[head];
          if (!_dead.empty() && _dead[frame.context.assumes] != 0) return {};
          if (frame.laMode != kDeadState && held.begin == at &&
              held.end == at && implied_of(held.sym) >= 0)
            return {&held, 1};
          const auto mode = _table.mode[frame.state];
          if (_table.cond != nullptr) typed_in(mode);
          if (!keeps_leaf(frame.context.reading, frame.context.mode,
                          frame.context.passes))
            return {};
          // What this mode reads in the gap is a token to it, on which the
          // drive reduces: the state it goes to may read the gap as trivia, and
          // go on to the candidate. A shift of that token leaves the candidate
          // behind.
          if (!trivia_to(head, mode, frame.at, at)) {
            const auto gap = reading(head, held);
            return !gap.empty() && gap.front().begin < at &&
                           gap.front().end <= at
                       ? gap
                       : std::span<const Token>{};
          }
          // What the mode reads where the candidate is typed: the drive
          // holds it, and may insert an implied token before it if refused.
          auto kept = std::ranges::find(_allowed, mode, &Allowed::mode);
          if (kept == _allowed.end()) {
            _allowed.emplace_back(mode,
                                  allows(mode, std::span<const SymbolId>{}));
            kept = _allowed.end() - 1;
          }
          Scan read = kept->scan;
          if (Token la{at, at + read.end, read.term};
              read.end != 0 &&
              refused(
                  mode, la, [&](const Token& t) { return takes(head, t); },
                  [&](std::span<const SymbolId> skip, TextOffset) {
                    const Scan s = allows(mode, skip);
                    return Token{at, at + s.end, s.term};
                  }))
            read = Scan{la.end - at, la.sym};
          const bool typed = read.term == term && read.end == spelling.size();
          const Token hand =
              typed ? token
              : read.term == kInvalidSymbol || read.end == 0
                  ? Token{at, at, kInvalidSymbol}
                  : Token{at, at + read.end, read.term};
          const int q = implied_before(frame, hand);
          if (q >= 0) {
            _virt = Token{at, at, _implied[q]};
            return {&_virt, 1};
          }
          if (typed) return {&token, 1};
          if (hand.sym == kInvalidSymbol || hand.end <= token.end) return {};
          _longerToken = hand;
          return {&_longerToken, 1};
        },
        [&](Id from, const LrAction& action, const Token& read, std::uint32_t) {
          if (read.begin < at) return;
          if (read.sym == term)
            _shifted.emplace_back(_frames[from].state, action.arg);
          else if (implied_of(read.sym) >= 0)
            add_implied(from, action.arg, implied_of(read.sym));
          else
            _outlasted = true;
        });
    if (!_outlasted)
      for (const auto& [state, toState] : _shifted) emit(state, toState);
    // Candidate closures never alter the prefix graph or one another.
    rollback(saved);
    _candidate = false;
  }

 private:
  CURSIV_ALWAYS_INLINE std::pair<Id, bool> node(Stage& stage,
                                                std::uint16_t state,
                                                TextOffset at, Token la,
                                                std::uint16_t laMode,
                                                Context context) {
    std::size_t hash = state;
    const auto mix = [&](std::size_t value) {
      hash ^= value + std::size_t{0x9e3779b9u} + (hash << 6) + (hash >> 2);
    };
    mix(at);
    mix(laMode);
    mix(la.begin);
    mix(la.end);
    mix(la.sym);  // Token equality deliberately ignores its scanner mode.
    mix(context.implied);
    mix(context.indent.head);
    mix(context.indent.depth);
    mix(context.reading);
    mix(context.mode);
    mix(context.assumes);
    mix(context.passes);
    const Id id = _frames.size();
    const Id found = stage.index.intern(
        hash, static_cast<LrGssIndex::Id>(id), [&](LrGssIndex::Id seen) {
          const Frame& f = _frames[seen];
          return f.state == state && f.at == at && f.laMode == laMode &&
                 f.la == la && f.context == context;
        });
    if (found != id) return {found, false};
    _frames.push_back(Frame{.state = state,
                            .laMode = laMode,
                            .at = at,
                            .rank = static_cast<NodeCount>(stage.rank),
                            .la = la,
                            .context = context});
    stage.active.push_back(id);
    return {id, true};
  }

  Mark mark() const { return {_frames.size(), _blocks.size(), _pool.size()}; }
  void rollback(const Mark& m) {
    _frames.resize(m.frames);
    _blocks.resize(m.blocks);
    _pool.resize(m.pool);
  }

  template <class Visit>
  void each_parent(const Frame& frame, Visit&& visit) const {
    if (frame.parent == kNoParent) return;
    visit(frame.parent);
    if (frame.more == kNoParent) return;
    const Block b = _blocks[frame.more];
    for (std::uint32_t i = 0; i < b.size; ++i) visit(_pool[b.at + i]);
  }

  // One predecessor into the set of a node `stage` created: the first inline,
  // the rest in its block, each once. Whether it was missing.
  bool add_parent(Stage& stage, Id id, Id parent) {
    if (parent == kNoParent) return false;
    Frame& frame = _frames[id];
    if (frame.parent == kNoParent) {
      frame.parent = parent;
      return true;
    }
    if (parent == frame.parent) return false;
    stage.anyEdge = true;
    if (!stage.edges.emplace(std::uint64_t{id} << 32 | parent).second)
      return false;
    if (frame.more == kNoParent) {
      frame.more = _blocks.size();
      _blocks.push_back(Block{static_cast<std::uint32_t>(_pool.size()), 0, 0});
    }
    Block& b = _blocks[frame.more];
    if (b.size == b.cap) {
      const std::uint32_t cap = b.cap == 0 ? 4 : b.cap * 2;
      const auto at = static_cast<std::uint32_t>(_pool.size());
      _pool.resize(_pool.size() + cap);
      std::copy(_pool.begin() + b.at, _pool.begin() + b.at + b.size,
                _pool.begin() + at);
      b.at = at;
      b.cap = cap;
    }
    _pool[b.at + b.size++] = parent;
    return true;
  }

  // Union of predecessor sets: the destination keeps what it has, only a
  // missing predecessor is added. `more` names the source's block.
  bool merge_parents(Stage& stage, Id id, Id parent, Id more = kNoParent) {
    bool changed = add_parent(stage, id, parent);
    if (more != kNoParent)
      for (std::uint32_t i = 0; i < _blocks[more].size; ++i)
        changed = add_parent(stage, id, _pool[_blocks[more].at + i]) || changed;
    return changed;
  }

  void copy(Stage& stage, Id from, TextOffset at, Token la = {},
            std::uint16_t laMode = kDeadState) {
    copy(stage, from, at, la, laMode, _frames[from].context.indent,
         _frames[from].context.assumes);
  }
  void copy(Stage& stage, Id from, TextOffset at, Token la,
            std::uint16_t laMode, IndentAt indent, std::uint32_t assumes) {
    const Frame old = _frames[from];
    const Id to = node(stage, old.state, at, la, laMode,
                       {.indent = indent, .assumes = assumes})
                      .first;
    merge_parents(stage, to, old.parent, old.more);
  }

  // The heads that refuse what they read, as the drive met the fault: an
  // implied token it inserted first is in hand, and one it shifted moved on.
  // The fault stands where the tape met it, on the text as it was.
  bool errors() {
    forget_scans();
    _next.reset(_rank);
    _asWritten = true;
    close_shared(
        [&](Id head, const Token& held) -> std::span<const Token> {
          const Frame& frame = _frames[head];
          if (frame.laMode != kDeadState && implied_of(held.sym) >= 0)
            return {&held, 1};
          const auto read = reading(head, held);
          if (read.size() != 1) return read;
          const int q = implied_before(frame, read.front());
          if (q < 0) return read;
          _virt = Token{read.front().begin, read.front().begin, _implied[q]};
          return {&_virt, 1};
        },
        [&](Id from, const LrAction& action, const Token& token,
            std::uint32_t) {
          const int q = implied_of(token.sym);
          if (q >= 0) add_implied(from, action.arg, q);
        },
        [&](Id head, const Token& token) {
          const auto state = _frames[head].state;
          copy(_next, head, _frames[head].at, token, _table.mode[state]);
        });
    _asWritten = false;
    // A reading refused before another is dropped by the drive, not
    // repaired: the fault is where the last reading standing was refused.
    TextOffset last = 0;
    for (const Id head : _next.active)
      last = std::max(last, _frames[head].la.begin);
    std::erase_if(_next.active,
                  [&](Id head) { return _frames[head].la.begin != last; });
    _heads.swap(_next.active);
    return !_heads.empty();
  }

  CURSIV_ALWAYS_INLINE void add(Stage& stage, std::uint16_t state,
                                TextOffset at, Id parent, Token la = {},
                                std::uint16_t laMode = kDeadState,
                                Context context = {}) {
    const auto [to, fresh] = node(stage, state, at, la, laMode, context);
    if (!merge_parents(stage, to, parent)) return;
    // Fresh nodes are appended to active and read by the closure in due time.
    // Only a new edge of an old node can change reductions already read.
    if (_closing && &stage == &_closure) [[likely]] {
      if (parent != kNoParent && parent >= _closureBase && parent != to)
        link_inner(parent, to);
      if (!fresh) grown(to, parent);
    }
  }

  CURSIV_ALWAYS_INLINE void add_implied(Id from, std::uint16_t state,
                                       unsigned q) {
    Context context = _frames[from].context;
    context.implied |= decltype(Context::implied){1} << q;
    add(_closure, state, _frames[from].at, from, {}, kDeadState, context);
  }

  unsigned marked(Id id) const {
    return id < _marks.size() && (_marks[id] >> 2) == _serial
               ? static_cast<unsigned>(_marks[id] & 3u)
               : 0u;
  }
  void mark(Id id, unsigned state) {
    if (id >= _marks.size()) _marks.resize(_frames.size(), 0);
    _marks[id] = (_serial << 2) | state;
  }
  // The closure's node `above` rests on `below`: the edge goes last in the
  // list of the edges that rest on it.
  void link_inner(Id below, Id above) {
    const auto e = static_cast<std::uint32_t>(_inner.size());
    _inner.push_back(InnerEdge{above, kNoLink});
    const std::size_t k = below - _closureBase;
    if (k >= _innerOf.size()) _innerOf.resize(k + 1);
    InnerEnds& ends = _innerOf[k];
    if (ends.head == kNoLink)
      ends.head = e;
    else
      _inner[ends.tail].next = e;
    ends.tail = e;
  }
  // `id` gained predecessor `via`: it reads its reductions again through that
  // edge alone, and what the closure built on it reads through it in whole.
  void grown(Id id, Id via) {
    const unsigned status = marked(id);
    if (status == 0) return;
    if (status == kDone) _queue.push_back(Reread{id, via});
    if (_inner.empty()) return;
    const std::uint32_t walk = next_walk();
    _todo.clear();
    _todo.push_back(id);
    while (!_todo.empty()) {
      const Id at = _todo.back();
      _todo.pop_back();
      // Only the edges that rest on `at`, in the order they were added.
      if (at < _closureBase || at - _closureBase >= _innerOf.size()) continue;
      for (std::uint32_t e = _innerOf[at - _closureBase].head; e != kNoLink;
           e = _inner[e].next) {
        const Id above = _inner[e].above;
        const unsigned read = marked(above);
        if (read == 0) continue;
        if (above >= _seen.size()) _seen.resize(_frames.size(), 0);
        if (std::exchange(_seen[above], walk) == walk) continue;
        if (read == kDone) {
          mark(above, kDone | kQueued);
          _queue.push_back(Reread{above, kNoParent});
        }
        _todo.push_back(above);
      }
    }
  }

  // A round number for `_seen` that no frame holds.
  std::uint32_t next_walk() {
    if (++_epoch == 0) {
      std::ranges::fill(_seen, 0u);
      _epoch = 1;
    }
    return _epoch;
  }

  std::span<const Id> below(Id head, unsigned len, Id via = kNoParent) {
    // Where the reduction is read for one predecessor, the first frame it
    // pops leads there and nowhere else.
    if (via != kNoParent && len != 0) {
      head = via;
      --len;
    }
    // The common chain needs neither a vector nor a sort. The returned
    // singleton lives outside _frames, which reductions may reallocate.
    while (len != 0 && _frames[head].more == kNoParent) {
      head = _frames[head].parent;
      if (head == kNoParent) return {};
      --len;
    }
    if (len == 0) {
      _singleton = head;
      return {&_singleton, 1};
    }
    // Neither `add` nor its callbacks call `below`: both buffers remain
    // stable for the caller's entire reduction, including frame growth.
    auto* layer = &_below;
    auto* next = &_belowNext;
    layer->clear();
    layer->push_back(head);
    while (len-- != 0 && !layer->empty()) {
      next->clear();
      for (const Id at : *layer)
        each_parent(_frames[at], [&](Id parent) { next->push_back(parent); });
      std::sort(next->begin(), next->end());
      next->erase(std::unique(next->begin(), next->end()), next->end());
      std::swap(layer, next);
    }
    return *layer;
  }

  // Where a reduction by `prod` from `head` goes, as `lr_apply_reduce`:
  // a guarded goto by the tag of the state each span opened on, an empty
  // span by the gap before `token`.
  template <class F>
  void reduce_targets(Id head, const LrProduction& prod, const Token& token,
                      std::uint32_t assumes, F&& f, Id via = kNoParent) {
    if (prod.len == 0) {
      // A span with no frame reads no predecessor.
      if (via != kNoParent) return;
      targets(
          token, assumes, _table.go(_frames[head].state, prod.lhs),
          [&](std::uint32_t to, std::uint32_t taken) { f(head, to, taken); });
      return;
    }
    // Without a guard no goto asks where its span opened.
    if (_table.guardAt.empty()) {
      for (const Id parent : below(head, prod.len, via))
        _table.each_target(
            _table.go(_frames[parent].state, prod.lhs),
            [](unsigned) { return 1u; },
            [&](std::uint32_t to) { f(parent, to, assumes); });
      return;
    }
    // The span opened on the frame `len - 1` below the head; from it, one
    // predecessor further is where the goto is taken.
    for (const Id first :
         below(head, prod.len - 1u, prod.len == 1 ? kNoParent : via)) {
      const auto opened = _frames[first].state;
      const auto held = [&](unsigned k) {
        return _table.holds(opened, k) ? 2u : 0u;
      };
      // Read before `f`, which may move _frames and its own predecessors.
      _parentsOf.clear();
      if (via != kNoParent && prod.len == 1)
        _parentsOf.push_back(via);
      else
        each_parent(_frames[first], [&](Id p) { _parentsOf.push_back(p); });
      for (const Id parent : _parentsOf)
        _table.each_target(_table.go(_frames[parent].state, prod.lhs), held,
                           [&](std::uint32_t to) { f(parent, to, assumes); });
    }
  }

  template <bool Repair = false, class Reads, class Shift,
            class Error = decltype([](Id, const Token&) {
              /* by default an error goes unreported */
            })>
  void close(Reads&& reads, Shift&& shift, Error&& error = {}) {
    Stage& stage = _closure;
    stage.reset(_rank, _heads);
    ++_serial;
    _closureBase = _frames.size();
    _queue.clear();
    _inner.clear();
    _innerOf.clear();
    _closing = true;
    const auto read = [&](Id head, Id via) {
      const auto state = _frames[head].state;
      const auto at = _frames[head].at;
      const Context context = _frames[head].context;
      // `add` can move _frames; a held lookahead's span must point here,
      // not into that vector. Scanned tokens live in an arena nothing
      // appends to while one head's tokens are read.
      const Token held = _frames[head].la;
      const auto heldMode = _frames[head].laMode;
      for (const auto& token : reads(head, held)) {
        // A repair acts on its pinned terminal, asks conditions about the
        // author's token, and preserves that lookahead across reductions.
        Token repairToken;
        if constexpr (Repair)
          repairToken = token.begin == token.end
                            ? Token{held.begin, held.begin, token.sym}
                            : written(held, heldMode);
        const Token& asked = Repair ? repairToken : token;
        const auto cell = _table.run(state, token.sym);
        bool viable = false;
        // An empty span whose goto leads to no state leaves the token without
        // the action, as the drive's default arm takes it.
        const auto reduce_by = [&](std::uint16_t by, std::uint32_t assumes) {
          const auto& prod = _table.prods[by];
          const auto committed =
              prod.len == 0 ? (Repair ? held.begin : token.begin) : at;
          bool reached = prod.len != 0;
          reduce_targets(
              head, prod, asked, assumes,
              [&](Id parent, std::uint32_t target, std::uint32_t taken) {
                Context into =
                    Repair ? Context{.indent = context.indent} : context;
                into.assumes = taken;
                if (_passing) [[unlikely]]
                  into.passes = pass(context.passes, state, by);
                reached = true;
                add(stage, static_cast<std::uint16_t>(target), committed,
                    parent, Repair ? held : token,
                    Repair ? heldMode : _table.mode[state], into);
              },
              via);
          return reached;
        };
        for (std::uint32_t slot = cell.first; slot < cell.first + cell.count;
             ++slot) {
          const LrAction action = _table.action(slot);
          // Read again for one predecessor, only what goes through it is new:
          // the reductions.
          if (via != kNoParent && action.kind != LrActionKind::reduce) continue;
          // A token whose shift the gap decides goes to every target the text
          // leaves possible.
          if (action.kind == LrActionKind::guard) {
            targets(asked, context.assumes,
                    static_cast<std::uint32_t>(_table.mode.size()) + action.arg,
                    [&](std::uint32_t to, std::uint32_t taken) {
                      LrAction reached = action;
                      reached.kind = LrActionKind::shift;
                      reached.arg = static_cast<std::uint16_t>(to);
                      viable = true;
                      shift(head, reached, token, taken);
                    });
            continue;
          }
          if (action.kind == LrActionKind::reduce) {
            auto assumes = context.assumes;
            if (take(_table.prods[action.arg].gap, asked, assumes) &&
                reduce_by(action.arg, assumes))
              viable = true;
            continue;
          }
          viable = true;
          if (action.kind == LrActionKind::shift)
            shift(head, action, token, context.assumes);
        }
        // As in the drive, a tight mode's refused gap can still take its
        // sole guarded reduction or region exit before reading it again.
        if (!Repair && !viable && token.sym == kInvalidSymbol &&
            _table.modeTight[_table.mode[state]] != 0 && via == kNoParent) {
          const auto sole = _table.soleReduce[state];
          auto assumes = context.assumes;
          if (sole != kLrNoProd &&
              (_table.prods[sole].gap != 0 ||
               (sole < _table.reductions.size() &&
                _table.reductions[sole].regionExit)) &&
              take(_table.prods[sole].gap, token, assumes) &&
              reduce_by(sole, assumes))
            viable = true;
        }
        if (!Repair && !viable && via == kNoParent) error(head, token);
      }
    };
    // Each node is read once when it stands, and again for as long as it
    // gains predecessors afterwards: the closure saturates, and a node no
    // edge touched is not read twice.
    std::size_t k = 0;
    for (;;) {
      if (k < stage.active.size()) {
        const Id head = stage.active[k++];
        mark(head, kDone);
        read(head, kNoParent);
      } else if (!_queue.empty()) {
        const Reread again = _queue.back();
        _queue.pop_back();
        mark(again.node, kDone);
        read(again.node, again.via);
      } else {
        break;
      }
    }
    _closing = false;
  }

  // `close` off the replay's path: the callbacks go by reference, so these
  // closures share one instantiation.
  void close_shared(
      function_ref<std::span<const Token>(Id, const Token&)> reads,
      function_ref<void(Id, const LrAction&, const Token&, std::uint32_t)>
          shift,
      function_ref<void(Id, const Token&)> error = [](Id, const Token&) {}) {
    close(reads, shift, error);
  }

  // The implied token the drive inserts before a candidate the state refuses:
  // the first it acts on whose condition holds, each once per position.
  int implied_before(const Frame& frame, const Token& token) const {
    if (_implied.empty()) return -1;
    if (token.sym != kInvalidSymbol) {
      const auto cell = _table.run(frame.state, token.sym);
      for (std::uint32_t at = cell.first; at < cell.first + cell.count; ++at)
        if (takes(frame.state, _table.action(at), token)) return -1;
    }
    for (unsigned q = 0; q < _implied.size(); ++q)
      if (((frame.context.implied >> q) & 1u) == 0 &&
          _table.run(frame.state, _implied[q]).count != 0 &&
          condition(_table.terms[_implied[q]].cond + 1u, token))
        return static_cast<int>(q);
    return -1;
  }

  // Whether the drive takes `action` on `token` in `state`: a reduce its
  // condition refuses, a guard or an empty span's goto that leads to no state,
  // leaves the token to the default arm.
  bool takes(std::uint16_t state, const LrAction& action,
             const Token& token) const {
    const auto may = [&](unsigned k) { return outcome(k + 1u, token); };
    bool hasTarget = false;
    const auto reached = [&](std::uint32_t) { hasTarget = true; };
    if (action.kind == LrActionKind::guard) {
      _table.each_target(
          static_cast<std::uint32_t>(_table.mode.size()) + action.arg, may,
          reached);
      return hasTarget;
    }
    if (action.kind != LrActionKind::reduce) return true;
    const auto& prod = _table.prods[action.arg];
    if (!condition(prod.gap, token)) return false;
    if (prod.len != 0) return true;
    _table.each_target(_table.go(state, prod.lhs), may, reached);
    return hasTarget;
  }

  // The token the indentation reads where `head` stands at a line start:
  // the caret's run against the levels its reading holds open.
  bool level_at(Id head, TextOffset at, TextOffset held, Token& out) {
    const Frame& frame = _frames[head];
    const TextOffset p = frame.at;
    if (p > at || frame.context.indent.depth != 0 ||
        !indent_line_start(_text.data(), p, [held] { return held; }))
      return false;
    const IndentRun blanks = indent_run(_text.data(), p, at);
    return blanks.stop == at &&
           indent_token(_text.data(), _levels, frame.context.indent.head,
                        _indent, _dedent, p, blanks.run, at, out);
  }

  bool moves(SymbolId t) const {
    return (_indent != 0 && t == _indent) || (_dedent != 0 && t == _dedent) ||
           (t < _table.terms.size() && _table.terms[t].delim != 0);
  }

  CURSIV_ALWAYS_INLINE IndentAt levels_after(IndentAt in, const Token& token) {
    if (!indented()) return in;
    indent_shift(_levels, in, token, _indent, _dedent,
                 token.sym < _table.terms.size() ? _table.terms[token.sym].delim
                                                 : std::int8_t{0});
    return in;
  }

  int implied_of(SymbolId sym) const {
    for (std::size_t q = 0; q < _implied.size(); ++q)
      if (_implied[q] == sym) return static_cast<int>(q);
    return -1;
  }

  // The token in hand as the author wrote it: where `mode` read nothing, the
  // reading of the alphabet a fault there is lexed with, as the repair asks it.
  Token written(const Token& held, std::uint16_t mode) const {
    const auto len = static_cast<TextOffset>(_text.size());
    if (held.sym != kInvalidSymbol || held.begin >= len) return held;
    TextOffset halt = 0;
    const Scan s =
        _table.lex(_text.data(), len, _table.errModeOf(mode), held.begin, halt);
    if (s.term != kInvalidSymbol && s.end > held.begin)
      return Token{held.begin, s.end, s.term};
    return held;
  }

  // The text a candidate's conditions read: from the gap it stands in, the
  // word, then what follows the span it replaces.
  void condition_text(TextOffset at, TextOffset to, std::string_view word) {
    const TextOffset from = std::min(_gapFrom, _assumedFrom);
    const TextOffset gap = at - from;
    // The initial [0, 0) gap is already the empty cached string.
    if (_conditionFrom != from || _typedAt != at) {
      _conditionFrom = from;
      _typedAt = at;
      _conditionText.assign(_text.substr(from, gap));
      _conditionGaps.clear();
    }
    _conditionText.resize(gap);
    _conditionText.append(word);
    const std::size_t kept = std::max<std::size_t>(
        {_widest.look, _readEnd > to ? _readEnd - to : 0, _tailKept});
    _conditionText.append(_text.substr(to, kept));
    _tailFrom = to;
    _tailEnd =
        static_cast<TextOffset>(std::min<std::size_t>(_text.size(), to + kept));
  }
  // A phrase that read to the end of the kept text is asked again over more of
  // it, the candidate's token read again where the lexer read it. Whether more
  // was kept.
  bool grow() const {
    if (_tailEnd >= _text.size()) return false;
    const std::size_t more = std::max<std::size_t>(_tailEnd - _tailFrom, 16);
    _conditionText.append(_text.substr(_tailEnd, more));
    _tailEnd = static_cast<TextOffset>(
        std::min<std::size_t>(_text.size(), _tailEnd + more));
    _tailKept = _tailEnd - _tailFrom;
    if (_typedCut) typed_end();
    return true;
  }

  // The candidate typed as `word` is the token the lexer reads over the word
  // and the text behind it, which can run on past the word: in the mode of the
  // reading that takes it, the one that admits everything until one does.
  void typed_as(SymbolId term, std::string_view word) {
    _typedTerm = term;
    _typedWord = static_cast<TextOffset>(_typedAt + word.size());
    _typedEnd = _typedWord;
    _typedMode = _table.errMode;
    _typedCut = false;
    if (!word.empty()) typed_end();
  }
  void typed_in(std::uint16_t mode) {
    if (_typedWord == _typedAt || _typedMode == mode) return;
    _typedMode = mode;
    typed_end();
  }
  void typed_end() const {
    const TextOffset from = _typedAt - _conditionFrom;
    const auto len = static_cast<TextOffset>(_conditionText.size());
    TextOffset halt = 0;
    const Scan s =
        _table.lex(_conditionText.data(), len, _typedMode, from, halt);
    _typedCut = halt >= len;
    _typedEnd = s.term == _typedTerm && s.end > from ? _conditionFrom + s.end
                                                     : _typedWord;
  }

  bool condition(unsigned gap, Token token) const {
    if (gap == 0 || _table.cond == nullptr || _loose) return true;
    if (!_candidate)
      return _table.cond(_text, gap - 1, _gapFrom, token, true, nullptr);
    _asked = true;
    const bool holds = spliced(gap, _gapFrom, token);
    if (_keeping) _answers.push_back({gap, _gapFrom, token, holds});
    return holds;
  }
  // The condition on the candidate's text, which begins at `_conditionFrom`.
  bool spliced(unsigned gap, TextOffset from, const Token& token) const {
    from -= _conditionFrom;
    for (;;) {
      Token in = token;
      if (in.begin == _typedAt && in.sym == _typedTerm) in.end = _typedEnd;
      in.begin -= _conditionFrom;
      in.end -= _conditionFrom;
      const bool holds = _table.cond(_conditionText, gap - 1, from, in, _known,
                                     &_conditionGaps);
      if (!_known || _table.condSeen == nullptr ||
          !_table.condReach[gap - 1u].phrase ||
          _table.condSeen(_conditionText, gap - 1u, from, in) <
              _conditionText.size() ||
          !grow())
        return holds;
    }
  }
  // The same, 0 false, 1 unknown, 2 true: what may hold is certain only
  // where its bytes are, and a loose closure refuses nothing.
  unsigned outcome(unsigned gap, const Token& token) const {
    if (!condition(gap, token)) return 0;
    return gap == 0 || (_table.cond != nullptr && !_loose &&
                        (!_candidate || _known))
               ? 2u
               : 1u;
  }

  // Whether condition `gap` asked of `token` may read past the edge: a token
  // with a width bounds what a run consumes.
  bool reaches(const Token& token, unsigned gap) const {
    if (_edge == kNoEdge || _table.cond == nullptr) return false;
    if (token.begin >= _edge) return true;
    const CondReach& r = _table.condReach[gap - 1u];
    const std::uint64_t width =
        token.end > token.begin ? token.end - token.begin : 0u;
    const std::uint64_t look =
        width != 0 ? std::min<std::uint64_t>(r.look, width + r.tail) : r.look;
    if (token.begin + look > _edge) return true;
    if (!r.phrase) return false;
    const TextOffset seen = _table.condSeen(_text, gap - 1u, _gapFrom, token);
    // A phrase that read to the end of the text goes on with what is typed.
    return seen > _edge || (seen == _edge && _edge == _text.size());
  }
  // The replay leaves such a condition to the candidates: the text it was
  // read on decides nothing past the edge.
  bool defers(const Token& token, unsigned gap) const {
    return !_candidate && !_loose && !_asWritten && reaches(token, gap);
  }
  // The same for a guard, when any condition it asks is one of them.
  bool defers_any(const Token& token, std::uint32_t target) const {
    if (_candidate || _loose || _asWritten) return false;
    for (const std::uint16_t condition : _table.conditions_of(target))
      if (reaches(token, condition + 1u)) return true;
    return false;
  }

  // Whether condition `gap` may hold on `token`; a deferred one holds as an
  // issue `assumes` takes.
  bool take(unsigned gap, const Token& token, std::uint32_t& assumes) {
    if (gap == 0 || !defers(token, gap)) [[likely]]
      return condition(gap, token);
    assumes = assume(assumes, gap, token, true);
    return assumes != kContradicted;
  }

  // Every target of `target` on `token`, with its issues added to `assumes`.
  template <class F>
  CURSIV_ALWAYS_INLINE void targets(const Token& token, std::uint32_t assumes,
                                    std::uint32_t target, F&& f) {
    if (defers_any(token, target)) [[unlikely]] {
      deferred(token, assumes, target, f);
      return;
    }
    _table.each_target(
        target, [&](unsigned k) { return outcome(k + 1u, token); },
        [&](std::uint32_t to) { f(to, assumes); });
  }
  // Deferred, one round per vector of issues. Out of line: inlined, it costs
  // every closure its inlining, conditions or not.
  template <class F>
  CURSIV_NOINLINE void deferred(const Token& token, std::uint32_t assumes,
                                std::uint32_t target, F& f) {
    _table.each_target_with_truth(
        target,
        [&](unsigned k) {
          return reaches(token, k + 1u) ? 1u
                                        : condition(k + 1u, token) ? 2u : 0u;
        },
        [&](std::uint32_t to, auto conditions, unsigned known, unsigned truth) {
          auto taken = assumes;
          for (unsigned b = 0;
               b < conditions.size() && taken != kContradicted; ++b)
            if (((known >> b) & 1u) == 0)
              taken = assume(taken, conditions[b] + 1u, token,
                             ((truth >> b) & 1u) != 0);
          if (taken != kContradicted) f(to, taken);
        });
  }

  // `chain` grown by the reduction `prod` that `state` took. A chain is a set.
  CURSIV_NOINLINE std::uint32_t pass(std::uint32_t chain, std::uint16_t state,
                                     std::uint16_t prod) {
    for (auto l = chain; l != 0; l = _passes[l].rest)
      if (_passes[l].state == state && _passes[l].prod == prod) return chain;
    if (_passes.empty()) _passes.emplace_back();
    const auto [it, fresh] = _passed.try_emplace(
        (std::uint64_t{chain} << 32) | (std::uint32_t{state} << 16) | prod,
        static_cast<std::uint32_t>(_passes.size()));
    if (fresh) _passes.push_back(Pass{state, prod, chain});
    return it->second;
  }

  // `set` grown by issue `holds` of condition `gap` on `token`, or
  // kContradicted where it took the other. One set grown by one issue is
  // one link: a closure through deferred conditions still saturates.
  CURSIV_NOINLINE std::uint32_t assume(std::uint32_t set, unsigned gap,
                                       const Token& token, bool holds) {
    // A reading's issues on the token in hand are its latest.
    for (auto l = set; l != 0; l = _links[l].rest) {
      const Assumed& a = _assumed[_links[l].assumed];
      if (a.token != token) break;
      if (a.gap == gap && a.from == _gapFrom)
        return a.holds == holds ? set : kContradicted;
    }
    const Assumed issue{gap, token, _gapFrom, holds};
    auto id = _assumed.size();
    for (auto i = _assumed.size(); i-- > 0 && _assumed[i].token == token;)
      if (_assumed[i] == issue) {
        id = i;
        break;
      }
    if (id == _assumed.size()) {
      _assumed.push_back(issue);
      _assumedFrom = std::min({_assumedFrom, issue.from, token.begin});
    }
    if (_links.empty()) _links.emplace_back();
    const auto [it, fresh] =
        _grown.try_emplace((std::uint64_t{set} << 32) | id,
                           static_cast<std::uint32_t>(_links.size()));
    if (fresh) _links.push_back(Link{static_cast<std::uint32_t>(id), set});
    return it->second;
  }

  // What one candidate's text says of each deferred issue, and so which
  // links are dead. A token the splice cuts, or bytes it does not spell,
  // refute nothing.
  void judge(TextOffset at) {
    _dead.clear();
    if (_links.empty()) return;
    _asked = true;
    _refuted.assign(_assumed.size(), 0);
    for (std::size_t i = 0; i < _assumed.size(); ++i) {
      const Assumed& a = _assumed[i];
      if (a.token.end > at) continue;
      const bool may = spliced(a.gap, a.from, a.token);
      if (_keeping) _answers.push_back({a.gap, a.from, a.token, may});
      _refuted[i] = a.holds ? !may : may && _known;
    }
    _dead.assign(_links.size(), 0);
    for (std::size_t l = 1; l < _links.size(); ++l)
      _dead[l] = _refuted[_links[l].assumed] | _dead[_links[l].rest];
  }

  void forget_scans() {
    _scanned.clear();
    _tokens.clear();
    _crossed.clear();
  }

  // What a head reads: the token it holds where its mode read it, else what its
  // mode lexes there. A flagged winner its stacks refuse is taken off and the
  // position read again, as the drive does.
  std::span<const Token> reading(Id head, const Token& held) {
    const Frame& frame = _frames[head];
    const auto mode = _table.mode[frame.state];
    if (frame.laMode == mode) return {&held, 1};
    const auto& seen =
        _scanned[lex(mode, frame.at, frame.context.indent.depth != 0)];
    // A condition the replay would leave to the candidates decides a token the
    // tape did not read here on the text as it is: the drive refused it.
    const auto stands = [&](const Token& t) {
      const bool faulting = _asWritten;
      _asWritten = faulting || (_tape != nullptr && (t.begin != _tape->begin ||
                                                    t.end != _tape->end));
      const bool taken = takes(head, t);
      _asWritten = faulting;
      return taken;
    };
    const bool inside = frame.context.indent.depth != 0;
    unsigned weight = 0;
    // A trivia left where a terminal is refused is crossed, as the drive does.
    const auto without = [&](std::span<const SymbolId> skip, TextOffset from) {
      const auto len = static_cast<TextOffset>(_text.size());
      for (TextOffset p = from;;) {
        p = past_skip_run(mode, p);
        if (p == len) return Token{p, p, kEofSymbol};
        TextOffset halt = 0;
        const Scan t =
            _table.lexWithout(_text.data(), len, mode, p, skip, halt);
        weight = lex_weight(t.priority);
        if (t.term == kInvalidSymbol || t.end <= p)
          return Token{p, p, kInvalidSymbol};
        if (!crosses(t.term, inside)) return Token{p, t.end, t.term};
        p = t.end;
      }
    };
    Token winner = seen.won;
    if (!refused(mode, winner, stands, without))
      return {_tokens.data() + seen.first, seen.count};
    _unrefused.assign(1, winner);
    refused_ties(mode, winner, weight, stands, without, [&](const Token& t) {
      _unrefused.push_back(t);
      return _unrefused.size() < _table.maxCands;
    });
    return _unrefused;
  }

  // The scan of `mode` at `from`, by its place among the scans kept.
  std::size_t lex(std::uint16_t mode, TextOffset from, bool inside) {
    for (std::size_t i = 0; i < _scanned.size(); ++i)
      if (_scanned[i].mode == mode && _scanned[i].at == from &&
          _scanned[i].inside == inside)
        return i;
    const auto first = static_cast<std::uint32_t>(_tokens.size());
    const Token won = scan(mode, from, inside);
    _scanned.push_back(
        Scanned{mode, inside, from, first,
                static_cast<std::uint32_t>(_tokens.size() - first), won});
    return _scanned.size() - 1;
  }

  // Whether an arm of a cell of several takes `token`, as the drive asks it.
  // Conditions are read through empty rules; an arm that reduces anything else
  // counts as taken, and so does what the replay leaves to the candidates.
  bool cell_takes(std::uint32_t state, const Token& token) const {
    const auto mayHold = [&](unsigned k) { return outcome(k + 1u, token); };
    return lr_cell_walk(state, [&](std::uint32_t at, auto&& leads) {
      const auto cell = _table.run(static_cast<std::uint16_t>(at), token.sym);
      for (std::uint32_t slot = cell.first; slot < cell.first + cell.count;
           ++slot) {
        const LrAction action = _table.action(slot);
        if (action.kind == LrActionKind::guard) {
          if (defers_any(token, static_cast<std::uint32_t>(_table.mode.size()) +
                                    action.arg) ||
              takes(static_cast<std::uint16_t>(at), action, token))
            return true;
          continue;
        }
        if (action.kind != LrActionKind::reduce) return true;
        const LrProduction& p = _table.prods[action.arg];
        if (p.gap != 0 && !defers(token, p.gap) && !condition(p.gap, token))
          continue;
        if (p.len != 0) return true;
        const std::uint32_t target =
            _table.go(static_cast<std::uint16_t>(at), p.lhs);
        if (defers_any(token, target)) return true;
        _table.each_target(target, mayHold, leads);
      }
      return false;
    });
  }

  // Whether some stack under `head` shifts `token` after the reductions the
  // table has for it, a guarded shift or target as the drive asks it. What
  // the replay leaves to the candidates counts as taken.
  bool takes(Id head, const Token& token) {
    const auto mayHold = [&](unsigned k) { return outcome(k + 1u, token); };
    return lr_graph_takes(
        _takes, head, [&](Id v) { return std::uint32_t{_frames[v].state}; },
        [&](Id v, auto&& f) { each_parent(_frames[v], f); },
        [&](std::uint32_t top, std::uint32_t& prod, std::uint32_t& len) {
          const auto cell =
              _table.run(static_cast<std::uint16_t>(top), token.sym);
          if (cell.count == 0) return 0;
          const LrAction action = _table.action(cell.first);
          if (cell.count == 1 && action.kind == LrActionKind::guard)
            return defers_any(token,
                              static_cast<std::uint32_t>(_table.mode.size()) +
                                  action.arg) ||
                           takes(static_cast<std::uint16_t>(top), action, token)
                       ? 1
                       : 0;
          if (cell.count != 1) return cell_takes(top, token) ? 1 : 0;
          if (action.kind != LrActionKind::reduce) return 1;
          const LrProduction& p = _table.prods[action.arg];
          if (p.gap != 0 && !defers(token, p.gap) && !condition(p.gap, token))
            return 0;
          // An empty span's target asks the token: what the text does not
          // decide goes on to several states.
          if (p.guarded != 0 && p.len == 0) {
            const std::uint32_t target = _table.go(top, p.lhs);
            if (defers_any(token, target)) return 1;
            unsigned targets = 0;
            _table.each_target(target, mayHold,
                               [&](std::uint32_t) { ++targets; });
            if (targets != 1) return targets == 0 ? 0 : 1;
          }
          prod = action.arg;
          len = p.len;
          return -1;
        },
        [&](std::uint32_t prod) { return _table.prods[prod].guarded != 0; },
        [&](std::uint32_t from, std::uint32_t prod, std::uint32_t first) {
          std::uint32_t to = kInvalidSymbol;
          const auto reached = [&](std::uint32_t state) { to = state; };
          const std::uint32_t target = _table.go(from, _table.prods[prod].lhs);
          if (first == kLrNoFirst)
            _table.each_target(target, mayHold, reached);
          else
            _table.each_target(
                target,
                [&](unsigned k) { return _table.holds(first, k) ? 2u : 0u; },
                reached);
          return to;
        });
  }

  // A trivia, or a statement's newline inside a bracket.
  bool crosses(SymbolId t, bool inside) const {
    return _table.terms[t].trivia || (inside && _table.terms[t].joins);
  }

  // The tokens one position reads in one mode, appended to the arena, and
  // the one the lexer elects.
  Token scan(std::uint16_t mode, TextOffset from, bool inside) {
    auto& result = _tokens;
    const auto found = result.size();
    const auto len = static_cast<TextOffset>(_text.size());
    auto at = from;
    for (;;) {
      at = past_skip_run(mode, at);
      if (at == len) return result.emplace_back(Token{at, at, kEofSymbol});
      std::uint32_t count = 0;
      const Scan first = elect(mode, at, true, count);
      if (first.term == kInvalidSymbol || first.end == at)
        return result.emplace_back(Token{at, at, kInvalidSymbol});
      if (crosses(first.term, inside)) {
        at = first.end;
        continue;
      }
      if (count == 0) result.push_back(Token{at, first.end, first.term});
      for (std::uint32_t k = 0; k < count; ++k)
        if (!_table.terms[_room[k].term].trivia)
          result.push_back(Token{at, _room[k].end, _room[k].term});
      // A capped sweep can hold competitors alone: the elected token is always
      // read.
      if (result.size() == found)
        result.push_back(Token{at, first.end, first.term});
      return Token{at, first.end, first.term};
    }
  }

  // Whether only trivia stands between `at` and `to` for `head`. Asked once per
  // mode and span, which the heads of one closure share, unless a terminal the
  // mode flags stands there: the stacks of the head decide that one.
  bool trivia_to(Id head, std::uint16_t mode, TextOffset at, TextOffset to) {
    const bool inside = _frames[head].context.indent.depth != 0;
    for (const Crossed& c : _crossed)
      if (c.mode == mode && c.inside == inside && c.at == at && c.to == to)
        return c.ok;
    bool asked = false;
    const bool ok = trivia_scan(mode, at, to, inside, nullptr, [&](Token& la) {
      asked = true;
      return refused_in_place(head, mode, la);
    });
    if (!asked) _crossed.push_back(Crossed{mode, inside, at, to, ok});
    return ok;
  }

  // `la` read again in place while `head` refuses it, as the drive reads it: a
  // refused terminal may leave its position to a trivia.
  bool refused_in_place(Id head, std::uint16_t mode, Token& la) {
    return refused(
        mode, la, [&](const Token& t) { return takes(head, t); },
        [&](std::span<const SymbolId> skip, TextOffset from) {
          TextOffset halt = 0;
          const Scan t = _table.lexWithout(
              _text.data(), static_cast<TextOffset>(_text.size()), mode, from,
              skip, halt);
          return t.term == kInvalidSymbol || t.end <= from
                     ? Token{from, from, kInvalidSymbol}
                     : Token{from, t.end, t.term};
        });
  }

  // Whether only trivia runs from `at` to `to`. `tail` takes the last token the
  // walk read; `refuses(la)` reads a token that is no trivia again, where a
  // stack can refuse it.
  template <class Refuses>
  bool trivia_scan(std::uint16_t mode, TextOffset at, TextOffset to,
                   bool inside, Trivia* tail, Refuses&& refuses) {
    while (at < to) {
      std::uint32_t count = 0;
      Scan next = elect(mode, at, false, count);
      if (next.term == kInvalidSymbol || next.end <= at) return false;
      if (!crosses(next.term, inside)) {
        Token la{at, next.end, next.term};
        if (!refuses(la) || la.sym == kInvalidSymbol || la.end <= at ||
            !crosses(la.sym, inside))
          return false;
        next = Scan{la.end, la.sym};
      }
      if (next.end > to) return ends_before_word(mode, at, to, next.term);
      if (tail != nullptr) *tail = Trivia{at, next};
      at = next.end;
    }
    return at == to;
  }

  // Whether the text from `at` to `to` is still all trivia with a word right
  // behind, as `StampedGrammar::ends_before_word` asks it.
  bool ends_before_word(std::uint16_t mode, TextOffset at, TextOffset to,
                        SymbolId term) const {
    return StampedGrammar::ends_before_word(
        _text.substr(at, to - at), term, _table.terms,
        [&](std::string_view s) {
          TextOffset halt = 0;
          return _table.lex(s.data(), static_cast<TextOffset>(s.size()), mode,
                            0, halt);
        });
  }

  // Past the run of blanks at `at`.
  TextOffset past_skip_run(std::uint16_t mode, TextOffset at) const {
    return lex_past_run(
        _text.data(), at, static_cast<TextOffset>(_text.size()),
        [&](unsigned char b) {
          return mode_row_has(_table.skipSet.data(), mode, b);
        },
        [&](unsigned char b) {
          return mode_row_has(_table.skipStop.data(), mode, b);
        });
  }

  // The token a mode elects at `at`, or the first competitor its sweep holds
  // where it elects none; the competitors are put in `_room`, swept always when
  // `scanAll` asks, else only for want of an elected token.
  Scan elect(std::uint16_t mode, TextOffset at, bool scanAll,
             std::uint32_t& count) {
    const auto len = static_cast<TextOffset>(_text.size());
    TextOffset halt = 0;
    Scan first = _table.lex(_text.data(), len, mode, at, halt);
    const bool none = first.term == kInvalidSymbol || first.end == at;
    count = 0;
    if ((scanAll || none) && _table.modeMulti[mode] != 0 &&
        _table.scan != nullptr)
      count = _table.scan(_text.data(), len, mode, at, _room.data(),
                          _table.maxCands);
    if (none && count != 0) first = _room[0];
    return first;
  }

  const LrCaretTable& _table;
  std::string_view _text;
  TextOffset _edge;
  // The farthest any condition of the table reads.
  CondReach _widest{};
  // The end of the text a candidate's conditions can read past it.
  TextOffset _readEnd = 0;
  // A condition the replay deferred, with the issue a reading took of it.
  struct Assumed {
    unsigned gap;
    Token token;
    TextOffset from;
    bool holds;
    bool operator==(const Assumed&) const = default;
  };
  std::vector<Assumed> _assumed;
  TextOffset _assumedFrom = kNoEdge;
  // The sets of issues readings took, as chains sharing their tails; link 0
  // is the empty set, created with the first.
  struct Link {
    std::uint32_t assumed = 0;
    std::uint32_t rest = 0;
  };
  std::vector<Link> _links;
  std::unordered_map<std::uint64_t, std::uint32_t> _grown;
  std::vector<char> _refuted;
  std::vector<char> _dead;
  // Conditions read the text as it is: none is left to the candidates.
  bool _asWritten = false;
  // The leaf the closure in hand replays, what the tape says was read there.
  const CstLeaf* _tape = nullptr;
  SymbolId _indent = 0, _dedent = 0;
  bool _bracketed = false;
  std::vector<SymbolId> _implied;
  // Every replayed branch has consumed the same leaves, including repairs.
  TextOffset _gapFrom = 0;
  bool _candidate = false;
  bool _known = true;
  mutable bool _asked = false;
  bool _keeping = false;
  struct Answer {
    unsigned gap;
    TextOffset from;
    Token token;
    bool holds;
  };
  mutable std::vector<Answer> _answers;
  // A superset of the candidates is sought: no condition refuses.
  bool _loose = false;
  // The gap before the caret, its typed candidate and what follows it: the
  // gap is copied, and its conditions read, once for every candidate.
  mutable std::string _conditionText;
  // Where the text kept past the candidate begins and ends, and how much of
  // it a condition read so far.
  TextOffset _tailFrom = 0;
  mutable TextOffset _tailEnd = 0;
  mutable std::size_t _tailKept = 0;
  SymbolId _typedTerm = kInvalidSymbol;
  // The caret position shared by the kept gap and its typed candidate.
  TextOffset _typedAt = 0;
  TextOffset _typedWord = 0;
  mutable TextOffset _typedEnd = 0;
  std::uint16_t _typedMode = 0;
  // Whether the lexer read the candidate's token to the end of the text kept.
  mutable bool _typedCut = false;
  TextOffset _conditionFrom = 0;
  mutable LrGapMemo _conditionGaps;
  std::vector<Scan> _room;
  std::vector<Frame> _frames;
  std::vector<Block> _blocks;
  std::vector<Id> _pool;
  std::vector<Id> _parentsOf;
  std::vector<Id> _heads;
  std::vector<Id> _headsBefore;
  // The readings before the leaf at the edge, and the gap that follows them.
  std::vector<Id> _edgeHeads;
  TextOffset _edgeGapFrom = 0;
  Id _singleton = 0;
  std::uint32_t _epoch = 0;
  // The last walk that passed each frame.
  std::vector<std::uint32_t> _seen;
  Token _virt{};
  Token _longerToken{};
  // A flagged winner read again, and the terminals taken off its position.
  std::vector<Token> _unrefused;
  std::vector<SymbolId> _skip;
  bool _outlasted = false;
  std::vector<std::pair<std::uint16_t, std::uint16_t>> _shifted;
  // What a mode reads of the candidate in hand, kept for the one `next`.
  struct Allowed {
    std::uint16_t mode;
    Scan scan;
  };
  std::vector<Allowed> _allowed;
  std::vector<Token> _impliedTokens;
  std::vector<Id> _ready;
  // The levels every reading of the replay opened, chained as the drive's.
  std::vector<IndentLevel> _levels;
  std::vector<LeafRead> _leafReads;
  // The chains of passes, sharing their tails; link 0 is the empty chain.
  // Only the closure that reads the leaf at the edge grows them.
  std::vector<Pass> _passes;
  std::unordered_map<std::uint64_t, std::uint32_t> _passed;
  bool _passing = false;
  std::vector<Id> _below;
  std::vector<Id> _belowNext;
  std::size_t _rank = 0;
  Stage _closure;
  Stage _next;
  // The closure in hand: its nodes are the frames from `_closureBase` on, done
  // once they read their predecessors, queued again when they gain one. A mark
  // holds the closure's serial, so none is cleared between closures.
  static constexpr std::uint64_t kDone = 1, kQueued = 2;
  bool _closing = false;
  Id _closureBase = 0;
  std::uint64_t _serial = 0;
  std::vector<std::uint64_t> _marks;
  // A node to read again, by the predecessor it gained or in whole.
  struct Reread {
    Id node;
    Id via;
  };
  std::vector<Reread> _queue;
  std::vector<Id> _todo;
  LrTakesScratch<Id> _takes;
  // Predecessor and successor, both in the closure: what reads through one
  // reads the predecessors it gains. A predecessor's successors are a list
  // through `_inner`.
  static constexpr std::uint32_t kNoLink = 0xFFFFFFFFu;
  struct InnerEdge {
    Id above;
    std::uint32_t next;
  };
  struct InnerEnds {
    std::uint32_t head = kNoLink;
    std::uint32_t tail = kNoLink;
  };
  std::vector<InnerEdge> _inner;
  std::vector<InnerEnds> _innerOf;
  // The tokens one mode reads at one place, in the arena, and the one the
  // lexer elects of them.
  struct Scanned {
    std::uint16_t mode;
    bool inside;
    TextOffset at;
    std::uint32_t first;
    std::uint32_t count;
    Token won;
  };
  std::vector<Scanned> _scanned;
  struct Crossed {
    std::uint16_t mode;
    bool inside;
    TextOffset at;
    TextOffset to;
    bool ok;
  };
  std::vector<Crossed> _crossed;
  std::vector<Token> _tokens;
};

// The tape read as a forest up to the caret: a clean closed record is one frame
// pushed by `jump`, one with a repair is replayed leaf by leaf, and an
// abandoned region replays and unwinds. Iterative.
class CaretReplay {
 public:
  CaretReplay(const LrCaretTable& table, const Cst& cst, CaretStacks& stacks,
              TextOffset horizon, const CancellationToken& cancelToken)
      : _table(table),
        _cst(cst),
        _stacks(stacks),
        _horizon(horizon),
        _stop(cancelToken) {
    for (const ParseDiagnostic& d : cst.diagnostics())
      if (d.isSyntax()) _firstRepair = std::min(_firstRepair, d.begin);
  }

  // The records under `prefix` in order, then the repairs `events` names. Out
  // of line: inlined, the caller's register pressure falls on every job.
  CURSIV_NOINLINE bool run(NodeId prefix, std::span<const NodeId> events) {
    for (std::size_t i = events.size(); i-- > 0;)
      _jobs.push_back(Job{events[i], 0, false});
    // The replay starts past the nearest record it can start from.
    for (NodeId r = prefix; r > 0; r = _cst.subtree_begin(r - 1)) {
      _stop.poll();
      if (r != prefix && starts_after(r - 1)) break;
      _jobs.push_back(Job{r - 1, 0, false});
    }
    while (!_jobs.empty()) {
      _stop.poll();
      const Job job = _jobs.back();
      _jobs.pop_back();
      if (!step(job)) return false;
    }
    return true;
  }

 private:
  struct Job {
    NodeId id;
    std::size_t rank;
    bool close;
  };

  // Starts the replay past `id` where it can: with no repair up to the next
  // token and one reading, the stack under the state `id` was stamped with is
  // known when each of its states has one predecessor down to the start state.
  bool starts_after(NodeId id) {
    const CstNode& n = _cst[id];
    const auto state = _table.stamp.state(n);
    if (_table.preds.empty() || n.hidden || n.recovered || n.begin == n.end ||
        n.end > _horizon || state == 0 || state >= _table.mode.size() ||
        std::ranges::find(_unknown, state) != _unknown.end())
      return false;
    if (n.subtreeSize == 1) {
      // Several readings of one leaf are several stacks.
      const SymbolId term = _table.shiftTerm[state];
      if (_table.chooses || term == kInvalidSymbol || _table.synthetic(term))
        return false;
    }
    const NodeId k = token_after(id);
    if (_firstRepair <= (k < _cst.size() ? _cst[k].begin : n.end) ||
        !_stacks.keeps_tape(_cst, id))
      return false;
    const std::span<const std::uint16_t> preds = _table.preds;
    _states.clear();
    for (std::uint16_t s = state; s != 0; s = preds[s]) {
      if (s == kDeadState || _states.size() == kStartDepth) {
        _unknown.push_back(state);
        return false;
      }
      _states.push_back(s);
    }
    std::ranges::reverse(_states);
    _stacks.seed(_states, n.end, token_end(id, _cst.subtree_begin(id)));
    _gathered = id + 1;
    return true;
  }

  // The first token after record `id`, or the tape's size.
  NodeId token_after(NodeId id) const {
    do ++id;
    while (id < _cst.size() && (_cst[id].subtreeSize != 1 || _cst[id].hidden));
    return id;
  }

  // The end of the last token with a width from `last` down to `first`, as
  // `jump` takes it.
  TextOffset token_end(NodeId last, NodeId first) const {
    while (last > first && (_cst[last].subtreeSize != 1 || _cst[last].hidden ||
                            _cst[last].begin == _cst[last].end))
      --last;
    return _cst[last].end;
  }

  // No repair inside the record, nor on the token after it: the tape may
  // close a record before the token a repair invents for it. The repairs are
  // gathered only as far as the records asked about.
  bool clean(NodeId id) {
    if (!_cst.recovered()) return true;
    for (; _gathered <= id; ++_gathered)
      if (_cst[_gathered].recovered) _repairs.push_back(_gathered);
    const auto it = std::lower_bound(_repairs.begin(), _repairs.end(),
                                     _cst.subtree_begin(id));
    if (it != _repairs.end() && *it <= id) return false;
    const NodeId k = token_after(id);
    return k == _cst.size() || !_cst[k].recovered;
  }

  bool step(const Job& job) {
    const CstNode& n = _cst[job.id];
    if (job.close)
      return _stacks.unwind(job.rank, _table.stamp.state(n), n.end);
    if (n.hidden) return true;
    const auto state = _table.stamp.state(n);
    if (n.subtreeSize == 1) {
      if (!n.recovered) {
        const auto virt = virt_of(n);
        return (n.begin == n.end && virt == kInvalidSymbol) ||
               _stacks.consume(CstLeaf{job.id, n.begin, n.end, false, false},
                               virt);
      }
      // A repair's leaf carries the state that took it; only dropped text
      // carries none.
      if (state != 0) {
        if (state >= _table.shiftTerm.size()) return false;
        const auto term = _table.shiftTerm[state];
        if (n.begin != n.end && term == kInvalidSymbol) return false;
        return _stacks.repair(term, state, n.begin, n.end);
      }
      return _stacks.skip(n.begin, n.end);
    }
    if (n.recovered) {
      // The unwind comes after the content it gave up on; a wrap's content
      // simply stands.
      if (_table.stamp.row(n) == kNoElement)
        _jobs.push_back(Job{job.id, _stacks.rank(), true});
    } else if (n.end <= _horizon && clean(job.id) &&
               _stacks.keeps_tape(_cst, job.id)) {
      const auto row = _table.stamp.row(n);
      const auto lhs = _table.row_lhs(row);
      NodeId f = _cst.subtree_begin(job.id);
      while (f < job.id && _cst[f].hidden) ++f;
      const CstNode& t = _cst[f];
      const auto first = _table.stamp.state(t);
      const SymbolId term = first < _table.shiftTerm.size()
                                ? _table.shiftTerm[first]
                                : kInvalidSymbol;
      if (!lhs.empty() && t.subtreeSize == 1 && t.begin != t.end &&
          term != kInvalidSymbol && term != 0 && !_table.synthetic(term)) {
        // The record is clean: a token with a width stands at or after its
        // first.
        if (_stacks.jump(Token{t.begin, t.end, term}, lhs, n.end,
                         token_end(job.id - 1, f))) {
          if (_stacks.indented()) carry(f, job.id);
          return true;
        }
      }
    }
    for (const NodeId child : _cst.reverse_children(job.id))
      _jobs.push_back(Job{child, 0, false});
    return true;
  }

  // Out of line, as `run`: inlined, its loop weighs on every step.
  CURSIV_NOINLINE void carry(NodeId from, NodeId to) {
    for (NodeId k = from; k < to; ++k) {
      const CstNode& leaf = _cst[k];
      if (leaf.subtreeSize != 1 || leaf.hidden) continue;
      const auto state = _table.stamp.state(leaf);
      if (state < _table.shiftTerm.size())
        _stacks.carry(Token{leaf.begin, leaf.end, _table.shiftTerm[state]});
    }
  }

  // An indentation's or an implied token's leaf is the tape's, not the
  // lexer's: the state that took it names its terminal.
  SymbolId virt_of(const CstNode& n) const {
    const auto state = _table.stamp.state(n);
    if (state == 0 || state >= _table.shiftTerm.size()) return kInvalidSymbol;
    const auto term = _table.shiftTerm[state];
    return _table.synthetic(term) ? term : kInvalidSymbol;
  }

  const LrCaretTable& _table;
  const Cst& _cst;
  CaretStacks& _stacks;
  TextOffset _horizon;
  CancellationPoll _stop;
  // The repairs among the records before `_gathered`, in order.
  std::vector<NodeId> _repairs;
  NodeId _gathered = 0;
  // Where the first repair begins, the stack a replay starts from, and the
  // states whose stack is not known from them alone.
  TextOffset _firstRepair = ~TextOffset{0};
  std::vector<std::uint16_t> _states;
  std::vector<std::uint16_t> _unknown;
  // The deepest stack rebuilt.
  static constexpr std::size_t kStartDepth = 256;
  std::vector<Job> _jobs;
};

// The end past which no record is stepped over whole, the replay ending on leaf
// `last`: a cell after a record resolves within kLrLookBudget tokens, plus one
// for the lexer's peek. An unresolved cell has no such bound.
TextOffset jump_horizon(const LrCaretTable& table, const Cst& cst,
                        NodeId last) {
  if (!table.chooses) return static_cast<TextOffset>(cst.text().size());
  unsigned seen = 0;
  for (NodeId r = last + 1; r > 0;) {
    const CstLeaf l = cst.as_leaf(r = cst.leaf_before(r));
    if (l.hidden || l.begin == l.end) continue;
    if (++seen == kLrLookBudget + 2u) return l.end;
  }
  return 0;
}

// A caret separated from a token the repair invented only by bytes that were
// blanks before the invention stands in the same gap, so it may precede it.
void step_before_invented(const LrCaretTable& table, const Cst& cst,
                          TextOffset at, CstLeaf& leaf,
                          const CancellationToken& cancelToken) {
  const std::string_view text = cst.text();
  for (NodeId k = leaf ? leaf.id + 1 : 0; k > 0;) {
    const CstLeaf l = cst.as_leaf(k = cst.leaf_before(k));
    if (l.recovered) {
      if (l.begin != l.end || table.stamp.state(cst[l.id]) == 0) return;
      NodeId behind = k;
      const CstLeaf before =
          read_leaf_before(cst, table.stamp, behind, nullptr);
      CaretStacks probe(table, text);
      CaretReplay replay(
          table, cst, probe,
          before ? jump_horizon(table, cst, before.id) : TextOffset{0},
          cancelToken);
      if (replay.run(before ? before.id + 1 : 0, {}) &&
          probe.reads_blanks_to(at))
        leaf = before;
      return;
    }
    if (l.hidden || l.begin == l.end) continue;
    if (!white_space_only(text.substr(l.begin, l.end - l.begin))) return;
  }
}
}  // namespace

std::atomic<std::uint32_t>* LrSampleReads::cells(std::size_t count) const {
  std::atomic<std::uint32_t>* held = _cells.load(std::memory_order_acquire);
  if (held != nullptr) return held;
  auto* const made = new std::atomic<std::uint32_t>[count]();
  if (_cells.compare_exchange_strong(held, made, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    return made;
  delete[] made;
  return held;
}

void lr_next_features_at(const LrCaretTable& table,
                         const StampedGrammar& grammar, const Cst& cst,
                         TextOffset at, TextOffset resume,
                         std::vector<NextFeature>& out,
                         const CancellationToken& cancelToken) {
  if (!StampedGrammar::caret_in(cst, at, resume)) return;
  NodeId behind = cst.leaf_after(at);
  CstLeaf leaf = read_leaf_before(cst, table.stamp, behind, nullptr);
  step_before_invented(table, cst, at, leaf, cancelToken);
  const std::string_view text = cst.text();
  bool skipped = false;
  behind = leaf ? leaf.id : 0;
  const CstLeaf pre = read_leaf_before(cst, table.stamp, behind, &skipped);
  const auto valid = [&](const CstLeaf& n) {
    return !n || (n.begin <= n.end && n.end <= at);
  };
  if (!valid(leaf) || !valid(pre)) return;
  // A stamp past the table's rows is no caret to answer.
  const std::uint16_t preState = pre ? table.stamp.state(cst[pre.id]) : 0;
  const std::uint16_t shifted = leaf ? table.stamp.state(cst[leaf.id]) : 0;
  if (preState >= table.mode.size() || shifted >= table.mode.size()) return;

  // A literal is answered only where the lexer accepts it spliced in. Nothing
  // attaches onto an indentation's or implied token's leaf; a word a repair
  // read as another is the author's, and what is typed behind it may attach.
  const SymbolId leafTerm =
      leaf && shifted < table.shiftTerm.size() ? table.shiftTerm[shifted] : 0;
  const bool attached = leaf && leaf.end == at && !table.synthetic(leafTerm);
  CaretStacks stacks(table, text, at);
  if (const unsigned tokens = stacks.phrase_tokens(); tokens != 0)
    stacks.read_through(phrase_end(cst, resume, tokens));
  // Only trivia, or a zero-width token the drive inserted, may stand between
  // the leaf and the token before it; otherwise the leaf is asked in the mode
  // that admits everything.
  bool spliceable = leaf && (!pre || pre.end == leaf.begin || !skipped);
  for (NodeId i = pre ? pre.id + 1 : 0; spliceable && i < leaf.id; ++i) {
    const CstNode& n = cst[i];
    const auto state = table.stamp.state(n);
    spliceable =
        !n.recovered && (n.subtreeSize != 1 || n.hidden ||
                         (n.begin == n.end && state < table.shiftTerm.size() &&
                          table.synthetic(table.shiftTerm[state])));
  }
  std::string splice;
  std::vector<Scan> room(table.scan != nullptr ? table.maxCands : 0);
  // What the position reads this terminal as, the only reading that makes it
  // typeable here: a guard belongs to its own acceptance, so a twin of one
  // spelling answers for itself. A refused winner is read without `skip`.
  const auto reads = [&](std::uint16_t mode, SymbolId term, TextOffset& halt,
                         std::span<const SymbolId> skip = {}) {
    const auto len = static_cast<TextOffset>(splice.size());
    if (!skip.empty())
      return table.lexWithout(splice.data(), len, mode, 0, skip, halt);
    const Scan s = table.lex(splice.data(), len, mode, 0, halt);
    if (s.term == term || room.empty()) return s;
    const std::uint32_t found =
        table.scan(splice.data(), len, mode, 0, room.data(), table.maxCands);
    for (std::uint32_t k = 0; k < found; ++k)
      if (room[k].term == term) return room[k];
    return (s.term == kInvalidSymbol || s.end == 0) && found != 0 ? room[0] : s;
  };
  const auto spliced = [&](TextOffset from, std::string_view spelling,
                           auto walk) {
    return StampedGrammar::spliced(splice, text, from, at, resume, spelling,
                                   walk);
  };
  // Attached to the leaf, the leaf's token re-lexed with the candidate behind
  // it must still end at `at` ("E" then "extends" is one identifier) and be the
  // reading followed. A head that names the leaf's mode is asked in it alone.
  const auto keeps_leaf = [&](std::string_view spelling, SymbolId reading,
                              std::uint16_t mode) {
    return spliced(leaf.begin, spelling, [&](TextOffset& halt) {
      bool ok = false;
      bool asked = false;
      const auto ask = [&](std::uint16_t m) {
        TextOffset h = 0;
        asked = true;
        // A word the repair read as another is no token of its reading: read
        // under the alphabet a fault there is lexed with, the candidate only
        // must not make one word with it.
        if (leaf.recovered) {
          const Scan s =
              table.lex(splice.data(), static_cast<TextOffset>(splice.size()),
                        table.errModeOf(m), 0, h);
          ok = ok || s.term == kInvalidSymbol || s.end <= at - leaf.begin;
          halt = std::max(halt, h);
          return;
        }
        Scan s = reads(m, reading, h);
        unsigned weight = 0;
        const auto without = [&](std::span<const SymbolId> skip, TextOffset) {
          const Scan r = reads(m, reading, h, skip);
          weight = lex_weight(r.priority);
          return Token{leaf.begin, leaf.begin + r.end, r.term};
        };
        const auto edge = [&](const Token& t) { return stacks.edge_takes(t); };
        if (Token la{leaf.begin, leaf.begin + s.end, s.term};
            s.term != reading && s.end != 0 &&
            stacks.refused(m, la, edge, without)) {
          s = Scan{la.end - leaf.begin, la.sym};
          // The reading may be one of those that tie with the new winner.
          if (la.sym != reading)
            stacks.refused_ties(m, la, weight, edge, without,
                                [&](const Token& t) {
                                  if (t.sym != reading) return true;
                                  s = Scan{t.end - leaf.begin, t.sym};
                                  return false;
                                });
        }
        if (s.term == reading && s.end == at - leaf.begin) ok = true;
        halt = std::max(halt, h);
      };
      if (mode != kNoMode)
        ask(mode);
      else
        for (const auto& read : stacks.leaf_reads())
          if (read.term == reading) ask(read.mode);
      if (!asked) ask(spliceable ? table.mode[preState] : table.errMode);
      return ok;
    });
  };
  // The states reducing on the way to the leaf's shift read it in their own
  // modes; with the candidate behind it each must read the same, or a token
  // reduced by the same rule, as `%` before `-` or `->`.
  const auto reduces = [&](std::uint16_t state, SymbolId term,
                           std::uint16_t prod) {
    if (term == kInvalidSymbol) return false;
    const auto cell = table.run(state, term);
    for (std::uint32_t k = cell.first; k < cell.first + cell.count; ++k) {
      const LrAction action = table.action(k);
      if (action.kind == LrActionKind::reduce && action.arg == prod)
        return true;
    }
    return false;
  };
  const auto passes_hold = [&](std::string_view spelling, std::uint16_t mode,
                               std::uint32_t passes) {
    return stacks.each_pass(passes, [&](std::uint16_t state,
                                        std::uint16_t prod) {
      const std::uint16_t m = table.mode[state];
      if (m == mode) return true;
      TextOffset h = 0;
      const Scan was = table.lex(
          text.data(), static_cast<TextOffset>(text.size()), m, leaf.begin, h);
      const bool none = was.term == kInvalidSymbol || was.end <= leaf.begin;
      return spliced(leaf.begin, spelling, [&](TextOffset& halt) {
        const auto len = static_cast<TextOffset>(splice.size());
        const Scan now = table.lex(splice.data(), len, m, 0, halt);
        const bool read = now.term != kInvalidSymbol && now.end != 0;
        if (read ? now.term == was.term && leaf.begin + now.end == was.end
                 : none)
          return true;
        if (reduces(state, now.term, prod)) return true;
        if (room.empty()) return false;
        const std::uint32_t found =
            table.scan(splice.data(), len, m, 0, room.data(), table.maxCands);
        for (std::uint32_t k = 0; k < found; ++k)
          if (reduces(state, room[k].term, prod)) return true;
        return false;
      });
    });
  };
  const auto fits = [&](std::string_view spelling, SymbolId reading,
                        std::uint16_t mode, std::uint32_t passes) {
    return keeps_leaf(spelling, reading, mode) &&
           passes_hold(spelling, mode, passes);
  };

  // The prefix: every record up to the caret's last token. After it, dropped
  // text and zero-width tape tokens are stepped over; an invented token is a
  // guess and is not assumed.
  const NodeId prefix = leaf ? leaf.id + 1 : 0;
  std::vector<NodeId> events;
  std::size_t dropped = 0;
  for (NodeId i = prefix; i < cst.size(); ++i) {
    const CstNode& n = cst[i];
    if (n.subtreeSize != 1 || n.hidden) continue;
    if (n.end > at) break;
    const auto state = table.stamp.state(n);
    if (!n.recovered) {
      if (n.begin != n.end) break;
      if (state < table.shiftTerm.size() &&
          table.synthetic(table.shiftTerm[state]))
        events.push_back(i);
      continue;
    }
    if (state == 0) {
      events.push_back(i);
      dropped = events.size();
    }
  }
  events.resize(dropped);

  CaretReplay replay(table, cst, stacks,
                     leaf ? jump_horizon(table, cst, leaf.id) : TextOffset{0},
                     cancelToken);
  if (!replay.run(prefix, events)) return;
  if (attached) stacks.name_readings(leafTerm);
  const NodeId held = cst.leaf_after(at);
  stacks.indentation(at, held > 0 ? cst[cst.leaf_before(held)].end : 0);
  // A comment that ends at the caret and closes on nothing goes on over what is
  // typed: `//` then `entity` is one comment. A candidate is offered where the
  // comment, read again with it behind, still ends here.
  CaretStacks::Trivia comment;
  const std::uint16_t commentMode = table.mode[shifted];
  CstLeaf before{};
  for (NodeId behind = held; behind > 0 && before.begin == before.end;)
    before = cst.as_leaf(behind = cst.leaf_before(behind));
  if (before.begin == before.end) before = {};
  if (before.hidden && !before.recovered && before.end == at) {
    TextOffset halt = 0;
    const Scan s = table.lex(text.data(), static_cast<TextOffset>(text.size()),
                             commentMode, before.begin, halt);
    if (s.term != kInvalidSymbol && s.end == before.end)
      comment = {before.begin, s};
  } else if (!before.recovered) {
    stacks.comment_before(commentMode, before ? before.end : 0, at, comment);
  }
  const auto comment_ends = [&](std::string_view spelling) {
    return spliced(comment.begin, spelling, [&](TextOffset& halt) {
      TextOffset h = 0;
      const Scan s =
          table.lex(splice.data(), static_cast<TextOffset>(splice.size()),
                    commentMode, 0, h);
      halt = h;
      // A terminal refused where the comment stands leaves it to a trivia.
      return (s.term == comment.scan.term ||
              (s.term < table.terms.size() && table.terms[s.term].trivia)) &&
             s.end == at - comment.begin;
    });
  };
  // The sample words a terminal with no spelling is read as under a mode, one
  // bit each below the bit that marks the answer known.
  constexpr std::uint32_t kKnown = 0x80000000u;
  static_assert(StampedGrammar::samples.size() < 32);
  const auto sampled_in = [&](std::uint16_t mode, SymbolId term) {
    std::atomic<std::uint32_t>* const kept =
        table.sampleReads != nullptr
            ? table.sampleReads->cells(table.modeTight.size() *
                                       table.terms.size()) +
                  std::size_t{mode} * table.terms.size() + term
            : nullptr;
    if (kept != nullptr)
      if (const std::uint32_t cached = kept->load(std::memory_order_relaxed);
          cached != 0)
        return cached & ~kKnown;
    std::uint32_t found = 0;
    for (std::size_t i = 0; i < StampedGrammar::samples.size(); ++i) {
      splice.assign(StampedGrammar::samples[i]);
      TextOffset halt = 0;
      const Scan s = reads(mode, term, halt);
      if (s.term == term && s.end == splice.size()) found |= 1u << i;
    }
    if (kept != nullptr) kept->store(found | kKnown, std::memory_order_relaxed);
    return found;
  };
  // A candidate with no spelling is typed as some word its terminal reads:
  // the comment lets it through where the word ends it, and where no sample
  // word is read the terminal is left as offered.
  const auto comment_lets_word = [&](std::uint16_t mode, SymbolId term) {
    const std::uint32_t read =
        comment.scan.term == kInvalidSymbol ? 0u : sampled_in(mode, term);
    for (std::size_t i = 0; i < StampedGrammar::samples.size(); ++i)
      if (((read >> i) & 1u) != 0 && comment_ends(StampedGrammar::samples[i]))
        return true;
    return read == 0;
  };
  // Each candidate brings its own lookahead and must reach a real shift on a
  // stack the prefix admits. Whether it attaches to the leaf is asked once per
  // leaf reading, mode and reductions.
  struct LeafCheck {
    SymbolId reading;
    std::uint16_t mode;
    std::uint32_t passes;
    bool ok;
  };
  std::vector<LeafCheck> leafChecks;
  // The shifts one candidate takes, each read once for its sites.
  std::vector<std::uint32_t> emitted;
  for (const auto term : stacks.terminals(at)) {
    const std::size_t mine = out.size();
    // The sample words the terminal reads under any mode, asked once.
    std::uint32_t anyMode = 0;
    const auto sampled = [&] {
      if (anyMode == 0) {
        anyMode = kKnown;
        for (std::size_t m = 0; m < table.modeTight.size(); ++m)
          anyMode |= sampled_in(static_cast<std::uint16_t>(m), term);
      }
      return anyMode & ~kKnown;
    };
    // The candidate typed as one of its spellings, or as the sample words its
    // terminal reads where it has none.
    const auto offer = [&](std::string_view spelling) {
      if (comment.scan.term != kInvalidSymbol && !spelling.empty() &&
          !comment_ends(spelling))
        return;
      leafChecks.clear();
      emitted.clear();
      // A terminal with no spelling is tried as the words it reads as: attached
      // to the token before the caret, a name can read as one word with it.
      const auto words_fit = [&](SymbolId reading, std::uint16_t mode,
                                 std::uint32_t passes) {
        const std::uint32_t read = sampled();
        if (read == 0) return true;
        for (std::size_t i = 0; i < StampedGrammar::samples.size(); ++i)
          if (((read >> i) & 1u) != 0 &&
              fits(StampedGrammar::samples[i], reading, mode, passes))
            return true;
        return false;
      };
      // A candidate with no spelling has its conditions asked of each sample
      // word its terminal reads, once a closure shows it asks any; a terminal
      // that reads none stays offered unchecked.
      const auto type = [&](std::string_view word) {
        stacks.next(
            term, at, resume, spelling, word,
            [&](SymbolId reading, std::uint16_t mode, std::uint32_t passes) {
              if (!attached) return true;
              for (const LeafCheck& seen : leafChecks)
                if (seen.reading == reading && seen.mode == mode &&
                    seen.passes == passes)
                  return seen.ok;
              leafChecks.push_back(
                  {reading, mode, passes,
                   spelling.empty() ? words_fit(reading, mode, passes)
                                    : fits(spelling, reading, mode, passes)});
              return leafChecks.back().ok;
            },
            [&](std::uint16_t mode, std::span<const SymbolId> skip) {
              if (spelling.empty())
                return comment_lets_word(mode, term) ? Scan{0, term}
                                                     : Scan{0, kInvalidSymbol};
              return spliced(at, spelling, [&](TextOffset& halt) {
                return reads(mode, term, halt, skip);
              });
            },
            [&](std::uint16_t st, std::uint16_t toState) {
              const std::uint32_t shift = std::uint32_t{st} << 16 | toState;
              if (std::ranges::find(emitted, shift) != emitted.end()) return;
              emitted.push_back(shift);
              const auto items = table.expectItems.begin();
              const auto end = items + table.expectBase[st + 1u];
              for (auto slot = std::lower_bound(
                       items + table.expectBase[st], end, term,
                       [&](std::uint16_t item, SymbolId t) {
                         return table.expectPool[item].feature < t;
                       });
                   slot != end && table.expectPool[*slot].feature == term;
                   ++slot) {
                const auto& e = table.expectPool[*slot];
                if (e.toState != kNoElement && e.toState != toState) continue;
                const NextFeature feature{e.feature, grammar.rowFeature(e.site),
                                          grammar.rowNarrow(e.site),
                                          grammar.rowType(e.site), e.entered};
                if (std::find(out.begin() + static_cast<std::ptrdiff_t>(mine),
                              out.end(), feature) == out.end())
                  out.push_back(feature);
              }
            });
      };
      type(spelling);
      if (spelling.empty() && table.cond != nullptr && stacks.asked()) {
        if (const std::uint32_t read = sampled(); read != 0) {
          out.resize(mine);
          emitted.clear();
          stacks.keep_answers(true);
          bool first = true;
          for (std::size_t i = 0; i < StampedGrammar::samples.size(); ++i) {
            if (((read >> i) & 1u) == 0) continue;
            const std::string_view word = StampedGrammar::samples[i];
            if (!first && stacks.unchanged(term, at, resume, word)) continue;
            first = false;
            type(word);
          }
          stacks.keep_answers(false);
        }
      }
    };
    // A terminal of several spellings is offered where one of them would be.
    const std::span<const std::string_view> spellings =
        grammar.terminalSpellings(term);
    if (spellings.empty()) offer({});
    for (const std::string_view spelling : spellings) offer(spelling);
  }
}

}  // namespace cursiv::engine
