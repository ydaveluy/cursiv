// Packed derivations of an episode. A node is elected once no alternative can
// reach it or its descendants; its alternatives share both ends' readings.
// Competing ranks must differ before either ends, for suffixes to preserve
// order. A rank is the sequence of words an alternative spells and of the
// ranks of the nodes below, held as a shared rope so that two ranks are told
// apart without reading what they share; an alternative's words stand before
// the child their position names. Where two alternatives read their first
// token apart, the lexer's rank of that token comes before any word either
// spells at the same offset.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "cursiv/engines/lr/runtime/LrGssPool.hpp"
#include "cursiv/engines/lr/runtime/LrGssRope.hpp"
#include "cursiv/engines/lr/runtime/LrGssRuns.hpp"

namespace cursiv {

class LrGssForest {
 public:
  using Id = std::uint32_t;
  static constexpr Id none = ~Id{0};
  // What an alternative reads itself: a token, one the lexer ranked among
  // rivals -- its only word is the rank --, or neither.
  enum class Kind : std::uint8_t { inner, token, ranked };

  void clear() {
    _nodes.clear();
    _alternatives.clear();
    _children.clear();
    _choices.clear();
    _choiceAt.clear();
    _spell.clear();
    _flats.clear();
    _small = true;
    _visits.clear();
  }

  // A fresh node, with no alternative yet.
  Id node() { return static_cast<Id>(_nodes.push_back(Node{})); }

  // The producer emits each path once; election also tolerates duplicates.
  // Words follow every child unless `at` places each before one.
  void add(Id node, std::span<const Id> children, Id step,
           std::span<const std::uint32_t> choices,
           std::span<const std::uint16_t> at = {}, Kind kind = Kind::inner) {
    assert(node < _nodes.size() && _nodes[node].mark == kFresh);
    assert(at.empty() || at.size() == choices.size());
    const Range childRange = {
        static_cast<std::uint32_t>(_children.append(children)),
        static_cast<std::uint32_t>(children.size())};
    const Range choiceRange = {
        static_cast<std::uint32_t>(_choices.append(choices)),
        static_cast<std::uint32_t>(choices.size())};
    if (at.empty())
      (void)_choiceAt.append(choices.size(),
                             static_cast<std::uint16_t>(children.size()));
    else
      (void)_choiceAt.append(at);
    const auto id = static_cast<Id>(
        _alternatives.push_back({childRange, choiceRange, step, none, kind}));
    Node& n = _nodes[node];
    if (n.alternative == none)
      n.first = id;
    else
      _alternatives[n.alternative].next = id;
    n.alternative = id;
    if (_small && _nodes.size() > kSmall) flatten_no_more();
  }

  Id elect(Id root) {
    assert(root < _nodes.size());
    if (_nodes[root].mark >= kElected) return _nodes[root].alternative;
    _visits.clear();
    _visits.push_back({root, _nodes[root].first});
    _nodes[root].mark = kVisiting;
    while (!_visits.empty()) {
      Visit& v = _visits.back();
      Node& n = _nodes[v.node];
      if (v.alternative != none) {
        const Alternative& a = _alternatives[v.alternative];
        if (v.child != a.children.size) {
          const Id child = _children[a.children.at + v.child++];
          assert(child < _nodes.size());
          if (_nodes[child].mark == kVisiting)
            throw std::logic_error("cursiv: cyclic GSS forest");
          if (_nodes[child].mark == kFresh) {
            _nodes[child].mark = kVisiting;
            _visits.push_back({child, _nodes[child].first});
          }
          continue;
        }
        v.alternative = a.next;
        v.child = 0;
        continue;
      }
      if (n.first == none)
        throw std::logic_error("cursiv: GSS forest node has no alternative");
      n.alternative = n.first;
      for (Id id = _alternatives[n.first].next; id != none;
           id = _alternatives[id].next)
        if (precedes(id, n.alternative)) n.alternative = id;
      n.mark = kElected;
      const Lead lead = lead_of(n.alternative);
      n.lead = lead.word;
      n.text = lead.text;
      _visits.pop_back();
    }
    return _nodes[root].alternative;
  }

  // The elected steps under `root`, in the order the reading took them.
  template <class F>
  void each_step(Id root, F&& take) {
    (void)elect(root);
    _visits.clear();
    _visits.push_back({root, _nodes[root].alternative});
    while (!_visits.empty()) {
      Visit& v = _visits.back();
      const Alternative& a = _alternatives[v.alternative];
      if (v.child != a.children.size) {
        const Id child = _children[a.children.at + v.child++];
        _visits.push_back({child, _nodes[child].alternative});
      } else {
        if (a.step != none) take(a.step);
        _visits.pop_back();
      }
    }
  }

