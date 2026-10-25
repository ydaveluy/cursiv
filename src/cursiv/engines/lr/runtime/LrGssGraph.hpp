// The graph an episode is read into, the same for every grammar: vertices keyed
// by state and reading, edges packed into a forest, reductions as an agenda.
// Vertices open by position, so every vertex before the next one's is closed.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/lr/runtime/LrGssDigest.hpp"
#include "cursiv/engines/lr/runtime/LrGssForest.hpp"
#include "cursiv/engines/lr/runtime/LrGssIndex.hpp"
#include "cursiv/engines/lr/runtime/LrGssPool.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

class LrGssGraph {
 public:
  using Id = LrGssForest::Id;
  static constexpr Id none = LrGssForest::none;

  struct Reading {
    struct Run {
      std::uint32_t state = 0;
      std::uint32_t back() const { return state; }
    } run;
    TextOffset at = 0;
    TextOffset pendEnd = 0;
    Token la{};
    std::uint16_t laMode = kNoMode;
    LrVirtualAt virt;
    IndentAt indent;
  };
  struct Step {
    Reading reading;
    // The vertex that took it.
    Id vertex = none;
    std::uint32_t action = 0;
    std::uint16_t lone = kLrNoProd;
    bool scanned : 1 = false;
    bool shifts : 1 = false;
    // Laid by the parser: even a nonempty indent validates no frozen rival.
    bool laid : 1 = false;
    // Where the lexer ranked rival tokens apart, the word its token adds to a
    // derivation's rank.
    bool ranked : 1 = false;
    // Its cell's order is the election's, whatever the text goes on to read.
    bool exact : 1 = false;
    std::uint32_t lexRank = 0;
  };
  // What an election says where it cannot be told from the table: neither
  // order, and not a tie.
  static constexpr int kUnknown = 2;
  // A reduction's path from `vertex`, one edge further down per link.
  struct Pop {
    Id vertex;
    Id step;
    Id parent;
    Id edge;
    std::uint16_t prod;
    std::uint16_t left;
    Id next = none;
  };

  // The seed's stack as a private chain under `start`, the one vertex to open;
  // each frame stands for the symbol `stood`, per state, names, if any.
  void seed(const LrStack& stack, const Reading& start,
            std::span<const std::uint16_t> stood = {});

  const Reading& reading(Id v) const { return _vertices[v].reading; }
  // Calls `f` with the vertex under each edge out of `v`.
  template <class F>
  void each_below(Id v, F&& f) const {
    for (Id e = _vertices[v].edges; e != none; e = _edges[e].next)
      f(_edges[e].previous);
  }
  const Step& step(Id s) const { return _steps[s]; }
  Id below(Id edge) const { return _edges[edge].previous; }
  std::size_t alternatives() const { return _forest.alternatives(); }

  Id add(Step step) {
    const Id id = static_cast<Id>(_steps.size());
    if (step.vertex != none && _vertices[step.vertex].steps == none)
      _vertices[step.vertex].steps = id;
    _steps.push_back(std::move(step));
    return id;
  }
  void accept_at(Id v, Id step) { _accepts.emplace_back(v, step); }

  // Frozen readings with more stacks than a search takes, and the span of
  // vertices they stood on.
  [[nodiscard]] std::uint32_t cut() const { return _cut; }
  [[nodiscard]] TextOffset cut_begin() const { return _cutBegin; }
  [[nodiscard]] TextOffset cut_end() const { return _cutEnd; }
  // Stacks dropped for the order the table lays its arms in, the derivations'
  // words leaving them open and the order proved the election's.
  [[nodiscard]] std::uint32_t tabled() const { return _tabled; }
  void die(TextOffset at) {
    if (at > _lastDeath) _lastDeath = at;
    _died = true;
  }
  // `v` has no action for the token `r` holds: a frozen reading, which a
  // repair may set out from.
  void freeze(Id v, const Reading& r) {
    die(r.la.begin);
    _frozen.emplace_back(v, r);
  }

  Id vertex(Reading r);
  // `words` stand before the children `at` names: a reduction's written
  // decisions. Where `to` stands after `stands`, the pass-throughs the table
  // skipped down to `symbol` read `wrap` around the node of `symbol`, which
  // the edge of `stands` holds.
  void edge(Id to, Id from, std::uint32_t symbol, std::span<const Id> children,
            Id step, std::span<const std::uint32_t> words = {},
            std::span<const std::uint16_t> at = {}) {
    edge(to, from, symbol, children, step, words, at, symbol, {}, {});
  }
  void edge(Id to, Id from, std::uint32_t symbol, std::span<const Id> children,
            Id step, std::span<const std::uint32_t> words,
            std::span<const std::uint16_t> at, std::uint32_t stands,
            std::span<const std::uint32_t> wrap,
            std::span<const std::uint16_t> wrapAt);
  void watch(Pop pop) {
    const Id id = static_cast<Id>(_pops.size());
    _pops.push_back(pop);
    Vertex& v = _vertices[pop.vertex];
    if (v.watchers == none)
      v.watchers = id;
    else
      _pops[v.lastWatcher].next = id;
    v.lastWatcher = id;
    for (Id e = v.edges; e != none; e = _edges[e].next)
      _ready.emplace_back(id, e);
  }

