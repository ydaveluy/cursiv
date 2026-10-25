// One action of a reading, taken as the drive takes it: the token in hand
// under the state's own mode, the default arm's fallbacks, and the table's
// answer. Every reading that is not the drive steps through it, whatever
// stack it stands on, in the indentation levels it owns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrOverlayClimb.hpp"
#include "cursiv/engines/lr/runtime/lr_guard.hpp"
#include "cursiv/engines/lr/runtime/lr_lex_exact.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// A pool with no room for another reading.
inline constexpr std::size_t kLrNoFork = ~std::size_t{0};

// A reading that can hold a token no lexer read.
template <class R>
inline constexpr bool kLrPins = requires(const R& r) { r.pin; };

template <const auto& A>
inline LrAction lr_shadow_action(std::uint32_t at) {
  // Read into 32 bits: shifted at a narrow width, GCC extends it again.
  return lr_action_of(A.programs.op[at]);
}

// Whether the cell of action `at` is ordered as the election orders it,
// whatever the text goes on to read.
template <const auto& A>
inline bool lr_order_exact(std::uint32_t at) {
  if constexpr (A.cold.orderExact.size() == 0) {
    return false;
  } else {
    return (at >> 6) < A.cold.orderExact.size() &&
           ((A.cold.orderExact[at >> 6] >> (at & 63u)) & 1u) != 0;
  }
}

// A held token answers conditions for the one it stands in for.
template <class R>
inline const Token& lr_cond_token(const R& s) {
  if constexpr (kLrPins<R>)
    if (s.pin && s.la == s.pinTok) return s.condTok;
  return s.la;
}

// A token no lexer read put in hand in `mode`: invented before the one in
// hand, or the author's bytes up to `end` read as `term`, whose conditions ask
// about `written`.
template <class R>
inline void lr_pin(R& r, SymbolId term, bool reread, TextOffset end,
                   const Token& written, std::uint16_t mode) {
  r.pin = true;
  r.pinRep = reread;
  r.pinTok = Token{r.la.begin, reread ? end : r.la.begin, term};
  r.condTok = reread ? written : r.pinTok;
  r.held = r.la;
  r.heldMode = r.laMode;
  r.la = r.pinTok;
  r.laMode = mode;
}

// What one reading's conditions answer, on the gap before its token.
template <class R>
inline auto lr_shadow_holds(const LrContext& c, const R& s) {
  return [&c, &s](unsigned k) {
    return c.drive->cond(c, k, s.virt.from, lr_cond_token(s));
  };
}

// A lone action the gap refuses -- a reduce whose condition fails, a guard
// that lands nowhere -- leaves the token without an action, as the drive
// sends it to the state's default arm.
template <const auto& A, class R>
inline void lr_shadow_unguard(const LrContext& c, const R& s,
                              std::uint32_t first, unsigned& count) {
  if constexpr (kLrAnyGap<A> || kLrGuarded<A>) {
    if (count != 1) return;
    const auto x = lr_shadow_action<A>(first);
    if (x.kind == LrActionKind::shift) return;
    if (x.kind == LrActionKind::guard) {
      if (lr_shift_target<A>(x.kind, x.arg, lr_shadow_holds(c, s)) ==
          kInvalidSymbol)
        count = 0;
      return;
    }
    if (x.kind != LrActionKind::reduce) return;
    if (!lr_reduce_allowed<A>(x.arg, lr_shadow_holds(c, s))) {
      count = 0;
      return;
    }
    // An empty reduction can be refused by its goto before touching the
    // stack. It then leaves the token to the same fallbacks as a refused
    // shift guard, including an implied token the state may lay.
    if constexpr (kLrGuarded<A>)
      if (A.cfg.prods[x.arg].len == 0 && A.cfg.prods[x.arg].guarded != 0 &&
          lr_reduce_target<A>(s.run.back(), x.arg, kLrNoFirst,
                              lr_shadow_holds(c, s)) == kInvalidSymbol)
        count = 0;
  } else {
    (void)c, (void)s, (void)first, (void)count;
  }
}