  // The same steps, the last the reading took first, while `take` asks for
  // more.
  template <class F>
  void each_step_back(Id root, F&& take) {
    (void)elect(root);
    _visits.clear();
    const auto enter = [&](Id node) {
      const Id id = _nodes[node].alternative;
      const Alternative& a = _alternatives[id];
      if (a.step != none && !take(a.step)) return false;
      _visits.push_back({node, id, a.children.size});
      return true;
    };
    if (!enter(root)) return;
    while (!_visits.empty()) {
      Visit& v = _visits.back();
      if (v.child == 0) {
        _visits.pop_back();
        continue;
      }
      const Id child =
          _children[_alternatives[v.alternative].children.at + --v.child];
      if (!enter(child)) return;
    }
  }

  [[nodiscard]] Id step(Id alternative) const {
    return _alternatives[alternative].step;
  }
  [[nodiscard]] std::span<const Id> children(Id alternative) const {
    const Range r = _alternatives[alternative].children;
    return _children.span(r.at, r.size);
  }
  [[nodiscard]] std::span<const std::uint16_t> choice_at(Id alternative) const {
    const Range r = _alternatives[alternative].choices;
    return _choiceAt.span(r.at, r.size);
  }

  std::vector<Id> steps(Id root) {
    std::vector<Id> out;
    each_step(root, [&](Id s) { out.push_back(s); });
    return out;
  }

  // Whether the stack `left` orders before `right` under one future.
  bool precedes(std::span<const Id> left, std::span<const Id> right) {
    return order(left, right) < 0;
  }
  // Two stacks under one future, frame by frame, each frame's rank: negative
  // where `left` orders first, positive where `right` does, zero where they
  // read alike, `kOpen` where one side ends a part the other goes on with, as
  // the future's words there are not in either.
  static constexpr int kOpen = 3;
  int order(std::span<const Id> left, std::span<const Id> right) {
    for (const Id n : left) (void)elect(n);
    for (const Id n : right) (void)elect(n);
    return ordered(left, right);
  }
  // The same over stacks whose nodes are elected already.
  int ordered(std::span<const Id> left, std::span<const Id> right) {
    if (left.size() != right.size()) return kOpen;
    for (std::size_t k = 0; k != left.size(); ++k)
      if (const int v = ordered_frame(left[k], right[k]); v != 0) return v;
    return 0;
  }
  // The frame of each stack at one depth, elected already: its rank.
  int ordered_frame(Id left, Id right) {
    if (left == right) return 0;
    if (const int v = lead_order(_nodes[left].lead, _nodes[right].lead))
      return v;
    ranked(left);
    ranked(right);
    _layLeft.clear();
    _layRight.clear();
    if (length(rank_piece(left)) != 0) _layLeft.push_back(rank_piece(left));
    if (length(rank_piece(right)) != 0) _layRight.push_back(rank_piece(right));
    const int v = compare(_layLeft, _layRight);
    return v == -2 || v == 2 ? kOpen : v;
  }

  std::vector<std::uint32_t> rank(Id root) {
    (void)elect(root);
    ranked(root);
    std::vector<std::uint32_t> out;
    const Node& n = _nodes[root];
    if (n.flatLen != 0) {
      const auto words = _flats.span(n.flatAt, n.flatLen);
      out.assign(words.begin(), words.end());
      return out;
    }
    Spell::Cursor c;
    for (c.seek(_spell, n.rank, 0); !c.done(); c.next())
      out.push_back(c.value());
    return out;
  }

  [[nodiscard]] std::size_t size() const { return _nodes.size(); }
  [[nodiscard]] std::size_t alternatives() const {
    return _alternatives.size();
  }
  [[nodiscard]] std::size_t links() const { return _children.size(); }
  [[nodiscard]] std::size_t rankCells() const { return _spell.nodes(); }
  [[nodiscard]] std::size_t choiceWords() const { return _choices.size(); }

 private:
  using Spell = LrGssRope<LrGssNoAgg>;