  // The forests of `pop`'s path ending on `last`, in text order, and the
  // symbols they stand on; valid until the next call.
  std::span<const Id> children(const Pop& pop, Id last) {
    _children.clear();
    _childSymbols.clear();
    const auto take = [&](Id e) {
      const Edge& x = _edges[e];
      _children.push_back(x.forest);
      _childSymbols.push_back(x.symbol == kSeedSymbol ? stood(x.to)
                                                      : x.symbol);
    };
    if (last != none) take(last);
    for (Pop at = pop; at.edge != none; at = _pops[at.parent]) take(at.edge);
    return _children;
  }
  std::span<const std::uint32_t> child_symbols() const { return _childSymbols; }
  // Room for the words of one edge, valid until the next call of `edge`.
  std::vector<std::uint32_t>& word_room() { return _wordRoom; }
  std::vector<std::uint16_t>& word_at_room() { return _wordAtRoom; }

  // The next path a reduction completes, and the edge it ends on; the shorter
  // joins on the way are made. False once the agenda is empty.
  bool next_path(Pop& pop, Id& last) {
    while (!_ready.empty()) {
      const auto [p, e] = _ready.front();
      _ready.pop_front();
      const Pop at = _pops[p];
      if (at.left == 1) {
        pop = at;
        last = e;
        return true;
      }
      watch(Pop{_edges[e].previous, at.step, p, e, at.prod,
                static_cast<std::uint16_t>(at.left - 1)});
    }
    return false;
  }
  // The unopened vertex nearest the text's start, false when none is left.
  bool next_vertex(Id& v, TextOffset& at) const;
  void take_vertex();
  // None of `vertices` is ever opened: the lexer chose another token for
  // their places.
  void retire(std::span<const Id> vertices);
  // The real tokens shifted that no rival token has been compared to yet: the
  // vertex each landed on and its extent, and whether a rival from the same
  // place outlasts it.
  struct Shift {
    Id vertex;
    TextOffset begin, end;
    bool beaten = false;
  };
  std::vector<Shift>& shifts() { return _shifts; }

  // Vertices before `before` gain no edge any more; neither do their forests.
  // `last`: no more text follows, so no pop reads what a prune would drop.
  void settle(TextOffset before, bool last = false);
  // Whether `v`, alone and past every opened vertex, can take the text back:
  // its stack has one path once pruned, and that path validated the arms that
  // died. It is then the graph's one derivation.
  bool hand_back(Id v, TextOffset openedAt);
  // The derivations every accept closes; false for none.
  bool close();
  std::vector<Id> elected();

  // A frozen reading no rival read kRepairValidateShifts real tokens past, once per
  // stack under it: `forests[i]` is what stands between `vertices[i]` and
  // `vertices[i + 1]`, bottom up. In the order written first; at most `cap` per
  // reading.
  struct Source {
    Reading reading;
    std::vector<Id> vertices;
    std::vector<Id> forests;
  };
  std::vector<Source> sources(std::size_t cap);
  // The author's tokens a forest's elected derivation shifts.
  std::uint32_t tokens(Id forest);
  // The elected steps of the derivation `forests` stand for, bottom up.
  std::vector<Id> derivation(std::span<const Id> forests);

 private:
  using Rope = LrGssDigest::Rope;
  struct Vertex {
    Reading reading;
    Id edges = none;
    Id lastEdge = none;
    Id watchers = none;
    Id lastWatcher = none;
    // Down lone out-edges: where the chain ends, its length, and a hash of
    // its states; `none` until the vertex is closed and asked.
    Id chainEnd = none;
    std::uint32_t chainDepth = 0;
    std::uint64_t chainHash = 0;
    // The out-edge its best derivation stands on; `none` until a comparison
    // asks, once every vertex under it is closed.
    Id best = none;
    // The frames under it along `best`, set with it; the vertex a skew-binary
    // jump down `best` reaches; the fork steps in those frames, and the edge
    // and the rope under it they were joined from, so as not to join them
    // twice.
    std::uint32_t depth = 0;
    Id jump = none;
    Rope::Id forks = Rope::none;
    Id forksEdge = none;
    Rope::Id forksUnder = Rope::none;
    // The first of the steps taken at it, which follow one another.
    Id steps = none;
  };
  // A stack of a repair's sources is widened level by level from the top; its
  // widening ends once this many children in a row fall outside the stacks
  // kept.
  static constexpr std::size_t kMisses = 64;
  // What a seed's edge stands on: no symbol an action lays.
  static constexpr std::uint32_t kSeedSymbol = 0;
  // The symbol seed vertex `v`'s frame stands for, `kSeedSymbol` where its
  // state names none.
  std::uint32_t stood(Id v) const;
  struct Edge {
    Id previous;
    Id forest;
    Id to;
    std::uint32_t symbol;
    Id next = none;
  };
  // A step from `from` that `to` stands on, and the end of the author's token
  // it shifted, 0 for none.
  struct Arc {
    Id to;
    Id from;
    TextOffset end;
  };
  struct Candidate {
    Id end;
    std::uint32_t depth;
    std::uint64_t hash;
    std::uint32_t order;
    Id edge;
    bool dead = false;
  };

