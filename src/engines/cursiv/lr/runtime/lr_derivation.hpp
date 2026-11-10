// The derivation a look-ahead reading took since its cell, kept step by step:
// where two readings of a cell become one, the rule elects between their
// derivations from the decisions the graph would read, or leaves them to it.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDerivationRoom.hpp"
#include "cursiv/lr/runtime/LrDerivationStep.hpp"
#include "cursiv/lr/runtime/LrShadow.hpp"
#include "cursiv/lr/runtime/lr_elect.hpp"
#include "cursiv/lr/runtime/lr_guard.hpp"
#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/lr/runtime/lr_read.hpp"
#include "cursiv/lr/table/LrActionKind.hpp"
#include "cursiv/lr/table/LrColdTables.hpp"
#include "cursiv/lr/table/LrProduction.hpp"
#include "cursiv/shared/runtime/LexTape.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

template <const auto& A>
inline constexpr bool kLrKeepsDerivations = A.cold.electWord.size() != 0;

// Two derivations no decision tells apart.
inline constexpr int kLrTie = 2;
// What one cell's elections may build and compare: past it the graph elects,
// which reads a cell of many merges for less.
inline constexpr std::uint32_t kLrDerivationWork = 512;

inline bool lr_derivation_spent(const LrContext& c) {
  return c.derivationRoom.work > kLrDerivationWork;
}

// The symbol a frame of the cell's stack stands for, read unbuilt: the one its
// state comes after, where fused states agree on one.
template <const auto& A>
inline std::uint32_t lr_derivation_unread(std::uint32_t state) {
  if constexpr (A.cold.electSym.size() != 0)
    if (const std::uint32_t s = A.cold.electSym[state]; s != kLrNoStateSym)
      return s;
  return 0;
}

CURSIV_ALWAYS_INLINE inline void lr_derivation_push(
    LrContext& c, LrShadow& s, std::uint16_t prod, TextOffset end = kLrNoEnd,
    std::uint16_t mode = kNoMode) {
  const auto at = static_cast<std::uint32_t>(c.derivation.size());
  const std::uint32_t word = prod == kLrNoProd ? s.lexRank : 0u;
  const std::uint32_t state = s.run.back();
  c.derivation.push_back(
      {s.derivation, state, word, s.la.sym, prod, end, s.la.begin, mode});
  // Two cells that read the same get the same path.
  s.path =
      (s.path ^
       (std::uint64_t{prod} << 48 | std::uint64_t{state} << 16 | s.la.sym) ^
       std::uint64_t{word} << 20) *
      0x9E3779B97F4A7C15ull;
  s.derivation = at;
}

template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_derivation_apply(LrContext& c, LrShadow& s,
                                                     std::uint32_t at) {
  const std::uint16_t mode = s.laMode;
  if (!lr_shadow_apply<A>(c, s, at)) return false;
  if constexpr (kLrKeepsDerivations<A>) {
    if (c.derivationRoom.closed) return true;
    const auto x = lr_shadow_action<A>(at);
    if (lr_takes_token_in<A>(x.kind)) {
      // A token the parse inserts stands at no width: no choice of the lexer's.
      const TextOffset end = s.la.begin != s.la.end ? s.la.end : kLrNoEnd;
      lr_derivation_push(c, s, kLrNoProd, end, mode);
    } else if (x.kind != LrActionKind::accept) {
      lr_derivation_push(c, s, x.arg);
    }
  }
  return true;
}

template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_derivation_reduce(LrContext& c, LrShadow& s,
                                                      std::uint16_t prod) {
  if (!lr_shadow_reduce<A>(c, s, prod)) return false;
  if constexpr (kLrKeepsDerivations<A>)
    if (!c.derivationRoom.closed) lr_derivation_push(c, s, prod);
  return true;
}

// The symbols a run of skipped pass-throughs may end at.
template <const auto& A>
inline constexpr auto kLrRunEnds = [] {
  std::array<bool, std::size_t{A.cfg.nterms} + A.cfg.nnts> ends{};
  for (const std::uint32_t key : A.cold.electChainKey)
    ends[key & 0xFFFFu] = true;
  return ends;
}();