  // Offsets into the arenas, which never outgrow 32 bits.
  struct Range {
    std::uint32_t at = 0;
    std::uint32_t size = 0;
  };
  struct Alternative {
    Range children;
    Range choices;
    Id step;
    Id next;
    Kind kind;
  };
  // A node's rank is computed once a comparison reads it, never before.
  enum Mark : std::uint8_t { kFresh, kVisiting, kElected, kRanked };
  struct Node {
    // The rank as a rope, where it is long; short, as those words laid in
    // `_flats`.
    Spell::Id rank = Spell::none;
    Id first = none;
    // Last added while fresh, elected after all its alternatives were visited.
    Id alternative = none;
    // The rank of the first token its elected alternative reads, `kNoLead`
    // where that token has none; whether it reads one.
    std::uint32_t lead = 0;
    std::uint32_t flatAt = 0;
    Mark mark = kFresh;
    bool text = false;
    std::uint8_t flatLen = 0;
  };
  // A small forest holds its ranks of up to `kFlat` words flat: its
  // comparisons are short, and a rope costs more than it saves. Past `kSmall`
  // nodes every rank is a rope.
  static constexpr std::size_t kFlat = 128;
  static constexpr std::size_t kSmall = 2048;
  static constexpr std::uint32_t kNoLead = 0;
  struct Lead {
    std::uint32_t word;
    bool text;
  };
  // Two first tokens the lexer ranked apart: the lower rank orders first, and
  // before any word either spells at the same offset.
  static int lead_order(std::uint32_t left, std::uint32_t right) {
    if (left == kNoLead || right == kNoLead || left == right) return 0;
    return left < right ? -1 : 1;
  }
  // The first token `alternative` reads, once its children are elected.
  Lead lead_of(Id alternative) const {
    const Alternative& a = _alternatives[alternative];
    if (a.kind != Kind::inner)
      return {a.kind == Kind::ranked ? _choices[a.choices.at] : kNoLead, true};
    for (const Id kid : _children.span(a.children.at, a.children.size))
      if (_nodes[kid].text) return {_nodes[kid].lead, true};
    return {kNoLead, false};
  }
  struct Visit {
    Id node;
    Id alternative;
    std::size_t child = 0;
  };
  // What a rank reads next: some words of the arena, or the rank of a node.
  struct Piece {
    const std::uint32_t* words = nullptr;
    std::uint32_t size = 0;
    Spell::Id rank = Spell::none;
  };
  Piece words_piece(Range r) const {
    return {r.size == 0 ? nullptr : &_choices[r.at], r.size};
  }
  Piece rank_piece(Id node) const {
    const Node& n = _nodes[node];
    if (n.flatLen != 0) return {&_flats[n.flatAt], n.flatLen};
    return {nullptr, 0, n.rank};
  }
  bool has_rank(Id node) const {
    return _nodes[node].flatLen != 0 || _nodes[node].rank != Spell::none;
  }
  Spell::Length length(const Piece& p) const {
    return p.words != nullptr ? p.size : _spell.length(p.rank);
  }

  // The pieces `alternative` reads from its child `from` on: its words and its
  // children's ranks. The children from `from` on must be ranked.
  void lay(Id alternative, std::size_t from, std::vector<Piece>& out) const {
    const Alternative& a = _alternatives[alternative];
    const auto kids = _children.span(a.children.at, a.children.size);
    const std::uint32_t end = a.choices.at + a.choices.size;
    std::uint32_t w = a.choices.at;
    for (std::size_t k = from;; ++k) {
      const std::uint32_t at = w;
      while (w != end && (k == kids.size() || _choiceAt[w] <= k)) ++w;
      if (w != at) out.push_back(words_piece(Range{at, w - at}));
      if (k == kids.size()) break;
      if (has_rank(kids[k])) out.push_back(rank_piece(kids[k]));
    }
  }

  // Leading children both alternatives read alike, before any word, are not
  // ranked.
  bool precedes(Id candidate, Id elected) {
    if (const int v =
            lead_order(lead_of(candidate).word, lead_of(elected).word))
      return v < 0;
    const auto kl = children(candidate), kr = children(elected);
    const auto al = choice_at(candidate), ar = choice_at(elected);
    std::size_t from = 0;
    while (from < kl.size() && from < kr.size() && kl[from] == kr[from] &&
           (al.empty() || al.front() > from) &&
           (ar.empty() || ar.front() > from))
      ++from;
    for (std::size_t k = from; k < kl.size(); ++k) ranked(kl[k]);
    for (std::size_t k = from; k < kr.size(); ++k) ranked(kr[k]);
    _layLeft.clear();
    _layRight.clear();
    lay(candidate, from, _layLeft);
    lay(elected, from, _layRight);
    return compare(_layLeft, _layRight) < 0;
  }

