// A cell the table leaves open, decided where the parse stands: one reading
// per arm over the stack in hand, read ahead until one survives or two prove
// identical -- the same top, the same class of frame below it. Arms are seeded
// as the table lays them, in the order the grammar wrote them; two readings
// that become one elect by their derivations, or leave the cell to the graph.
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/core/utils/ImplicitLifetime.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrShadow.hpp"
#include "cursiv/engines/lr/runtime/LrStackClimb.hpp"
#include "cursiv/engines/lr/runtime/lr_guard.hpp"
#include "cursiv/engines/lr/runtime/lr_read.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/lr/runtime/lr_trail.hpp"
#include "cursiv/engines/lr/table/LrBitSet.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"

namespace cursiv {

// The budgets below bound the speculation: a cell one of them runs out on is
// declared unsettled and goes to the graph, and only readings that already
// agree on a single arm answer at exhaustion. The graph reads every arm to the
// end, and on a fault it keeps every arm's frozen reading for the search, so
// the repair it elects can differ from the one a settled cell leaves.

inline constexpr unsigned kLrMaxShadows = 64;

// What a cell answers when the look-ahead ran out with rivals alive.
inline constexpr unsigned kLrUnsettled = 0xFFFFFFFFu;
// ...and when the text refuted every arm: a repair of each is the search's
// to elect.
inline constexpr unsigned kLrRefuted = 0xFFFFFFFEu;
// ...and when the graph reads the cell: past every arm, which the drive's
// switch takes for a stop, the context left as the cell found it.
inline constexpr unsigned kLrSplitCell = 0xFFFFFFFDu;
// ...and when two arms read the text into one reading and the rule elects
// neither from what the look-ahead holds: the graph elects.
inline constexpr unsigned kLrMerged = 0xFFFFFFFCu;
// Tokens of text read ahead for one cell: a DEPTH, or width alone would
// decide a wide cell. A reading costs more here than in the drive, so a cell
// that needs more goes to the graph.
inline constexpr unsigned kLrLookBudget = 24;
// Actions applied since the last token was read, over the cell's round: a
// reduce reads no text. Over the stack the cell stands on: a reading that
// reduces down it pays its depth, which no constant bounds.
inline constexpr unsigned kLrStepBudget = 4096;
// Lexes the whole cell may pay: a round lexes once per reading alive.
inline constexpr unsigned kLrLexBudget = kLrLookBudget * kLrMaxShadows;

// The arms a reading descends from, one set each at the table's width: the
// rows live on the context, and a reading swapped by its slot carries them.
template <unsigned Arms>
inline LrBitSet<Arms>* lr_origin_room(LrContext& c, std::size_t slots) {
  const std::size_t want = slots * LrBitSet<Arms>::kWords;
  if (c.origin.size() < want) c.origin.resize(want, 0);
  return start_implicit_array<LrBitSet<Arms>>(c.origin.data(), slots);
}

// Everything `lr_shadow_same` reads but the run itself, and the run's top,
// which two identical runs share. Implied by sameness, so unequal marks are
// never one reading -- and equal ones are still walked.
inline std::uint64_t lr_shadow_mark(const LrShadow& s) {
  constexpr std::uint64_t kPrime = 0x100000001b3ull;
  std::uint64_t m = s.at;
  m = m * kPrime + s.run.size();
  m = m * kPrime + (s.run.size() != 0 ? s.run.back() : 0u);
  m = m * kPrime + (s.done ? 1u : 0u);
  m = m * kPrime + (std::uint64_t{s.indent.head} << 32 | s.indent.depth);
  const LrVirtualAt virt = s.virt.seen_from(s.at);
  m = m * kPrime + virt.insertedAt;
  m = m * kPrime + virt.inserted;
  m = m * kPrime + virt.from;
  m = m * kPrime + virt.pending;
  m = m * kPrime + s.laMode;
  if (s.laMode != kNoMode) {
    m = m * kPrime + s.la.sym;
    m = m * kPrime + s.la.begin;
    m = m * kPrime + s.la.end;
  }
  return m;
}

// The same stack can still owe different implied tokens, read a different
// gap or stand in different levels.
template <class Class>
inline bool lr_shadow_same(const LrShadow& a, const LrShadow& b,
                           Class&& klass) {
  return a.at == b.at && a.done == b.done && a.laMode == b.laMode &&
         a.virt.seen_from(a.at) == b.virt.seen_from(b.at) &&
         a.indent == b.indent && (a.laMode == kNoMode || a.la == b.la) &&
         lr_same_run(a.run, b.run, klass);
}

// A cell's readings, as the shared step reads them, and the round's budgets.
template <const auto& A, unsigned Arms>
struct LrCellPool {
  LrContext& c;
  std::vector<LrShadow>& sh;
  LrBitSet<Arms>*& arms;
  std::size_t& n;
  const std::size_t cap;
  unsigned lexes = 0;
  unsigned kept = 0;
  unsigned steps = 0;
  // A candidate the pool had no room for: what it would have read is not
  // refuted, it is unread, and nothing below may conclude from its silence.
  bool crop = false;
  // The last token read at a position, by mode and by whether a bracket is
  // open.
  struct Read {
    std::uint16_t mode;
    bool deep;
    TextOffset at;
    Token token;
  };
  std::array<Read, 6> seen{};
  unsigned nseen = 0;
  unsigned nextSeen = 0;
  // Whether the token in hand came from `seen`, and so cost no lex.
  bool shared = false;