// Read where two readings joined, when their frames there do not stand the
// same; and how many such points an election tries.
inline constexpr int kLrUnjoined = 3;
// Two derivations told apart only past the end of a frame, where the future
// adds words neither holds yet.
inline constexpr int kLrGap = 4;
inline constexpr std::size_t kLrJoinTries = 4;

// `lr_derivation_first` read from the steps `ta` and `tb` end their paths at;
// where `joined`, the frames from the first that differs must stand on one
// symbol each and be told apart inside one frame, else `kLrUnjoined`.
template <const auto& A>
int lr_derivation_elect(LrContext& c, std::uint32_t top, std::uint32_t ta,
                        std::uint32_t tb, bool joined = false) {
  using Node = LrDerivationRoom::Node;
  using Walk = LrDerivationRoom::Walk;
  constexpr std::uint32_t kNone = LrDerivationRoom::kNone;
  constexpr std::uint32_t kSeed = 0x80000000u;
  // A node reads as events: its words, the children it enters between them,
  // either end of a run of pass-throughs the future may restore, and the ends
  // of a frame, past which the future adds words neither reading holds yet.
  constexpr std::uint64_t kChild = std::uint64_t{1} << 62;
  constexpr std::uint64_t kGap = std::uint64_t{2} << 62;
  constexpr std::uint64_t kRun = std::uint64_t{3} << 62;
  LrDerivationRoom& r = c.derivationRoom;
  const auto open = [&](std::uint32_t tag, std::uint32_t sym) {
    r.nodes.push_back(
        {tag, sym, static_cast<std::uint32_t>(r.events.size()), 0});
    return static_cast<std::uint32_t>(r.nodes.size() - 1);
  };
  const auto event = [&](std::uint64_t v) {
    r.events.push_back(v);
    ++r.nodes.back().size;
  };
  const std::uint32_t seeded = c.stack.size();
  // Frame `k` of the cell's stack, its subtree unread.
  const auto unread = [&](std::uint32_t k) {
    return open(kSeed | k, lr_derivation_unread<A>(c.stack[k].state));
  };
  // The rule a frame `d` below the top stands under, where the top state
  // knows it.
  const auto rule_at = [&](std::uint32_t d) -> std::uint32_t {
    return d == 0   ? A.cold.electSym[top]
           : d == 1 ? A.cold.electBelow[top]
                    : kLrNoStateSym;
  };
  if (r.frameOf.size() < c.derivation.size())
    r.frameOf.resize(c.derivation.size(), kNone);
  // The stack step `t` leaves, built with every step before it not built
  // yet: readings that share steps share their subtrees. False past the
  // cell's work.
  const auto build = [&](std::uint32_t t) {
    r.pending.clear();
    for (std::uint32_t u = t; u != kLrNoDerivation && r.frameOf[u] == kNone;
         u = c.derivation[u].previous)
      r.pending.push_back(u);
    r.work += static_cast<std::uint32_t>(r.pending.size());
    if (lr_derivation_spent(c)) return false;
    for (auto it = r.pending.rbegin(); it != r.pending.rend(); ++it) {
      const std::uint32_t e = *it;
      const LrDerivationStep& st = c.derivation[e];
      std::uint32_t head =
          st.previous == kLrNoDerivation ? kNone : r.frameOf[st.previous];
      std::uint32_t kept = head == kNone ? seeded : r.frames[head].kept;
      std::uint32_t n;
      if (st.prod == kLrNoProd) {
        n = open(e, st.sym);
        if (st.word != 0) event(st.word);
      } else {
        const std::uint16_t len = A.cfg.prods[st.prod].len;
        r.popped.clear();
        for (std::uint16_t k = 0; k < len; ++k) {
          if (head != kNone) {
            r.popped.push_back(r.frames[head].node);
            head = r.frames[head].below;
          } else {
            r.popped.push_back(unread(--kept));
          }
        }
        std::ranges::reverse(r.popped);
        r.syms.clear();
        for (const std::uint32_t x : r.popped) r.syms.push_back(r.nodes[x].sym);
        const auto [words, pos] =
            lr_elect_words<A>(st.prod, r.syms, r.words, r.wordAt);
        n = open(e, A.cfg.nterms + A.cfg.prods[st.prod].lhs);
        std::size_t w = 0;
        for (std::uint16_t k = 0; k <= len; ++k) {
          while (w != words.size() && (k == len || pos[w] <= k))
            event(words[w++]);
          if (k == len) break;
          event(kChild | r.popped[k]);
        }
      }
      if constexpr (A.cold.electSym.size() != 0) {
        const std::uint32_t run = lr_elect_stands<A>(st.state, r.nodes[n].sym);
        if (run != kLrNoRun) {
          const std::uint32_t under = n;
          n = open(e, A.cold.electSym[st.state]);
          lr_elect_chain<A>(run, 0, event);
          event(kChild | under);
          lr_elect_chain<A>(run, 1, event);
        }
      }
      r.frames.push_back({n, head, kept});
      r.frameOf[e] = static_cast<std::uint32_t>(r.frames.size() - 1);
    }
    return true;
  };
  // The frames a reading pushed, bottom up, over the `kept` frames of the
  // cell's it stands on.
  const auto stack = [&](std::uint32_t t, std::vector<std::uint32_t>& out,
                         std::uint32_t& kept) {
    out.clear();
    kept = seeded;
    if (t == kLrNoDerivation) return true;
    if (!build(t)) return false;
    std::uint32_t h = r.frameOf[t];
    kept = r.frames[h].kept;
    for (; h != kNone; h = r.frames[h].below) out.push_back(r.frames[h].node);
    std::ranges::reverse(out);
    return true;
  };
  std::uint32_t ka = 0, kb = 0;
  if (!stack(ta, r.left, ka) || !stack(tb, r.right, kb)) return 0;
  r.read = std::min({r.read, ka, kb});
  const auto size = static_cast<std::uint32_t>(ka + r.left.size());
  if (size != kb + r.right.size()) return 0;
  const auto tag = [&](std::uint32_t kept, const std::vector<std::uint32_t>& f,
                       std::uint32_t k) {
    return k < kept ? kSeed | k : r.nodes[f[k - kept]].tag;
  };
  std::uint32_t from = std::min(ka, kb);
  while (from < size && tag(ka, r.left, from) == tag(kb, r.right, from)) ++from;
  if (from == size) return kLrTie;
  for (std::uint32_t k = from; k < size; ++k) {
    if (k < ka || k < kb) {
      if (joined) return kLrUnjoined;
      continue;
    }
    const std::uint32_t a = r.nodes[r.left[k - ka]].sym;
    const std::uint32_t b = r.nodes[r.right[k - kb]].sym;
    if (a == b) continue;
    if (joined) return kLrUnjoined;
    const std::uint32_t rule = rule_at(size - 1 - k);
    // Distinct frames await their parent unless a shared rule restores it.
    if (rule == kLrNoStateSym || rule < A.cfg.nterms ||
        (a != rule && lr_elect_run<A>(rule - A.cfg.nterms, a) == kLrNoRun) ||
        (b != rule && lr_elect_run<A>(rule - A.cfg.nterms, b) == kLrNoRun))
      return kLrGap;
  }
  // Each frame stands under the rule its future reads it as: known from the
  // top state for the two frames nearest it, else a mark both sides must
  // share for the comparison to cross it.
  const auto root = [&](std::uint32_t kept,
                        const std::vector<std::uint32_t>& f) {
    r.region.clear();
    for (std::uint32_t k = from; k < size; ++k) {
      const std::uint32_t x = k < kept ? unread(k) : f[k - kept];
      const std::uint32_t sym = r.nodes[x].sym;
      const std::uint32_t d = size - 1 - k;
      const std::uint32_t rule = rule_at(d);
      const std::uint64_t mark =
          kRun | std::uint64_t{sym} << 32 | std::uint64_t{d} << 1;
      std::uint32_t run = kLrNoRun;
      if (rule != kLrNoStateSym && rule != sym && rule >= A.cfg.nterms)
        run = lr_elect_run<A>(rule - A.cfg.nterms, sym);
      const bool marked = rule == kLrNoStateSym && kLrRunEnds<A>[sym];
      r.region.push_back(open(0, sym));
      if (marked) event(mark);
      if (run != kLrNoRun) lr_elect_chain<A>(run, 0, event);
      event(kChild | x);
      event(kGap | std::uint64_t{d} << 1);
      if (run != kLrNoRun) lr_elect_chain<A>(run, 1, event);
      if (marked) event(mark | 1u);
      event(kGap | std::uint64_t{d} << 1 | 1u);
    }
    const std::uint32_t whole = open(0, 0);
    for (const std::uint32_t w : r.region) event(kChild | w);
    return whole;
  };
  // The next event of a walk that is not a child to enter, past the
  // children both walks enter at once, which read the same on both.
  const auto next = [&](std::vector<Walk>& w, std::uint64_t& v) {
    while (!w.empty()) {
      Walk& t = w.back();
      const Node& n = r.nodes[t.node];
      if (t.at == n.size) {
        w.pop_back();
        continue;
      }
      v = r.events[n.events + t.at++];
      if ((v >> 62) != 1) return true;
      w.push_back({static_cast<std::uint32_t>(v), 0});
    }
    return false;
  };
  const auto peek = [&](std::vector<Walk>& w) {
    while (!w.empty() && w.back().at == r.nodes[w.back().node].size)
      w.pop_back();
    return w.empty() ? std::uint64_t{0}
                     : r.events[r.nodes[w.back().node].events + w.back().at];
  };
  r.walkLeft.assign(1, Walk{root(ka, r.left), 0});
  r.walkRight.assign(1, Walk{root(kb, r.right), 0});
  for (;;) {
    for (std::uint64_t v = peek(r.walkLeft);
         (v >> 62) == 1 && v == peek(r.walkRight); v = peek(r.walkLeft)) {
      ++r.walkLeft.back().at;
      ++r.walkRight.back().at;
    }
    std::uint64_t x = 0, y = 0;
    const bool more = next(r.walkLeft, x);
    if (more != next(r.walkRight, y)) return 0;
    if (!more) return kLrTie;
    ++r.work;
    if (lr_derivation_spent(c)) return 0;
    if (x == y) continue;
    if ((x | y) >> 62 != 0)
      return joined ? kLrUnjoined : ((x | y) >> 62) == 2 ? kLrGap : 0;
    return x < y ? -1 : 1;
  }
}