  void clear();
  Id intern(Id to, Id from, std::uint32_t symbol, bool held, bool& made);
  void queue(Id v);
  void prune(Id u);
  Id best(Id v);
  std::uint32_t frames_under(Id v) const {
    return _vertices[v].edges == none ? 0 : _vertices[v].depth;
  }
  Id best_below(Id v) const { return _edges[_vertices[v].best].previous; }
  Id jump_from(Id v) const {
    return _vertices[v].edges == none ? v : _vertices[v].jump;
  }
  Id ancestor(Id v, std::uint32_t depth) const;
  Id shared(Id x, Id y) const;
  // One stack of a comparison: the frames above the vertex both stand on,
  // bottom up, the last being the edge compared.
  struct Climb {
    Id from;
    Id edge;
    std::uint32_t base;
    std::uint32_t frames;
    Id frame(const LrGssGraph& g, std::uint32_t k) const {
      return k + 1 == frames
                 ? g._edges[edge].forest
                 : g._edges[g._vertices[g.ancestor(from, base + 1 + k)].best]
                       .forest;
    }
  };
  bool alike(Id a, Id b) const;
  int written(Id a, Id b);
  int written_lone(Id a, Id b);
  int forked(Id top, Id shared, const std::array<Climb, 2>& stacks);
  void load(std::size_t side, const Climb& stack, Id shared);
  int fork_scan(TextOffset at);
  void note_cut(Id v);
  void set_best(Id v, Id edge);
  int parted_order(Id top);
  LrGssDigest::Facts facts(Id step) const;
  int fork_order(const Step& x, const Step& y, bool& table) const;
  void chain(Id v);
  bool linear(Id v) const;
  bool validated(Id v);
  template <bool KeepVertices, class Take>
  void each_path(Id v, std::vector<Id>& forests, std::vector<Id>& next,
                 std::vector<Id>* vertices, Take&& take);
  void accept(Id v, Id step);
  TextOffset read_past();
  std::uint64_t edge_hash(Id to, Id from, std::uint32_t symbol) const;

  LrGssForest _forest;
  LrGssDigest _digest;
  std::vector<Vertex> _vertices;
  LrGssPool<Edge> _edges;
  std::vector<Step> _steps;
  LrGssPool<Pop> _pops;
  // Publishing an edge queues work; it cannot recurse into this scratch buffer.
  std::vector<Id> _children;
  std::vector<std::uint32_t> _childSymbols;
  std::vector<std::uint32_t> _wordRoom;
  std::vector<std::uint16_t> _wordAtRoom;
  LrGssIndex _index;
  LrGssIndex _edgeIndex;
  std::vector<std::pair<TextOffset, Id>> _fresh;
  std::vector<Shift> _shifts;
  std::deque<std::pair<Id, Id>> _ready;
  std::vector<std::pair<Id, Id>> _accepts;
  LrGssPool<Arc> _arcs;
  std::vector<std::array<TextOffset, kRepairValidateShifts>> _readEnds;
  TextOffset _readPast = 0;
  std::vector<std::pair<Id, Reading>> _frozen;
  std::vector<std::uint32_t> _tokens;
  // The step each stack took at a vertex, as of the comparison its round names.
  struct Stamp {
    std::uint32_t round = 0;
    Id step = none;
  };
  std::array<std::vector<Stamp>, 2> _stamps;
  // The fork ropes of the frames one scan reads, and where it reads them.
  struct Stream {
    std::span<const LrGssDigest::Rope::Id> frames;
    std::array<LrGssDigest::Rope::Id, 2> held;
    // The fork steps in the frames, the leading ones of the first not read.
    LrGssDigest::Rope::Length total = 0;
    LrGssDigest::Rope::Length skip = 0;
    std::size_t frame = 0;
    LrGssDigest::Rope::Cursor cursor;
  };
  std::array<Stream, 2> _streams;
  std::uint32_t _round = 0;
  std::uint32_t _cut = 0;
  std::uint32_t _tabled = 0;
  bool _byTable = false;
  // A comparison that drops a stack: the order of a cell only counts where it
  // is exact.
  bool _strict = false;
  TextOffset _cutBegin = 0;
  TextOffset _cutEnd = 0;
  std::span<const std::uint16_t> _stood;
  std::vector<Id> _branching;
  TextOffset _settled = 0;
  TextOffset _lastDeath = 0;
  bool _died = false;
  std::vector<Candidate> _group;
  std::vector<Id> _batch;
  std::vector<Id> _walk;
  std::vector<Id> _staged;
  std::vector<Id> _unranked;
  std::vector<std::pair<Id, Id>> _partedLeft, _partedRight;
  Id _root = none;
  std::uint32_t _seedDepth = 0;
  std::uint32_t _reopenCost = 0;
  bool _accepted = false;
};

}  // namespace cursiv