// Production `prod` closed on one reading. False where the reading dies of it.
// The cursor crosses the gap where the drive's reduction flushes, no other:
// past a backslash it would stand on a line start.
template <const auto& A, class R>
inline bool lr_shadow_reduce(const LrContext& c, R& s, std::uint16_t prod) {
  if (A.derived.reductions[prod].flushes && s.laMode != kNoMode &&
      s.la.begin > s.at)
    s.at = s.la.begin;
  return lr_apply_reduce<A>(s.run, prod, lr_shadow_holds(c, s)) !=
         kInvalidSymbol;
}

// What shifting the token in hand does to the reading's levels.
template <const auto& A, class R>
inline void lr_read_indent_shift(LrContext& c, R& s) {
  if constexpr (kLrPins<R>)
    if (s.pin && s.la == s.pinTok) {
      indent_shift_invented<A.cfg>(c.indentArena, s.indent, s.la);
      return;
    }
  indent_shift<A.cfg>(c.indentArena, s.indent, s.la);
}

// One action on one reading. False where the reading dies of it.
template <const auto& A, class R>
inline bool lr_shadow_apply(LrContext& c, R& s, std::uint32_t at) {
  const auto x = lr_shadow_action<A>(at);
  if (lr_takes_token_in<A>(x.kind)) {
    const std::uint32_t to =
        lr_shift_target<A>(x.kind, x.arg, lr_shadow_holds(c, s));
    if constexpr (kLrGuarded<A>)
      if (x.kind == LrActionKind::guard && to == kInvalidSymbol) return false;
    if constexpr (kIndented<A.cfg>) lr_read_indent_shift<A>(c, s);
    // An invented or implied token stands where the drive committed: the
    // author's trivia before the token behind it is still to be read.
    const bool implied = kLrImplied<A>.size() != 0 && s.virt.implies(s.la);
    if constexpr (kLrConditioned<A>) s.virt.shift(s.la);
    s.run.push(to);
    s.laMode = kNoMode;
    if constexpr (kLrPins<R>) {
      if (!implied && (!s.pin || s.pinRep || !(s.la == s.pinTok)))
        s.at = s.la.end;
      s.template shifted<A>(A.cfg.terms[s.la.sym].laid != 0);
    } else if (!implied) {
      s.at = s.la.end;
    }
    return true;
  }
  if (x.kind == LrActionKind::accept) {
    s.done = true;
    return true;
  }
  if (!lr_reduce_allowed<A>(x.arg, lr_shadow_holds(c, s))) return false;
  return lr_shadow_reduce<A>(c, s, x.arg);
}

// No token at all -- a blank a tight mode reads nothing of -- still answers
// the one reduce a condition guards, as the drive's default arm does: the
// production, or `kLrNoProd`.
template <const auto& A, class R>
inline std::uint16_t lr_shadow_lone_guard(const LrContext& c,
                                          std::uint32_t state, const R& s) {
  if constexpr (kLrAnyGap<A>) {
    if (s.la.sym != kInvalidSymbol || A.modeTight[A.mode[state]] == 0)
      return kLrNoProd;
    const auto p = A.cold.soleReduce[state];
    if (p == kLrNoProd || A.cfg.prods[p].gap == 0 ||
        !c.drive->cond(c, A.cfg.prods[p].gap - 1u, s.virt.from, s.la))
      return kLrNoProd;
    return p;
  } else {
    (void)c, (void)state, (void)s;
    return kLrNoProd;
  }
}

// The token the indentation says where the reading stands, if one.
template <const auto& A, class R>
inline bool lr_read_indent(LrContext& c, R& s, std::uint16_t mode) {
  const TextOffset at = s.at;
  const auto held = [at] { return at; };
  Token t;
  if (s.indent.depth != 0 ||
      !indent_virtual_at(c.src, c.len, c.indentArena, s.indent.head,
                         kLaid<A.cfg, kLaidIndent>, kLaid<A.cfg, kLaidDedent>,
                         at, t, held))
    return false;
  s.la = t;
  s.laMode = mode;
  return true;
}

// The actions offered after the token's mode and virtual-token fallbacks
// have settled. A lone guarded reduction can also answer an unmatched byte.
struct LrReadActions {
  std::uint32_t first = 0;
  unsigned count = 0;
  std::uint16_t lone = kLrNoProd;
};

