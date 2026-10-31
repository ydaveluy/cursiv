// The episode's graph named in LrGssGraph.hpp, compiled once for every
// language: nothing here reads a table.

#include "cursiv/engines/lr/runtime/LrGssGraph.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/table/LrColdTables.hpp"

namespace cursiv {

namespace {

using Id = LrGssGraph::Id;
using Reading = LrGssGraph::Reading;

std::uint64_t mix(std::uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdull;
  return h ^ (h >> 33);
}

constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

auto key(const Reading& r) {
  return std::tuple{r.run.state,      r.at,          r.pendEnd,
                    r.la.begin,       r.la.end,      r.la.sym,
                    r.laMode,         r.virt.insertedAt, r.virt.from,
                    r.virt.inserted,  r.virt.pending, r.indent.head,
                    r.indent.depth};
}

std::uint64_t hash(const Reading& r) {
  std::uint64_t h = std::uint64_t{r.run.state} << 32 | r.at;
  h = (h ^ (std::uint64_t{r.pendEnd} << 32 | r.la.begin)) * kGolden;
  h = (h ^ (std::uint64_t{r.la.end} << 32 | std::uint64_t{r.la.sym} << 16 |
            r.laMode)) *
      kGolden;
  h = (h ^ (std::uint64_t{r.virt.insertedAt} << 32 | r.virt.from)) * kGolden;
  h = (h ^ (std::uint64_t{r.virt.inserted} << 32 | r.virt.pending)) * kGolden;
  h = (h ^ (std::uint64_t{r.indent.head} << 32 | r.indent.depth)) * kGolden;
  return mix(h);
}

// Stable, and safe for an order that owes no transitivity: a neighbour moves
// ahead of another only where it is earlier than it.
template <class T, class Earlier>
void merge_sort(std::vector<T>& v, Earlier&& earlier) {
  std::vector<T> out(v.size());
  for (std::size_t width = 1; width < v.size(); width *= 2) {
    for (std::size_t lo = 0; lo < v.size(); lo += 2 * width) {
      const std::size_t mid = std::min(lo + width, v.size());
      const std::size_t hi = std::min(lo + 2 * width, v.size());
      std::size_t i = lo, j = mid, k = lo;
      while (i != mid && j != hi)
        out[k++] = std::move(earlier(v[j], v[i]) ? v[j++] : v[i++]);
      while (i != mid) out[k++] = std::move(v[i++]);
      while (j != hi) out[k++] = std::move(v[j++]);
    }
    v.swap(out);
  }
}

}  // namespace

void LrGssGraph::clear() {
  _forest.clear();
  _index.clear();
  _edgeIndex.clear();
  _vertices.clear();
  _edges.clear();
  _steps.clear();
  _pops.clear();
  _children.clear();
  _fresh.clear();
  _ready.clear();
  _accepts.clear();
  _arcs.clear();
  _readEnds.clear();
  _frozen.clear();
  _tokens.clear();
  _digest.clear();
  _cut = 0;
  _tabled = 0;
  _cutBegin = 0;
  _cutEnd = 0;
  _branching.clear();
  _settled = 0;
  _shifts.clear();
  _lastDeath = 0;
  _died = false;
  _held = none;
  _owed = 0;
  _root = none;
  _seedDepth = 0;
  _seedBase = 0;
  _underflow = false;
  _accepted = false;
}

void LrGssGraph::seed(const LrStack& stack, const Reading& start,
                      std::span<const std::uint16_t> stood,
                      std::uint32_t window) {
  clear();
  _stood = stood;
  _seedBase = stack.size() > window ? stack.size() - window : 0;
  _seedDepth = stack.size() - _seedBase;
  Id previous = none;
  for (std::uint32_t i = _seedBase; i < stack.size(); ++i) {
    Reading r;
    r.run.state = stack[i].state;
    r.at = stack[i].begin;
    const auto v = static_cast<Id>(_vertices.size());
    _vertices.push_back(Vertex{r});
    if (previous != none) {
      const Id f = _forest.node();
      _forest.add(f, {}, none, {});
      _edges.push_back(Edge{previous, f, v, kSeedSymbol});
      _vertices[v].edges = _vertices[v].lastEdge =
          static_cast<Id>(_edges.size() - 1);
    }
    previous = v;
  }
  _vertices[previous].reading = start;
  _index.intern(hash(start), previous, [](Id) { return false; });
  queue(previous);
  _root = _forest.node();
}

std::uint32_t LrGssGraph::stood(Id v) const {
  if (_stood.empty()) return kSeedSymbol;
  const std::uint16_t s = _stood[_vertices[v].reading.run.state];
  return s == kLrNoStateSym ? kSeedSymbol : s;
}

Id LrGssGraph::vertex(Reading r) {
  r.virt = r.virt.seen_from(r.at);
  const auto k = key(r);
  const auto fresh = static_cast<Id>(_vertices.size());
  const Id id = _index.intern(
      hash(r), fresh, [&](Id at) { return key(_vertices[at].reading) == k; },
      [&](Id at) { return _vertices[at].reading.at < _settled; });
  if (id != fresh) return id;
  _vertices.push_back(Vertex{r});
  queue(id);
  return id;
}

void LrGssGraph::queue(Id v) {
  _fresh.emplace_back(_vertices[v].reading.at, v);
  std::ranges::push_heap(_fresh, std::greater<>{});
}

bool LrGssGraph::next_vertex(Id& v, TextOffset& at) const {
  if (_fresh.empty()) return false;
  at = _fresh.front().first;
  v = _fresh.front().second;
  return true;
}

void LrGssGraph::retire(std::span<const Id> vertices) {
  _batch.assign(vertices.begin(), vertices.end());
  std::ranges::sort(_batch);
  const auto gone = std::ranges::remove_if(
      _fresh, [this](const std::pair<TextOffset, Id>& e) {
        return std::ranges::binary_search(_batch, e.second);
      });
  if (gone.empty()) return;
  _fresh.erase(gone.begin(), gone.end());
  std::ranges::make_heap(_fresh, std::greater<>{});
}

void LrGssGraph::take_vertex() {
  std::ranges::pop_heap(_fresh, std::greater<>{});
  _fresh.pop_back();
}

std::uint64_t LrGssGraph::edge_hash(Id to, Id from,
                                    std::uint32_t symbol) const {
  return mix((std::uint64_t{to} << 32 | from) ^
             std::uint64_t{symbol} * kGolden);
}

// The edge `to` stands on over `from` by `symbol`, made where none is: one
// `held` back under another is laid in no vertex's list.
Id LrGssGraph::intern(Id to, Id from, std::uint32_t symbol, bool held,
                      bool& made) {
  const auto h = edge_hash(to, from, symbol);
  const auto fresh = static_cast<Id>(_edges.size());
  const Id e = _edgeIndex.intern(
      h, fresh,
      [&](Id at) {
        const Edge& x = _edges[at];
        return x.to == to && x.previous == from && x.symbol == symbol;
      },
      [&](Id at) { return _vertices[_edges[at].to].reading.at < _settled; });
  made = e == fresh;
  if (!made) return e;
  const Id f = _forest.node();
  _edges.push_back(Edge{from, f, to, symbol});
  if (held) return e;
  Vertex& v = _vertices[to];
  if (v.edges == none) {
    v.edges = e;
  } else {
    if (v.edges == v.lastEdge) _branching.push_back(to);
    _edges[v.lastEdge].next = e;
  }
  v.lastEdge = e;
  // Published pairs name existing watchers; later watchers take existing edges.
  for (Id p = v.watchers; p != none; p = _pops[p].next)
    _ready.emplace_back(p, e);
  return e;
}

void LrGssGraph::edge(Id to, Id from, std::uint32_t symbol,
                      std::span<const Id> children, Id step,
                      std::span<const std::uint32_t> words,
                      std::span<const std::uint16_t> at, std::uint32_t stands,
                      std::span<const std::uint32_t> wrap,
                      std::span<const std::uint16_t> wrapAt) {
  bool made = false;
  Id f;
  if (stands == symbol) {
    f = _edges[intern(to, from, symbol, false, made)].forest;
  } else {
    f = _edges[intern(to, from, symbol, true, made)].forest;
    if (made) {
      bool outer = false;
      const Id up = _edges[intern(to, from, stands, false, outer)].forest;
      const std::array<Id, 1> under{f};
      _forest.add(up, under, none, wrap, wrapAt);
    }
  }
  const Step& s = _steps[step];
  const bool token = s.shifts && !s.laid &&
                     s.reading.la.begin < s.reading.la.end;
  if (words.empty() && s.shifts && s.ranked)
    _forest.add(f, children, step, std::span(&s.lexRank, 1), {},
                token ? LrGssForest::Kind::ranked : LrGssForest::Kind::inner);
  else
    _forest.add(f, children, step, words, at,
                token ? LrGssForest::Kind::token : LrGssForest::Kind::inner);
  const Id under = s.shifts ? from : s.vertex;
  _arcs.push_back(Arc{to, under, token ? s.reading.la.end : 0});
  if (_held != none) {
    const auto ahead = static_cast<std::uint8_t>(
        std::min(_vertices[under].ahead + std::uint32_t{token},
                 kRepairValidateShifts));
    if (ahead > _vertices[to].ahead) _vertices[to].ahead = ahead;
  }
}

void LrGssGraph::settle(TextOffset before, bool last) {
  if (before <= _settled) return;
  _settled = before;
  std::size_t kept = 0;
  _batch.clear();
  for (const Id u : _branching) {
    if (_vertices[u].reading.at < before)
      _batch.push_back(u);
    else
      _branching[kept++] = u;
  }
  _branching.resize(kept);
  std::ranges::sort(_batch, [&](Id a, Id b) {
    return std::pair(_vertices[a].reading.at, a) <
           std::pair(_vertices[b].reading.at, b);
  });
  if (!last)
    for (const Id u : _batch) prune(u);
}

// Two out-edges whose chains down share states and end hold stacks with one
// future: all but the one whose words order first are dropped. Only edges of
// one symbol compare, as a pass-through the table skipped is not in their
// words. `u` must be closed, and so all it reaches.
void LrGssGraph::prune(Id u) {
  if (_vertices[u].edges == _vertices[u].lastEdge) return;
  _group.clear();
  std::uint32_t order = 0;
  for (Id e = _vertices[u].edges; e != none; e = _edges[e].next) {
    const Id w = _edges[e].previous;
    chain(w);
    const Vertex& t = _vertices[w];
    _group.push_back(
        {t.chainEnd, t.chainDepth,
         mix((t.chainHash ^ std::uint64_t{_edges[e].symbol} << 32) * kGolden),
         order++, e});
  }
  const auto key = [](const Candidate& c) {
    return std::tie(c.end, c.depth, c.hash);
  };
  std::ranges::sort(_group, [&](const Candidate& a, const Candidate& b) {
    return std::tuple_cat(key(a), std::tie(a.order)) <
           std::tuple_cat(key(b), std::tie(b.order));
  });
  bool dropped = false;
  for (std::size_t i = 0; i < _group.size();) {
    std::size_t j = i + 1;
    while (j < _group.size() && key(_group[j]) == key(_group[i])) ++j;
    for (std::size_t best = i, k = i + 1; k < j; ++k) {
      if (!alike(_group[best].edge, _group[k].edge)) continue;
      _strict = true;
      const int first = written_lone(_group[best].edge, _group[k].edge);
      _strict = false;
      if (first == 0 || first == kUnknown) continue;
      if (_byTable) ++_tabled;
      dropped = true;
      _group[first < 0 ? k : best].dead = true;
      if (first > 0) best = k;
    }
    i = j;
  }
  if (!dropped) return;
  std::ranges::sort(_group, {}, &Candidate::order);
  Id head = none, tail = none;
  for (const Candidate& c : _group) {
    if (c.dead) continue;
    if (tail == none)
      head = c.edge;
    else
      _edges[tail].next = c.edge;
    tail = c.edge;
  }
  _edges[tail].next = none;
  _vertices[u].edges = head;
  _vertices[u].lastEdge = tail;
}

// Whether two edges over lone chains that end at one vertex, and as deep, lay
// the same symbols over the same states: the hash tells where to look, never
// that they do.
bool LrGssGraph::alike(Id a, Id b) const {
  if (_edges[a].symbol != _edges[b].symbol) return false;
  Id x = _edges[a].previous, y = _edges[b].previous;
  const Id end = _vertices[x].chainEnd;
  while (x != end) {
    if (y == end ||
        _vertices[x].reading.run.state != _vertices[y].reading.run.state)
      return false;
    const Edge &ex = _edges[_vertices[x].edges],
               &ey = _edges[_vertices[y].edges];
    if (ex.symbol != ey.symbol) return false;
    x = ex.previous;
    y = ey.previous;
  }
  return y == end;
}

// `written` for two edges whose stacks are lone chains down to one vertex. The
// chains are ranked for this comparison alone, that vertex their floor, so
// that nothing under it is compared.
int LrGssGraph::written_lone(Id a, Id b) {
  const Id x = _edges[a].previous, y = _edges[b].previous;
  const Id floor = _vertices[x].chainEnd;
  Vertex& end = _vertices[floor];
  const bool based = end.edges != none && end.best == none;
  if (based) {
    end.depth = 0;
    end.jump = floor;
  }
  _staged.clear();
  for (const Id side : {x, y}) {
    const std::size_t from = _staged.size();
    for (Id v = side; v != floor && _vertices[v].best == none;
         v = _edges[_vertices[v].edges].previous)
      _staged.push_back(v);
    for (std::size_t i = _staged.size(); i-- > from;)
      set_best(_staged[i], _vertices[_staged[i]].edges);
  }
  const int first = written(a, b);
  for (const Id v : _staged) {
    Vertex& w = _vertices[v];
    w.best = none;
    w.depth = 0;
    w.jump = none;
  }
  if (based) {
    end.depth = 0;
    end.jump = none;
  }
  return first;
}

void LrGssGraph::chain(Id v) {
  _walk.clear();
  for (Id at = v; _vertices[at].chainEnd == none;) {
    Vertex& x = _vertices[at];
    assert(x.reading.at < _settled);
    if (x.edges == none || x.edges != x.lastEdge) {
      x.chainEnd = at;
      break;
    }
    _walk.push_back(at);
    at = _edges[x.edges].previous;
  }
  for (auto at = _walk.rbegin(); at != _walk.rend(); ++at) {
    Vertex& x = _vertices[*at];
    const Edge& e = _edges[x.edges];
    const Vertex& below = _vertices[e.previous];
    const bool ends = below.chainEnd == e.previous;
    x.chainEnd = below.chainEnd;
    x.chainDepth = (ends ? 0 : below.chainDepth) + 1;
    x.chainHash = mix(((ends ? 0 : below.chainHash) ^ x.reading.run.state ^
                       std::uint64_t{e.symbol} << 32) *
                      kGolden);
  }
}

// Every vertex under `v` asked first, its out-edges compared then in the
// order written: the stack under the best edge of each is its own best.
Id LrGssGraph::best(Id v) {
  _unranked.assign(1, v);
  while (!_unranked.empty()) {
    const Id x = _unranked.back();
    if (_vertices[x].edges == none || _vertices[x].best != none) {
      _unranked.pop_back();
      continue;
    }
    const std::size_t waiting = _unranked.size();
    for (Id e = _vertices[x].edges; e != none; e = _edges[e].next) {
      const Vertex& y = _vertices[_edges[e].previous];
      if (y.edges != none && y.best == none)
        _unranked.push_back(_edges[e].previous);
    }
    if (_unranked.size() != waiting) continue;
    _unranked.pop_back();
    Id b = _vertices[x].edges;
    for (Id e = _edges[b].next; e != none; e = _edges[e].next)
      if (written(e, b) < 0) b = e;
    set_best(x, b);
  }
  return _vertices[v].best;
}

void LrGssGraph::set_best(Id v, Id edge) {
  Vertex& x = _vertices[v];
  const Id under = _edges[edge].previous;
  const std::uint32_t depth = frames_under(under);
  const Id jump = jump_from(under);
  x.best = edge;
  x.depth = depth + 1;
  const Id beyond = jump_from(jump);
  x.jump = depth - frames_under(jump) == frames_under(jump) - frames_under(beyond)
               ? beyond
               : under;
  const Rope::Id below = _vertices[under].forks;
  if (x.forksEdge != edge || x.forksUnder != below) {
    x.forks = _digest.join(below,
                           _digest.of(_forest, _edges[edge].forest,
                                      [this](Id step) { return facts(step); }));
    x.forksEdge = edge;
    x.forksUnder = below;
  }
}

void LrGssGraph::note_cut(Id v) {
  const TextOffset at = _vertices[v].reading.at;
  if (_cut++ == 0 || at < _cutBegin) _cutBegin = at;
  if (at > _cutEnd) _cutEnd = at;
}

LrGssDigest::Facts LrGssGraph::facts(Id step) const {
  const Id v = _steps[step].vertex;
  if (v == none) return {0, false};
  const bool fork = (step != 0 && _steps[step - 1].vertex == v) ||
                    (step + 1 < _steps.size() && _steps[step + 1].vertex == v);
  return {_vertices[v].reading.at, fork};
}

LrGssGraph::Id LrGssGraph::ancestor(Id v, std::uint32_t depth) const {
  while (frames_under(v) > depth) {
    const Id jump = jump_from(v);
    v = frames_under(jump) >= depth ? jump : best_below(v);
  }
  return v;
}

// The vertex the best stacks under `x` and `y` both stand on.
LrGssGraph::Id LrGssGraph::shared(Id x, Id y) const {
  const std::uint32_t dx = frames_under(x), dy = frames_under(y);
  if (dx > dy)
    x = ancestor(x, dy);
  else if (dy > dx)
    y = ancestor(y, dx);
  while (x != y) {
    if (jump_from(x) != jump_from(y)) {
      x = jump_from(x);
      y = jump_from(y);
    } else {
      x = best_below(x);
      y = best_below(y);
    }
  }
  return x;
}

// Two out-edges of one vertex, each over the best stack under it, down to the
// vertex both stacks stand on. -1 where the first stands first in the order
// written, 1 where the second does, 0 where neither. What was read compares
// in text order; where that leaves them open, the cell the two parted at
// first forks them in the order the table lays, the grammar's written order.
int LrGssGraph::written(Id a, Id b) {
  _byTable = false;
  const Id x = _edges[a].previous, y = _edges[b].previous;
  const Id c = shared(x, y);
  const std::uint32_t base = frames_under(c);
  const std::array<Climb, 2> stacks{
      Climb{x, a, base, frames_under(x) - base + 1},
      Climb{y, b, base, frames_under(y) - base + 1}};
  if (stacks[0].frames == stacks[1].frames) {
    for (std::uint32_t k = 0; k != stacks[0].frames; ++k) {
      const Id left = stacks[0].frame(*this, k);
      const Id right = stacks[1].frame(*this, k);
      (void)_forest.elect(left);
      (void)_forest.elect(right);
      const int v = _forest.ordered_frame(left, right);
      if (v == -1 || v == 1) return v;
      if (v != 0) break;
    }
  }
  return forked(_edges[a].to, c, stacks);
}

// The forks the two stacks took, where the comparison in text order leaves
// them open. Before `top`'s position they are the same or are read earliest
// first; the steps they end on are left to `parted_order`.
int LrGssGraph::forked(Id top, Id c, const std::array<Climb, 2>& stacks) {
  const TextOffset at = _vertices[top].reading.at;
  const auto of = [this](Id step) { return facts(step); };
  for (int side = 0; side != 2; ++side) {
    auto& ends = side == 0 ? _partedLeft : _partedRight;
    ends.clear();
    const auto take = [&](Id forest) {
      const Rope::Id rope = _digest.of(_forest, forest, of);
      if (rope == Rope::none) return;
      const Rope& forks = _digest.forks();
      const auto cut = forks.first_where(
          rope, [at](const LrGssForkAgg& a) { return a.at >= at; });
      Rope::Cursor cur;
      for (cur.seek(forks, rope, cut); !cur.done(); cur.next())
        ends.emplace_back(_steps[cur.value()].vertex, cur.value());
    };
    take(_edges[stacks[side].edge].forest);
    Id w = stacks[side].from;
    while (w != c && _vertices[w].reading.at == at) {
      take(_edges[_vertices[w].best].forest);
      w = best_below(w);
    }
  }
  load(0, stacks[0], c);
  load(1, stacks[1], c);
  if (const int first = fork_scan(at); first != 0) return first;
  std::ranges::sort(_partedLeft);
  std::ranges::sort(_partedRight);
  return parted_order(top);
}

// The fork steps of a stack above those it shares with the other, as
// `fork_scan` reads them: its best stack's, past the vertex both stand on,
// then its top frame's.
void LrGssGraph::load(std::size_t side, const Climb& stack, Id shared) {
  Stream& s = _streams[side];
  std::size_t count = 0;
  s.total = 0;
  for (const Rope::Id rope :
       {_vertices[stack.from].forks,
        _digest.of(_forest, _edges[stack.edge].forest,
                   [this](Id step) { return facts(step); })})
    if (rope != Rope::none) {
      s.held[count++] = rope;
      s.total += _digest.forks().length(rope);
    }
  s.frames = std::span(s.held).first(count);
  s.skip = _digest.forks().length(_vertices[shared].forks);
}

// The two streams' fork steps before `at`, read in the order of their
// positions: the first vertex both took a step at, and took a different one,
// parts them. What both took alike is not read: the scan starts at the first
// step the streams do not share.
int LrGssGraph::fork_scan(TextOffset at) {
  const Rope& forks = _digest.forks();
  const std::array<Rope::Length, 2> left{_streams[0].total - _streams[0].skip,
                                         _streams[1].total - _streams[1].skip};
  const Rope::Length shared =
      forks.common(_streams[0].frames, _streams[0].skip, _streams[1].frames,
                   _streams[1].skip, std::min(left[0], left[1]));
  bool before = false;
  for (Stream& s : _streams) {
    Rope::Length index = s.skip + shared;
    s.frame = 0;
    while (s.frame != s.frames.size() &&
           index >= forks.length(s.frames[s.frame]))
      index -= forks.length(s.frames[s.frame++]);
    if (s.frame == s.frames.size()) continue;
    s.cursor.seek(forks, s.frames[s.frame], index);
    before = before || s.cursor.agg().at < at;
  }
  if (!before) return 0;
  std::array<Id, 2> held{none, none};
  std::array<TextOffset, 2> heldAt{};
  std::array<bool, 2> over{};
  if (++_round == 0) {
    for (auto& stamps : _stamps) std::ranges::fill(stamps, Stamp{});
    _round = 1;
  }
  for (auto& stamps : _stamps)
    if (stamps.size() < _vertices.size()) stamps.resize(_vertices.size());
  const auto read = [&](int side) {
    Stream& s = _streams[side];
    while (held[side] == none && !over[side]) {
      if (s.frame == s.frames.size()) {
        over[side] = true;
      } else if (s.cursor.done()) {
        if (++s.frame != s.frames.size())
          s.cursor.seek(forks, s.frames[s.frame], 0);
      } else {
        const Id step = s.cursor.value();
        const TextOffset when = s.cursor.agg().at;
        s.cursor.next();
        if (when >= at) {
          over[side] = true;
        } else {
          held[side] = step;
          heldAt[side] = when;
        }
      }
    }
  };
  int verdict = 0;
  bool byTable = false;
  Id part = none;
  TextOffset partAt = 0;
  for (;;) {
    read(0);
    read(1);
    if (held[0] == none && held[1] == none) break;
    const int side =
        held[0] == none ? 1 : held[1] == none ? 0 : heldAt[1] < heldAt[0];
    const Id step = held[side];
    const TextOffset when = heldAt[side];
    held[side] = none;
    if (part != none && when > partAt) break;
    const Id vertex = _steps[step].vertex;
    _stamps[side][vertex] = {_round, step};
    const Stamp& other = _stamps[1 - side][vertex];
    if (other.round != _round || other.step == step) continue;
    bool table = false;
    const int first = fork_order(_steps[side == 0 ? step : other.step],
                                 _steps[side == 0 ? other.step : step], table);
    if (first != 0 && (part == none || vertex < part)) {
      verdict = first;
      byTable = table;
      part = vertex;
      partAt = when;
    }
  }
  _byTable = byTable;
  return verdict;
}

// `written` past the comparison in text order, over `_partedLeft` and
// `_partedRight`.
int LrGssGraph::parted_order(Id top) {
  int verdict = 0;
  bool byTable = false;
  TextOffset first = 0;
  auto from = _partedRight.begin();
  for (const auto& [v, sa] : _partedLeft) {
    while (from != _partedRight.end() && from->first < v) ++from;
    for (auto at = from; at != _partedRight.end() && at->first == v; ++at) {
      if (at->second == sa) continue;
      bool table = false;
      const int w = fork_order(_steps[sa], _steps[at->second], table);
      const TextOffset where = _vertices[v].reading.at;
      if (w != 0 && (verdict == 0 || where < first)) {
        verdict = w;
        byTable = table;
        first = where;
      }
    }
  }
  _byTable = byTable;
  if (verdict != 0 || top == none || _vertices[top].steps == none)
    return verdict;
  _byTable = true;
  // One stack was made at the top by an arm the other has yet to take: it
  // stands first where that arm is the cell's first.
  const auto made = [&](const std::vector<std::pair<Id, Id>>& p) {
    const auto at = std::ranges::lower_bound(p, std::pair{top, Id{0}});
    return at != p.end() && at->first == top ? at->second : none;
  };
  const Id ml = made(_partedLeft), mr = made(_partedRight);
  if ((ml == none) == (mr == none)) return 0;
  const Id taken = ml != none ? ml : mr;
  const Step& m = _steps[taken];
  bool firstArm = true;
  for (Id st = _vertices[top].steps;
       st < _steps.size() && _steps[st].vertex == top; ++st) {
    if (st == taken || _steps[st].reading.la.sym != m.reading.la.sym) continue;
    bool table;
    const int o = fork_order(_steps[st], m, table);
    if (o == kUnknown) return kUnknown;
    if (o < 0) firstArm = false;
  }
  return (ml != none) == firstArm ? -1 : 1;
}

// Two steps taken at one vertex: the candidate the lexer ranks first, then
// the arm the table lays first. Candidates of one rank are the grammar's to
// order, which only their derivations' words do.
int LrGssGraph::fork_order(const Step& x, const Step& y, bool& table) const {
  table = false;
  if (x.reading.la.sym != y.reading.la.sym)
    return x.lexRank == y.lexRank ? 0 : x.lexRank < y.lexRank ? -1 : 1;
  table = true;
  if (_strict && !(x.exact && y.exact)) return kUnknown;
  if (x.lone != y.lone) return x.lone < y.lone ? -1 : 1;
  if (x.action != y.action) return x.action < y.action ? -1 : 1;
  return 0;
}

bool LrGssGraph::linear(Id v) const {
  for (std::size_t n = 0; n != _vertices.size(); ++n) {
    // Only the seed's top is interned; its lower frames stay a private chain.
    if (v + 1u < _seedDepth) return true;
    const Id e = _vertices[v].edges;
    if (e == none) return true;
    if (_edges[e].next != none) return false;
    v = _edges[e].previous;
  }
  return false;
}

// A dead arm is a frozen reading, a repair's source until a rival reads
// `kRepairValidateShifts` real tokens past it: the real tokens `v`'s path
// shifted past the last one, up to that many. `v`'s stack must be linear.
std::uint32_t LrGssGraph::validated(Id v) {
  if (!_died) return kRepairValidateShifts;
  std::uint32_t read = 0;
  for (Id x = v; _vertices[x].edges != none &&
                 _vertices[x].reading.at > _lastDeath &&
                 read < kRepairValidateShifts;
       x = _edges[_vertices[x].edges].previous)
    _forest.each_step_back(_edges[_vertices[x].edges].forest, [&](Id s) {
      const Token& t = _steps[s].reading.la;
      if (!_steps[s].shifts || _steps[s].laid || t.begin == t.end) return true;
      if (t.end <= _lastDeath) return false;
      return ++read < kRepairValidateShifts;
    });
  return read;
}

// A dead arm is a repair's source until a rival validates it, so the text
// comes back only past that. A vertex alone whose own path did not validate
// the arms is held: the vertices past it count the real tokens their
// derivations shifted from it, and the first opened with as many as it owed
// gives the text back at it, its stack still one path. What the graph read
// past it is dropped: the drive reads that text again and decides its cells
// itself.
bool LrGssGraph::hand_back(Id v, TextOffset openedAt) {
  bool alone = _fresh.size() == 1 && _accepts.empty() &&
               _vertices[v].reading.at > openedAt;
  if (alone) {
    prune(v);
    alone = linear(v);
  }
  const std::uint32_t read = alone ? validated(v) : 0;
  Id back = none;
  if (read >= kRepairValidateShifts)
    back = v;
  else if (_held != none && v > _held && _accepts.empty() &&
           _vertices[v].ahead >= _owed &&
           _vertices[_held].edges == _vertices[_held].lastEdge)
    back = _held;
  if (back != none) {
    accept(back, none);
    return true;
  }
  if (alone) {
    _held = v;
    _owed = kRepairValidateShifts - read;
    _vertices[v].ahead = 0;
  }
  return false;
}

std::vector<LrGssGraph::Id> LrGssGraph::elected() {
  return _forest.steps(_root);
}

bool LrGssGraph::close() {
  settle(std::numeric_limits<TextOffset>::max(), true);
  for (const auto& [v, s] : _accepts) accept(v, s);
  return _accepted;
}

// Paths down from `v` in edge order, without recursion; `take` returns false
// to stop.
template <bool KeepVertices, class Take>
void LrGssGraph::each_path(Id v, std::vector<Id>& forests,
                           std::vector<Id>& next, std::vector<Id>* vertices,
                           Take&& take) {
  forests.clear();
  next.assign(1, _vertices[v].edges);
  if constexpr (KeepVertices) vertices->assign(1, v);
  if (next.back() == none) {
    (void)take();
    return;
  }
  while (!next.empty()) {
    Id& e = next.back();
    if (e == none) {
      next.pop_back();
      if (!forests.empty()) forests.pop_back();
      if constexpr (KeepVertices) vertices->pop_back();
      continue;
    }
    const Id taken = e;
    e = _edges[taken].next;
    forests.push_back(_edges[taken].forest);
    const Id below = _edges[taken].previous;
    if constexpr (KeepVertices) vertices->push_back(below);
    if (_vertices[below].edges != none) {
      next.push_back(_vertices[below].edges);
      continue;
    }
    if (!take()) return;
    forests.pop_back();
    if constexpr (KeepVertices) vertices->pop_back();
  }
}

// One root alternative per path down from `v`.
void LrGssGraph::accept(Id v, Id step) {
  std::vector<Id> forests, next;
  each_path<false>(v, forests, next, nullptr, [&] {
    _forest.add(_root, std::vector<Id>(forests.rbegin(), forests.rend()), step,
                {});
    _accepted = true;
    return true;
  });
}

// The furthest position a derivation read kRepairValidateShifts real tokens past:
// the ends of each vertex's last tokens, the most over its derivations, held
// until no step moves them. Those ends only grow until `clear()`, so the
// answer never falls between calls.
TextOffset LrGssGraph::read_past() {
  using Ends = std::array<TextOffset, kRepairValidateShifts>;
  auto& ends = _readEnds;
  ends.resize(_vertices.size(), Ends{});
  for (bool moved = true; moved;) {
    moved = false;
    for (std::size_t i = 0; i != _arcs.size(); ++i) {
      const Arc& a = _arcs[i];
      Ends e = ends[a.from];
      if (a.end != 0) {
        std::shift_right(e.begin(), e.end(), 1);
        e[0] = a.end;
      }
      for (std::size_t k = 0; k < e.size(); ++k)
        if (e[k] > ends[a.to][k]) {
          ends[a.to][k] = e[k];
          moved = true;
        }
    }
  }
  TextOffset most = 0;
  for (const Ends& e : ends) most = std::max(most, e.back());
  return most;
}

std::vector<LrGssGraph::Source> LrGssGraph::sources(std::size_t cap) {
  const TextOffset past = read_past();
  std::vector<Source> out;
  if (cap == 0) return out;
  // A stack with its frames elected and the fork steps below each, once
  // however many stacks it is compared with.
  struct Ranked {
    Source source;
    // Per frame: the fork steps below it, and where the fork ropes of the
    // frames from it on begin among `ropes`, those of the frames that took a
    // fork.
    std::vector<Rope::Length> steps;
    std::vector<std::uint32_t> from;
    std::vector<Rope::Id> ropes;
  };
  // A candidate's upper edges are fixed; its `free` lower edges are the best
  // completion below them. Splitting at the first edge that differs partitions
  // the remaining paths without visiting every stack under a frozen reading.
  struct Candidate {
    Ranked ranked;
    std::size_t free;
  };
  const auto of = [this](Id step) { return facts(step); };
  const auto ranked = [&](Source s) {
    std::vector<Rope::Length> steps(1, 0);
    std::vector<std::uint32_t> from;
    std::vector<Rope::Id> ropes;
    for (const Id f : s.forests) {
      (void)_forest.elect(f);
      from.push_back(static_cast<std::uint32_t>(ropes.size()));
      Rope::Length taken = 0;
      if (const Rope::Id rope = _digest.of(_forest, f, of);
          rope != Rope::none) {
        ropes.push_back(rope);
        taken = _digest.forks().length(rope);
      }
      steps.push_back(steps.back() + taken);
    }
    from.push_back(static_cast<std::uint32_t>(ropes.size()));
    return Ranked{std::move(s), std::move(steps), std::move(from),
                  std::move(ropes)};
  };
  // Two stacks compare above the frames they share: by their frames, then by
  // the first fork they took apart, the whole of both read. Alike, the edges'
  // insertion order, top down, parts them: two stacks never tie.
  const auto earlier = [&](const Ranked& x, const Ranked& y) {
    const Source& a = x.source;
    const Source& b = y.source;
    std::size_t k = 0;
    while (k < a.forests.size() && k < b.forests.size() &&
           a.forests[k] == b.forests[k] &&
           a.vertices[k + 1] == b.vertices[k + 1])
      ++k;
    const auto left = std::span(a.forests).subspan(k);
    const auto right = std::span(b.forests).subspan(k);
    int v = left.size() == right.size() ? _forest.ordered(left, right) : 0;
    if (v == LrGssForest::kOpen) v = 0;
    if (v == 0 &&
        (x.steps.back() != x.steps[k] || y.steps.back() != y.steps[k])) {
      for (std::size_t side = 0; side != 2; ++side) {
        const Ranked& r = side == 0 ? x : y;
        Stream& s = _streams[side];
        s.frames = std::span(r.ropes).subspan(r.from[k]);
        s.total = r.steps.back() - r.steps[k];
        s.skip = 0;
      }
      v = fork_scan(std::numeric_limits<TextOffset>::max());
    }
    if (v != 0) return v < 0;
    return std::lexicographical_compare(a.forests.rbegin(), a.forests.rend(),
                                        b.forests.rbegin(), b.forests.rend());
  };
  const auto completion = [&](Id v, const Reading& r) {
    (void)best(v);
    Source s{r, {v}, {}};
    while (_vertices[v].edges != none) {
      const Edge& e = _edges[_vertices[v].best];
      s.forests.push_back(e.forest);
      v = e.previous;
      s.vertices.push_back(v);
    }
    std::ranges::reverse(s.vertices);
    std::ranges::reverse(s.forests);
    return s;
  };
  std::vector<Id> path, via, next;
  std::vector<Ranked> found;
  std::vector<Candidate> pending;
  // Whether `c` is kept among the best `room` stacks pending.
  const auto put = [&](Candidate c, std::size_t room) {
    if (pending.size() >= room && room != 0 &&
        !earlier(c.ranked, pending.back().ranked))
      return false;
    const auto at = std::ranges::partition_point(
        pending,
        [&](const Candidate& x) { return !earlier(c.ranked, x.ranked); });
    pending.insert(at, std::move(c));
    if (pending.size() > room) pending.pop_back();
    return true;
  };
  for (const auto& [v, r] : _frozen) {
    if (past > r.la.begin) continue;
    // Most frozen readings have very few stacks. Keep their direct walk; an
    // extra path detects overflow before any ranked stack is discarded.
    const std::size_t begin = found.size();
    each_path<true>(v, via, next, &path, [&] {
      found.push_back(Ranked{
          Source{r, {path.rbegin(), path.rend()}, {via.rbegin(), via.rend()}},
          {},
          {},
          {}});
      return found.size() - begin <= cap;
    });
    if (found.size() - begin <= cap) {
      for (std::size_t k = begin; k < found.size(); ++k)
        found[k] = ranked(std::move(found[k].source));
      continue;
    }
    found.resize(begin);
    pending.clear();
    note_cut(v);
    Source first = completion(v, r);
    const std::size_t depth = first.forests.size();
    (void)put({ranked(std::move(first)), depth}, cap);
    std::size_t taken = 0;
    while (!pending.empty() && taken < cap) {
      Candidate c = std::move(pending.front());
      pending.erase(pending.begin());
      ++taken;
      const std::size_t room = cap - taken;
      const Source& cs = c.ranked.source;
      std::size_t missed = 0;
      for (std::size_t i = c.free; room != 0 && missed < kMisses && i-- > 0;) {
        for (Id e = _vertices[cs.vertices[i + 1]].edges; e != none;
             e = _edges[e].next) {
          if (_edges[e].forest == cs.forests[i]) continue;
          Source s = completion(_edges[e].previous, r);
          const std::size_t free = s.forests.size();
          s.forests.push_back(_edges[e].forest);
          s.forests.insert(s.forests.end(), cs.forests.begin() + i + 1,
                           cs.forests.end());
          s.vertices.insert(s.vertices.end(), cs.vertices.begin() + i + 1,
                            cs.vertices.end());
          missed = put({ranked(std::move(s)), free}, room) ? 0 : missed + 1;
        }
      }
      found.push_back(std::move(c.ranked));
    }
  }
  merge_sort(found, earlier);
  out.reserve(found.size());
  for (Ranked& s : found) out.push_back(std::move(s.source));
  return out;
}

std::uint32_t LrGssGraph::tokens(Id forest) {
  if (_tokens.size() < _forest.size()) _tokens.resize(_forest.size(), none);
  if (_tokens[forest] != none) return _tokens[forest];
  std::uint32_t n = 0;
  _forest.each_step(forest, [&](Id s) {
    const Token& t = _steps[s].reading.la;
    if (_steps[s].shifts && t.begin < t.end) ++n;
  });
  return _tokens[forest] = n;
}

std::vector<Id> LrGssGraph::derivation(std::span<const Id> forests) {
  const Id root = _forest.node();
  _forest.add(root, forests, none, {});
  return _forest.steps(root);
}

}  // namespace cursiv