  void ranked(Id root) {
    if (_nodes[root].mark == kRanked) return;
    _ranking.assign(1, {root, _nodes[root].alternative});
    while (!_ranking.empty()) {
      Visit& v = _ranking.back();
      const Alternative& elected = _alternatives[v.alternative];
      if (v.child != elected.children.size) {
        const Id child = _children[elected.children.at + v.child++];
        if (_nodes[child].mark != kRanked)
          _ranking.push_back({child, _nodes[child].alternative});
        continue;
      }
      const Id node = v.node;
      const Id alternative = v.alternative;
      _ranking.pop_back();
      _joined.clear();
      lay(alternative, 0, _joined);
      Spell::Length total = 0;
      for (const Piece& p : _joined)
        total = detail::gss_length_add(total, length(p));
      Node& n = _nodes[node];
      n.mark = kRanked;
      if (_small && total <= kFlat) {
        std::size_t at = _flats.reserve(total);
        n.flatAt = static_cast<std::uint32_t>(at);
        for (const Piece& p : _joined)
          for (std::uint32_t i = 0; i != p.size; ++i) _flats[at++] = p.words[i];
        n.flatLen = static_cast<std::uint8_t>(total);
      } else {
        n.rank = spell(_joined);
      }
    }
  }

  // Flat ranks are dropped for ropes, which a rank takes when next asked for.
  // A parent that already has a rope must revisit its children too: opening
  // its derivation still reads their ranks, including in the spine index. No
  // node reads `_flats` past this point, so its words are freed.
  void flatten_no_more() {
    for (std::size_t i = 0; i != _nodes.size(); ++i) {
      Node& n = _nodes[i];
      n.flatLen = 0;
      if (n.mark == kRanked) n.mark = kElected;
    }
    _flats.clear();
    _small = false;
  }

  // The rope of the values the pieces read in order.
  Spell::Id spell(const std::vector<Piece>& pieces) {
    _parts.clear();
    for (const Piece& p : pieces) {
      if (p.words == nullptr) {
        _parts.push_back(p.rank);
        continue;
      }
      for (std::uint32_t i = 0; i != p.size; ++i)
        _parts.push_back(_spell.leaf(p.words[i]));
    }
    return _spell.concat_all(_parts);
  }

  // The value of a piece at an offset.
  std::uint32_t value(const Piece& p, Spell::Length at) const {
    return p.words != nullptr ? p.words[at] : _spell.at(p.rank, at);
  }

  // How many values two pieces read alike from their offsets, within `n`.
  Spell::Length agree(const Piece& a, Spell::Length oa, const Piece& b,
                      Spell::Length ob, Spell::Length n) const {
    if (a.words == nullptr && b.words == nullptr) {
      if (a.rank == b.rank && oa == ob) return n;
      return _spell.common(a.rank, oa, b.rank, ob, n);
    }
    Spell::Length i = 0;
    if (a.words != nullptr && b.words != nullptr) {
      while (i != n && a.words[oa + i] == b.words[ob + i]) ++i;
      return i;
    }
    const bool ropeA = a.words == nullptr;
    const std::uint32_t* words = ropeA ? b.words + ob : a.words + oa;
    Spell::Cursor c;
    c.seek(_spell, ropeA ? a.rank : b.rank, ropeA ? oa : ob);
    for (; i != n && c.value() == words[i]; ++i, c.next()) {
    }
    return i;
  }

  // Negative where `left` reads first, positive where `right` does, zero
  // where they read alike; two where one ends while the other goes on.
  int compare(std::span<const Piece> left, std::span<const Piece> right) const {
    std::size_t i = 0, j = 0;
    Spell::Length oi = 0, oj = 0;
    for (;;) {
      while (i != left.size() && oi == length(left[i])) ++i, oi = 0;
      while (j != right.size() && oj == length(right[j])) ++j, oj = 0;
      if (i == left.size() || j == right.size())
        return i == left.size() && j == right.size() ? 0
               : i == left.size()                    ? -2
                                                     : 2;
      const Spell::Length n =
          std::min(length(left[i]) - oi, length(right[j]) - oj);
      const Spell::Length same = agree(left[i], oi, right[j], oj, n);
      if (same != n)
        return value(left[i], oi + same) < value(right[j], oj + same) ? -1 : 1;
      oi += n;
      oj += n;
    }
  }

  LrGssPool<Node> _nodes;
  LrGssPool<Alternative> _alternatives;
  LrGssRuns<Id> _children;
  // Every alternative's choices, kept once as they were added, and the child
  // each stands before.
  LrGssRuns<std::uint32_t> _choices;
  LrGssRuns<std::uint16_t> _choiceAt;
  LrGssRuns<std::uint32_t> _flats;
  bool _small = true;
  Spell _spell;
  std::vector<Piece> _joined, _layLeft, _layRight;
  std::vector<Spell::Id> _parts;
  std::vector<Visit> _visits;
  std::vector<Visit> _ranking;
};

}  // namespace cursiv