// Resolve the token before choosing an action. Relex may grow the reading's
// pool, so `reading()` reacquires it after every call instead of retaining a
// reference. The repair writer uses the same decision over its pinned token.
template <const auto& A, class Reading, class Relex, class Kept>
CURSIV_ALWAYS_INLINE inline LrReadActions lr_read_actions(LrContext& c,
                                                          Reading&& reading,
                                                          Relex&& relex,
                                                          Kept&& kept) {
  const std::uint32_t state = reading().run.back();
  const std::uint16_t mode = A.mode[state];
  if (reading().laMode != mode) {
    // No token stands across a line start the indentation answers, as
    // in the drive; one that stands is charged once, not once per round
    // that only reduces under it.
    if (kIndented<A.cfg> || reading().laMode == kNoMode ||
        !A.mode_sub(reading().laMode, mode) ||
        lr_has_rivals<A>(mode, reading().la.sym))
      relex();
    else
      kept();
  }
  std::uint32_t f = 0;
  unsigned k = 0;
  lr_action_run<A>(state, reading().la.sym, f, k);
  lr_shadow_unguard<A>(c, reading(), f, k);
  if (k == 0 && reading().laMode != mode) {
    relex();
    lr_action_run<A>(state, reading().la.sym, f, k);
    lr_shadow_unguard<A>(c, reading(), f, k);
  }
  // The end of input while suites are open is a dedent where the state reads
  // one or refuses the end, as the drive hands it over.
  if constexpr (kLaid<A.cfg, kLaidDedent> != 0) {
    if (reading().la.sym == kEofSymbol &&
        indent_owes_dedent(c.indentArena, reading().indent.head)) {
      std::uint32_t closing = 0;
      unsigned reads = 0;
      lr_action_run<A>(state, kLaid<A.cfg, kLaidDedent>, closing, reads);
      if (k == 0 || reads != 0) {
        reading().la = Token{reading().la.begin, reading().la.begin,
                             kLaid<A.cfg, kLaidDedent>};
        f = closing;
        k = reads;
      }
    }
  }
  // A refused token is an implied one where the state answers it and
  // its condition holds, as the drive hands it over.
  if constexpr (kLrImplied<A>.size() != 0) {
    auto& v = reading().virt;
    if (k == 0 && v.pending == 0) {
      for (unsigned q = 0; q < kLrImplied<A>.size() && k == 0; ++q) {
        const SymbolId term = kLrImplied<A>[q];
        if (((v.laid_at(reading().la) >> q) & 1u) != 0) continue;
        std::uint32_t f2 = 0;
        unsigned k2 = 0;
        lr_action_run<A>(state, term, f2, k2);
        if (k2 == 0 || !c.drive->cond(c, A.cfg.terms[term].cond, v.from,
                                      lr_cond_token(reading())))
          continue;
        v.lay(reading().la, q, term);
        f = f2;
        k = k2;
      }
    }
  }
  return {f, k,
          k == 0 ? lr_shadow_lone_guard<A>(c, state, reading()) : kLrNoProd};
}

// The token a reading holds, a flagged winner of `mode`, read again while its
// stack refuses it.
template <const auto& A, class R>
CURSIV_NOINLINE bool lr_read_refused(LrContext& c, R& s, std::uint16_t mode) {
  return lr_lex_refused<A>(
      [&](const Token& t) {
        return lr_climb_takes<A>(
            LrOverlayClimb<std::remove_cvref_t<decltype(s.run)>>(s.run), t.sym,
            lr_shadow_holds(c, s), c.walkMemo);
      },
      mode, s.la, [&](const LrSkipped<A>& skip, TextOffset from) {
        const IndentAt was = c.indent;
        c.indent = s.indent;
        const Token t = lr_scan_skipping<A>(c, mode, from, skip,
                                            [](TextOffset, const Token*) {});
        c.indent = was;
        return t;
      });
}