// A shifted token's lexical rank: the word the lexer gave it, or the weight of
// the category it reads its text at.
template <const auto& A>
std::uint32_t lr_derivation_rank(const LrContext& c,
                                 const LrDerivationStep& step) {
  if (step.word != 0) return step.word;
  if constexpr (A.cold.accPriority.size() == 0 && A.nrelays == 0) {
    return ~std::uint32_t{0};
  } else {
    const unsigned priority =
        step.mode == kNoMode
            ? 0u
            : lex_reading_priority(LrLexTape<A>{}, c.src, c.len, step.mode,
                                   step.begin, step.end, step.sym);
    return ~lex_weight(priority);
  }
}

// Which of `a` and `b` took the terminal the lexer ranks first where they read
// one spelling as two: the lexer elects it before any arm. Zero where they read
// the text the same, or part on a token's length first.
template <const auto& A>
int lr_derivation_lexical(LrContext& c, const LrShadow& a, const LrShadow& b) {
  LrDerivationRoom& r = c.derivationRoom;
  if (a.derivation == b.derivation) return 0;
  const auto token = [&](std::uint32_t t) {
    return c.derivation[t].end != kLrNoEnd;
  };
  r.left.clear();
  r.right.clear();
  // A step is numbered after the steps under it.
  std::uint32_t x = a.derivation, y = b.derivation;
  while (x != y) {
    if (y == kLrNoDerivation || (x != kLrNoDerivation && x > y)) {
      if (token(x)) r.left.push_back(x);
      x = c.derivation[x].previous;
    } else {
      if (token(y)) r.right.push_back(y);
      y = c.derivation[y].previous;
    }
  }
  const std::size_t n = std::min(r.left.size(), r.right.size());
  for (std::size_t k = 1; k <= n; ++k) {
    const LrDerivationStep& p = c.derivation[r.left[r.left.size() - k]];
    const LrDerivationStep& q = c.derivation[r.right[r.right.size() - k]];
    if (p.end != q.end) return 0;
    if (p.sym == q.sym) continue;
    const std::uint32_t u = lr_derivation_rank<A>(c, p),
                        v = lr_derivation_rank<A>(c, q);
    if (u != v) return u < v ? -1 : 1;
  }
  return 0;
}