  LrShadow& operator[](std::size_t i) { return sh[i]; }
  // A reading forked from another, ranged beside it. THE POOL MOVES ITS ARMS:
  // an `emplace_back` reallocates both, so the row pointer is taken again here
  // and the caller keeps none across a fork.
  std::size_t fork(std::size_t i) {
    if (n >= cap) {
      crop = true;
      return kLrNoFork;
    }
    if (n == sh.size()) {
      sh.emplace_back();
      arms = lr_origin_room<Arms>(c, sh.size());
    }
    sh[n] = sh[i];
    sh[n].dead = false;
    arms[n] = arms[i];
    return n++;
  }
  void drop(std::size_t) { --n; }
  CURSIV_ALWAYS_INLINE bool apply(std::size_t i, std::uint32_t at) {
    if (!lr_trail_apply<A>(c, sh[i], at)) return false;
    sh[i].shifted =
        sh[i].shifted || (lr_takes_token_in<A>(lr_shadow_action<A>(at).kind) &&
                          A.cfg.terms[sh[i].la.sym].laid == 0);
    return true;
  }
  CURSIV_ALWAYS_INLINE bool reduce(std::size_t i, std::uint16_t p) {
    return lr_trail_reduce<A>(c, sh[i], p);
  }
  // The token at a position in a mode is one for every reading standing
  // there: it is read once.
  Token scan(std::size_t i, std::uint16_t mode) {
    const TextOffset at = sh[i].at;
    const bool deep = kBracketed<A.cfg> && sh[i].indent.depth != 0;
    for (unsigned k = 0; k < nseen; ++k)
      if (seen[k].at == at && seen[k].mode == mode && seen[k].deep == deep) {
        shared = true;
        return seen[k].token;
      }
    const Token t =
        lr_scan_at<A>(c, mode, at, kBracketed<A.cfg> ? sh[i].indent.depth : 0);
    seen[nextSeen] = {mode, deep, at, t};
    nextSeen = (nextSeen + 1) % seen.size();
    if (nseen < seen.size()) ++nseen;
    return t;
  }
  void lexed() {
    if (!shared) ++lexes;
    shared = false;
    steps = 0;
  }
  void kept_at(std::size_t i) {
    if (sh[i].keptAt == sh[i].at) return;
    ++kept;
    sh[i].keptAt = sh[i].at;
  }
  void stepped() { ++steps; }
};

// Of the readings that shifted a token from one place, only those of the
// token the lexer elects stand: it chose once for them all.
template <const auto& A>
inline void lr_elected_shift(const LrContext& c, std::vector<LrShadow>& sh,
                             std::size_t n) {
  // A tie reads its tokens again: each once, not once per rival.
  for (std::size_t i = 0; i < n; ++i) sh[i].laWeight = LrShadow::kUnweighed;
  const auto weight = [&](LrShadow& s) {
    if (s.laWeight == LrShadow::kUnweighed)
      s.laWeight = static_cast<std::uint8_t>(lr_token_weight<A>(c, s.la));
    return s.laWeight;
  };
  for (std::size_t i = 0; i < n; ++i) {
    if (!sh[i].shifted || sh[i].dead || sh[i].la.begin == sh[i].la.end)
      continue;
    for (std::size_t j = 0; j < n; ++j)
      if (sh[j].shifted && !sh[j].dead &&
          lr_token_beats(sh[j].la, sh[i].la,
                         [&] { return weight(sh[j]) > weight(sh[i]); })) {
        sh[i].dead = true;
        break;
      }
  }
}

// The arm the cell answers, as an index into the order the caller seeded. Or
// none: `kLrUnsettled`, `kLrRefuted`, `kLrMerged`.
template <const auto& A>
unsigned lr_settle_arm(LrContext& c, std::size_t n, unsigned count) {
  constexpr unsigned kArms = A.maxArms < 2u ? 2u : A.maxArms;
  // `lr_resolve_cands` seeds one arm per candidate, so the widest cell must
  // hold the widest position: this is what keeps `arms[n].set(k)` in bounds.
  static_assert(A.maxArms >= A.maxCands,
                "cursiv: this grammar's table declares a position wider than "
                "its widest cell");
  // The hooks that reach here are installed only where a reading can split,
  // which is where the goto classes are laid.
  static_assert(A.cold.gotoClass.size() == A.nstates,
                "cursiv: a table that decides cells or positions must lay "
                "its goto classes");
  using Arms = LrBitSet<kArms>;
  std::vector<LrShadow>& sh = c.spec;
  // Taken once: nothing below grows the pool but a fork, which takes it again.
  Arms* arms = lr_origin_room<kArms>(c, sh.size());
  const auto klass = [](std::uint32_t s) { return A.cold.gotoClass[s]; };
  unsigned tokens = 0;
  const std::size_t stepBudget = kLrStepBudget + c.stack.size();
  // The cap bounds only what the forks add to the seeded front. The
  // position's own room is free by now: whoever decides a cell has taken what
  // it held before asking.
  LrCellPool<A, kArms> x{c, sh, arms, n,
                         count > kLrMaxShadows ? count : kLrMaxShadows};
  // The furthest a refuted reading got, and the furthest any got.
  TextOffset refuted = 0;
  TextOffset reached = c.la.end;
  bool undecided = false;
  const auto unsettled = [&] {
    c.lookReach = reached;
    return kLrUnsettled;
  };
  const auto drop = [&](std::size_t i) CURSIV_ALWAYS_INLINE {
    --n;
    if (i != n) {
      std::swap(sh[i], sh[n]);
      std::swap(arms[i], arms[n]);
    }
  };
  const auto kill = [&](std::size_t i) {
    refuted = sh[i].at > refuted ? sh[i].at : refuted;
    drop(i);
  };
  for (;;) {
    // Narrower than this a cell has no more pairs to walk than it would
    // have marks to take.
    const bool marked = n > 3;
    if (marked)
      for (std::size_t i = 0; i < n; ++i) sh[i].mark = lr_shadow_mark(sh[i]);
    for (std::size_t i = 0; i < n; ++i)
      for (std::size_t j = i + 1; j < n;) {
        // One reading now: its arms fold, and neither is refuted.
        if ((!marked || sh[i].mark == sh[j].mark) &&
            lr_shadow_same(sh[i], sh[j], klass)) {
          if constexpr (kLrTrails<A>)
            if (!c.trailRoom.closed && !undecided &&
                !lr_trail_meet<A>(c, sh[i], sh[j], undecided)) {
              ++j;
              continue;
            }
          arms[i] |= arms[j];
          drop(j);
        } else {
          ++j;
        }
      }
    // Every reading is over: the search elects among the arms.
    if (n == 0) return x.crop ? unsettled() : kLrRefuted;
    // What the readings say between them: the lowest arm any of them stands
    // on -- where they carry derivations, the arm whose derivation each
    // carries --, and whether they all stand on the same ones. Two sets that
    // differ name two arms between them, so their union is never taken.
    unsigned lowest = count;
    bool agree = true, anyLive = false, one = true;
    TextOffset stood = 0;
    for (std::size_t i = 0; i < n; ++i) {
      anyLive = anyLive || !sh[i].done;
      agree = agree && arms[i] == arms[0];
      const unsigned b = kLrTrails<A> ? sh[i].arm : arms[i].first();
      one = one && b == (kLrTrails<A> ? sh[0].arm : b);
      if (b < lowest) lowest = b;
      if (sh[i].at > stood) stood = sh[i].at;
    }
    if (stood > reached) reached = stood;
    // Readings of one arm alone elect nothing: no other arm is left to meet
    // the derivation they carry.
    if constexpr (kLrTrails<A>) c.trailRoom.closed = one;
    // Readings of two arms the rule did not elect between: the graph does.
    const bool elected = !kLrTrails<A> || (one && !undecided);
    if (!anyLive) {
      if (x.crop) return unsettled();
      return !kLrTrails<A> || (agree && elected) ? lowest : kLrMerged;
    }
    // A rival does not win by merely still being open: until the survivors
    // have read past the token that refuted the others, the reading goes on.
    const bool past = refuted < stood;
    if (agree && past)
      return x.crop ? unsettled() : elected ? lowest : kLrMerged;
    if (tokens >= kLrLookBudget || x.lexes >= kLrLexBudget ||
        x.steps >= stepBudget) {
      ++c.stats.lookStops;
      // Out of room is not an answer.
      return unsettled();
    }
    // A round that only reduced read no text: the depth is what the readings
    // advanced through, and `steps` is what bounds a run of reduces. The
    // readings that stand furthest back read on to their next token, so two
    // that reduce by chains of different lengths meet at it, and only there
    // are they told to be one.
    const unsigned lexedAt = x.lexes;
    const auto keptAt = x.kept;
    TextOffset back = ~TextOffset{0};
    for (std::size_t i = 0; i < n; ++i)
      if (!sh[i].done && sh[i].at < back) back = sh[i].at;
    for (std::size_t i = 0; i < n; ++i) {
      if (sh[i].done || sh[i].at != back) continue;
      sh[i].shifted = false;
      while (!sh[i].done && !sh[i].dead && !sh[i].shifted &&
             x.steps < stepBudget && x.lexes < kLrLexBudget)
        lr_read_step<A>(c, x, i);
    }
    lr_elected_shift<A>(c, sh, n);
    for (std::size_t i = 0; i < n;) {
      if (sh[i].dead)
        kill(i);
      else
        ++i;
    }
    if (x.lexes != lexedAt || x.kept != keptAt) ++tokens;
  }
}

// One reading per arm of a cell, in table order. `seed` puts the token the arm
// stands for in hand and answers false where the arm is already dead.
template <const auto& A, class Seed>
CURSIV_ALWAYS_INLINE inline std::size_t lr_seed_arms(LrContext& c,
                                                     unsigned count,
                                                     Seed seed) {
  constexpr unsigned kArms = A.maxArms < 2u ? 2u : A.maxArms;
  std::size_t n = 0;
  if constexpr (kLrTrails<A>) {
    if (c.trail.capacity() == 0) c.trail.reserve(64);
    c.trail.clear();
    c.trailRoom.restart();
  }
  for (unsigned k = 0; k < count; ++k) {
    if (n == c.spec.size()) c.spec.emplace_back();
    LrShadow& s = c.spec[n];
    s.run.stand_on(c.stack);
    s.lexRank = 0;
    s.trail = kLrNoTrail;
    s.path = 0;
    s.arm = static_cast<std::uint16_t>(k);
    s.torn = false;
    s.at = c.committed;
    s.laMode = c.laMode;
    s.done = false;
    s.dead = false;
    s.virt = c.virt;
    if constexpr (kIndented<A.cfg>) s.indent = c.indent;
    s.keptAt = kLrNoKeep;
    LrBitSet<kArms>* const arms = lr_origin_room<kArms>(c, n + 1);
    arms[n] = {};
    arms[n].set(k);
    if (seed(s, k)) ++n;
  }
  return n;
}

// A cell or a lexical position: the seed differs, while exhaustion and the
// fallback to the graph follow the same path. A state whose look-ahead ran out
// on a cell, or on a position, goes to the graph on its later visits until the
// cursor passes what that look-ahead read: the memo spares reading the same
// text again, it proves nothing about the next occurrence. A cell that ran out
// says nothing of a position in its state.
template <const auto& A, LrSplit Split, class Seed>
CURSIV_ALWAYS_INLINE inline unsigned lr_resolve_seeded(
    LrContext& c, std::uint32_t state, unsigned count, Seed seed) {
  assert(c.drive != nullptr);
  const std::size_t at =
      std::size_t{state} * 2 + (Split == LrSplit::cell ? 0 : 1);
  if (at >= c.unsettled.size() || c.unsettled[at] <= c.la.begin) {
    const std::size_t levels = c.indentArena.size();
    const std::size_t n = lr_seed_arms<A>(c, count, seed);
    const unsigned r = lr_settle_arm<A>(c, n, count);
    // The levels the readings opened die with them: merges compare level
    // indices, and the parse's own pushes must find the arena they would have.
    if constexpr (kIndented<A.cfg>) c.indentArena.resize(levels);
    if (r < kLrMerged) return r;
    if (r == kLrUnsettled) {
      if (c.unsettled.empty())
        c.unsettled.assign(std::size_t{A.nstates} * 2, 0);
      if (c.lookReach > c.unsettled[at]) c.unsettled[at] = c.lookReach;
    }
  }
  c.split = Split;
  return kLrSplitCell;
}

// The cell `first + [0, count)`, decided: the answer is an index into it.
template <const auto& A>
unsigned lr_resolve(LrContext& c, std::uint32_t state, std::uint32_t first,
                    unsigned count) {
  return lr_resolve_seeded<A, LrSplit::cell>(
      c, state, count, [&](LrShadow& s, unsigned arm) {
        s.la = c.la;
        return lr_trail_apply<A>(c, s, first + arm);
      });
}

// The position several terminals match at, decided the same way: each
// candidate is an arm, seeded as the token the state would read, in the order
// the deterministic matcher would have answered. Two of one rank are a choice
// the grammar's words elect, as a cell's arms are.
template <const auto& A>
unsigned lr_resolve_cands(LrContext& c, std::uint32_t state, const Scan* arm,
                          unsigned count) {
  const TextOffset begin = c.la.begin;
  const bool ranked = lr_rivals_ranked(arm, count);
  return lr_resolve_seeded<A, LrSplit::position>(
      c, state, count, [&, begin](LrShadow& s, unsigned k) {
        s.la = Token{begin, arm[k].end, arm[k].term, c.laMode};
        if (ranked) s.lexRank = ~lex_weight(arm[k].tier);
        return true;
      });
}

}  // namespace cursiv