// One action of reading `i` of pool `x`: `x.fork(i)` is a copy beside it or
// `kLrNoFork`, `x.drop(j)` a fork its action refused, `x.apply` and `x.reduce`
// false where the reading dies, `x.pick`, optional, the candidate it takes;
// the pool counts its work in `lexed`, `kept_at` and `stepped`. A
// reading the table refuses is marked `dead` and left as it stood.
template <const auto& A, class Pool>
CURSIV_ALWAYS_INLINE inline void lr_read_step(LrContext& c, Pool& x,
                                              std::size_t i) {
  using R = std::remove_cvref_t<decltype(x[i])>;
  const std::uint32_t state = x[i].run.back();
  const std::uint16_t mode = A.mode[state];
  // The token in hand stands where its mode covers this one, as it does in
  // the drive; a state with no action for it lexes again.
  const auto relex = [&]() CURSIV_ALWAYS_INLINE {
    if constexpr (requires { x[i].lexRank; }) x[i].lexRank = 0;
    // An implied token not yet shifted stands, whatever the mode.
    if constexpr (kLrImplied<A>.size() != 0) {
      if (x[i].virt.pending != 0) {
        x[i].la = x[i].virt.in_hand();
        x[i].laMode = mode;
        return;
      }
    }
    if constexpr (kLrPins<R>) {
      if (x[i].pin) {
        x[i].la = x[i].pinTok;
        x[i].laMode = mode;
        return;
      }
    }
    // From what the reading committed, never from the token it holds, as
    // the drive re-reads a mode change: the trivia between the two is the
    // old mode's alone.
    if constexpr (kIndented<A.cfg>) {
      if (lr_read_indent<A>(c, x[i], mode)) {
        x.lexed();
        return;
      }
    }
    x[i].la = x.scan(i, mode);
    x[i].laMode = mode;
    x.lexed();
    [[maybe_unused]] bool refused = false;
    if constexpr (A.has_flags())
      if (x[i].la.sym < A.cfg.nterms && A.flagged(mode, x[i].la.sym))
        [[unlikely]]
        refused = lr_read_refused<A>(c, x[i], mode);
    // The drive would have this position's rivals in hand, so a
    // reading takes one fork each, not the longest match alone.
    if constexpr (A.maxCands > 1) {
      if (!refused && c.drive->scanText != nullptr &&
          lr_has_rivals<A>(mode, x[i].la.sym)) {
        Scan* const room = c.cand;
        const std::uint32_t nc = lr_rivals<A>(c, mode, x[i].la.begin, room);
        if constexpr (requires { x.pick(i, state, room, nc); }) {
          if (nc != 0) x.pick(i, state, room, nc);
        } else {
          // Where the lexer ranks rivals apart, a token's rank is a word of
          // the derivation that reads it.
          [[maybe_unused]] const bool ranked = lr_rivals_ranked<A>(room, nc);
          [[maybe_unused]] const auto rank = [&](const Scan& scan) {
            return ranked ? ~lex_weight(scan.tier) : 0u;
          };
          for (std::uint32_t q = 1; q < nc; ++q) {
            if (room[q].term == room[0].term) continue;
            const std::size_t j = x.fork(i);
            if (j == kLrNoFork) break;
            x[j].la = Token{x[i].la.begin, room[q].end, room[q].term};
            if constexpr (requires { x[j].lexRank; })
              x[j].lexRank = rank(room[q]);
          }
          if (nc != 0) {
            x[i].la = Token{x[i].la.begin, room[0].end, room[0].term};
            if constexpr (requires { x[i].lexRank; })
              x[i].lexRank = rank(room[0]);
          }
        }
      }
    }
  };
  const auto actions = lr_read_actions<A>(
      c, [&]() -> R& { return x[i]; }, relex, [&] { x.kept_at(i); });
  if (actions.count == 0) {
    if (actions.lone != kLrNoProd) {
      if (!x.reduce(i, actions.lone)) x[i].dead = true;
      x.stepped();
    } else {
      x[i].dead = true;
    }
    return;
  }
  bool went = false;
  for (unsigned j = 1; j < actions.count; ++j) {
    // An arm no reading was opened for is unread, not refuted: a cell the
    // room cut may not name a winner.
    const std::size_t fi = x.fork(i);
    if (fi == kLrNoFork) break;
    if (x.apply(fi, actions.first + j))
      went = true;
    else
      x.drop(fi);
  }
  if (!x.apply(i, actions.first)) {
    x[i].dead = true;
    // Beside an arm that went on, the drive does not stop here: a search's
    // reading is gone, not a pause.
    if constexpr (requires { x[i].gone; }) x[i].gone = went;
  }
  x.stepped();
}

}  // namespace cursiv