// Which of `a` and `b`, two readings of one cell, derived the text first:
// negative for `a`, positive for `b`, `kLrTie` where no decision parts them,
// zero where it lies past them, `kLrGap` where their future writes it.
template <const auto& A>
int lr_derivation_first(LrContext& c, const LrShadow& a, const LrShadow& b) {
  // What the lexer ranks first decides before any derivation does.
  if (const int lexical = lr_derivation_lexical<A>(c, a, b); lexical != 0)
    return lexical;
  using Memo = LrDerivationRoom::Memo;
  LrDerivationRoom& r = c.derivationRoom;
  Memo key{};
  key.left = a.path ^ a.path >> 31;
  key.right = b.path ^ b.path >> 31;
  key.top = a.run.back();
  // A path spells no frame of the cell's stack: a verdict holds for another
  // cell only where the frames it read stand for the same symbols there.
  const std::uint32_t seeded = c.stack.size();
  const auto hash = [](const Memo& m) {
    return (m.left ^ (m.right * 0x9E3779B97F4A7C15ull) ^ m.top) *
           0xBF58476D1CE4E5B9ull;
  };
  const auto slot = [&](const Memo& m) {
    const std::size_t mask = r.memo.size() - 1;
    std::size_t at = (hash(m) >> 32) & mask;
    for (; r.memo[at].first != Memo::kFree; at = (at + 1) & mask) {
      const Memo& x = r.memo[at];
      if (x.left == m.left && x.right == m.right && x.top == m.top) break;
    }
    return at;
  };
  // The shapes a parse merges in outnumber a small table: it grows to a bound
  // before it forgets. A parse of few merges keeps to a few slots.
  constexpr std::size_t kFewMemos = 16, kMemos = 1024,
                        kMostMemos = std::size_t{1} << 15;
  if (r.memo.empty()) {
    r.memo.assign(kFewMemos, Memo{});
    r.reserve();
  }
  std::size_t at = slot(key);
  const auto exact = [&](const Memo& m) {
    if (m.depth > seeded) return false;
    const auto words = std::span(r.memoExact).subspan(m.exactAt, m.exactSize);
    std::size_t pos = 2;
    const auto path = [&](std::uint32_t derivation, std::size_t size) {
      const std::size_t end = pos + size;
      for (; derivation != kLrNoDerivation;
           derivation = c.derivation[derivation].previous) {
        if (pos == end) return false;
        const LrDerivationStep& step = c.derivation[derivation];
        if (words[pos++] != (std::uint32_t{step.prod} << 16 | step.sym) ||
            words[pos++] != step.state || words[pos++] != step.word)
          return false;
      }
      return pos == end;
    };
    if (!path(a.derivation, words[0]) || !path(b.derivation, words[1]))
      return false;
    for (std::uint32_t k = seeded - m.depth; k < seeded; ++k)
      if (words[pos++] != lr_derivation_unread<A>(c.stack[k].state))
        return false;
    return true;
  };
  bool known = r.memo[at].first != Memo::kFree;
  if (known && r.memo[at].exactSize != 0 && exact(r.memo[at]))
    return r.memo[at].first;
  if (lr_derivation_spent(c)) return 0;
  // Steps both took the same since they last differed decide nothing: the
  // election reads at the first point after them where the two stand the same.
  r.read = seeded;
  std::array<std::pair<std::uint32_t, std::uint32_t>, kLrJoinTries> joins;
  std::size_t njoins = 0;
  for (std::uint32_t x = a.derivation, y = b.derivation;
       x != kLrNoDerivation && y != kLrNoDerivation && x != y;) {
    const LrDerivationStep& p = c.derivation[x];
    const LrDerivationStep& q = c.derivation[y];
    if (p.prod != q.prod || p.state != q.state || p.sym != q.sym ||
        p.word != q.word)
      break;
    joins[njoins++ % kLrJoinTries] = {x, y};
    x = p.previous;
    y = q.previous;
  }
  int first = kLrUnjoined;
  for (std::size_t k = njoins, tries = 0;
       k-- > 0 && first == kLrUnjoined && tries < kLrJoinTries; ++tries) {
    const auto [x, y] = joins[k % kLrJoinTries];
    first = lr_derivation_elect<A>(c, c.derivation[x].state, x, y, true);
  }
  if (first == kLrUnjoined)
    first = lr_derivation_elect<A>(c, key.top, a.derivation, b.derivation);
  key.first = static_cast<std::int8_t>(first);
  key.depth = seeded - r.read;
  r.syms.clear();
  // Keep a proof only when a key recurs; an unproved entry elects nothing.
  if (known) {
    constexpr std::size_t kExactWords = kMostMemos;
    r.syms.assign(2, 0);
    const auto keep_path = [&](std::uint32_t derivation, unsigned side) {
      const auto from = r.syms.size();
      for (; derivation != kLrNoDerivation;
           derivation = c.derivation[derivation].previous) {
        if (r.syms.size() + 3u + key.depth > kExactWords) return false;
        const LrDerivationStep& step = c.derivation[derivation];
        r.syms.push_back(std::uint32_t{step.prod} << 16 | step.sym);
        r.syms.push_back(step.state);
        r.syms.push_back(step.word);
      }
      r.syms[side] = static_cast<std::uint32_t>(r.syms.size() - from);
      return true;
    };
    if (key.depth + 2u > kExactWords || !keep_path(a.derivation, 0) ||
        !keep_path(b.derivation, 1))
      return key.first;
    for (std::uint32_t k = seeded - key.depth; k < seeded; ++k)
      r.syms.push_back(lr_derivation_unread<A>(c.stack[k].state));
    if (r.memoExact.size() + r.syms.size() > kExactWords) {
      r.memo.assign(r.memo.size(), Memo{});
      r.memoExact.clear();
      r.memos = 0;
      known = false;
      at = slot(key);
    }
  }
  if (!known && 2 * ++r.memos > r.memo.size()) {
    if (r.memo.size() < kMostMemos) {
      std::vector<Memo> kept;
      kept.swap(r.memo);
      r.memo.assign(std::max(2 * kept.size(), kMemos), Memo{});
      for (const Memo& m : kept)
        if (m.first != Memo::kFree) r.memo[slot(m)] = m;
    } else {
      r.memo.assign(r.memo.size(), Memo{});
      r.memoExact.clear();
      r.memos = 1;
    }
    at = slot(key);
  }
  if (!r.syms.empty()) {
    key.exactAt = static_cast<std::uint32_t>(r.memoExact.size());
    key.exactSize = static_cast<std::uint32_t>(r.syms.size());
    r.memoExact.insert(r.memoExact.end(), r.syms.begin(), r.syms.end());
  }
  r.memo[at] = key;
  return key.first;
}

// Two readings of one cell become `a`, carrying the derivation the rule elects,
// `undecided` turning where none is elected. False where only their future
// tells them apart: both read on.
template <const auto& A>
bool lr_derivation_merge(LrContext& c, LrShadow& a, const LrShadow& b,
                         bool& undecided) {
  if (a.derivation == b.derivation) {
    a.torn = a.torn || b.torn;
    return true;
  }
  int first = lr_derivation_first<A>(c, a, b);
  if (first == kLrGap) {
    if (!a.torn && !b.torn) return false;
    first = 0;
  }
  if (a.arm != b.arm && (first == 0 || a.torn || b.torn)) undecided = true;
  a.torn = a.arm == b.arm && (first == 0 || a.torn || b.torn);
  // The same, the arm the table puts first stands: no decision says otherwise.
  if (first == kLrTie ? b.arm < a.arm : first > 0) {
    a.derivation = b.derivation;
    a.path = b.path;
    a.arm = b.arm;
  }
  return true;
}

}  // namespace cursiv::engine
