// A flagged terminal wins a match only because a merged state offers it for
// another caller, or because a condition the gap may refuse offers it. Asked
// of the stack and its conditions before the lexer commits, it is taken off
// the position when no reduction path of the stack ends in shifting it, and
// the position is read again without it.
#pragma once

#include <cstdint>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrOverlayClimb.hpp"
#include "cursiv/engines/lr/runtime/LrSkipMemo.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/lr/runtime/LrWalkMemo.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_refusal.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What `state` alone answers on `term`, `holds` answering the conditions of
// the gap: 0 or 1, or -1 where it reduces by `prod`. A cell with several arms
// counts as taken.
template <const auto& A, class Holds>
int lr_climb_act(std::uint32_t state, SymbolId term, Holds&& holds,
                 std::uint32_t& prod) {
  std::uint32_t first = 0;
  unsigned count = 0;
  lr_action_run<A>(state, term, first, count);
  if (count == 0) return 0;
  if (count != 1) return 1;
  const LrAction x = lr_action_of(A.programs.op[first]);
  if (x.kind == LrActionKind::accept) return 1;
  if (x.kind != LrActionKind::reduce)
    return lr_shift_target<A>(x.kind, x.arg, holds) != kInvalidSymbol ? 1 : 0;
  prod = x.arg;
  return -1;
}

// The reductions a stack makes on `term` end in a shift or an accept. The walk
// ends: reductions on one lookahead cannot cycle, the generator refuses unit
// cycles. `whole` falls where the stack ran short.
template <const auto& A, class Climb, class Holds>
bool lr_climb_walk(Climb& climb, SymbolId term, Holds&& holds, bool& whole) {
  for (;;) {
    std::uint32_t prod = 0;
    const int act = lr_climb_act<A>(climb.back(), term, holds, prod);
    if (act >= 0) return act != 0;
    if (!lr_reduce_allowed<A>(prod, holds)) return false;
    if (A.cfg.prods[prod].len + 1u > climb.size()) {
      whole = false;
      return false;
    }
    if (lr_apply_reduce<A>(climb, prod, holds) == kInvalidSymbol) return false;
  }
}

// The same, answered from `memo` where a walk read the frames this stack
// repeats, and told to it where the walk asked no condition.
template <const auto& A, class Climb, class Holds>
bool lr_climb_takes(Climb climb, SymbolId term, Holds&& holds,
                    LrWalkMemo& memo) {
  const std::uint32_t size = climb.size();
  std::uint32_t prod = 0;
  if (const int act = lr_climb_act<A>(climb.peek(size - 1), term, holds, prod);
      act >= 0)
    return act != 0;
  const auto top = [&](std::uint32_t i) -> std::uint32_t {
    return i < size ? climb.peek(size - 1 - i) : LrWalkMemo::kNone;
  };
  if (const int known = memo.find(term, top); known >= 0) return known != 0;
  bool held = false, whole = true;
  const bool verdict = lr_climb_walk<A>(
      climb, term,
      [&](unsigned k) {
        held = true;
        return holds(k);
      },
      whole);
  if (!held && whole && !climb.bottom())
    memo.put(term, climb.reach(), verdict, top);
  return verdict;
}

// The terminals a position is read without.
template <const auto& A>
using LrSkipped = typename LrLexTape<A>::Skip;

// The token at `from` in `mode` under bracket depth `depth`, trivia crossed,
// the terminals of `skip` off the position. `crossed(end, trivia)` is told
// each stretch of trivia read: the bytes the mode skips (`trivia` null), or
// one token. A read that crossed none is the same for every reading that asks
// it at that depth.
template <const auto& A, class Crossed>
Token lr_scan_skipping(const LrContext& c, std::uint16_t mode, TextOffset from,
                       std::uint32_t depth, const LrSkipped<A>& skip,
                       Crossed&& crossed) {
  std::uint16_t names[LrSkipMemo::kSkips];
  std::uint32_t n = 0;
  bool named = true;
  {
    LrSkipped<A> open = skip;
    for (unsigned t = open.take(); t < A.cfg.nterms; t = open.take()) {
      if (n == LrSkipMemo::kSkips) {
        named = false;
        break;
      }
      names[n++] = static_cast<std::uint16_t>(t);
    }
  }
  if (named)
    if (const Token* seen = c.skipMemo.find(from, mode, depth, names, n))
      return *seen;
  const typename LrLexTape<A>::Skipping tape{.skip = &skip};
  bool crossing = false;
  TextOffset p = from;
  for (;;) {
    const TextOffset blank = p;
    while (p < c.len && lr_skips<A>(mode, static_cast<unsigned char>(c.src[p])))
      ++p;
    if (p != blank) {
      crossed(p, nullptr);
      crossing = true;
    }
    if (p >= c.len) return Token{p, p, kEofSymbol};
    const Scan s = lex_match(tape, c.src, c.len, mode, p);
    if (s.term == kInvalidSymbol || s.end == p)
      return Token{p, p, kInvalidSymbol};
    if (!lr_is_trivia<A>(s.term) && !lr_joins<A>(depth, s.term)) {
      const Token token{p, s.end, s.term, mode};
      if (named && !crossing)
        c.skipMemo.put(from, mode, depth, names, n, token);
      return token;
    }
    const Token trivia{p, s.end, s.term, mode};
    p = s.end;
    crossed(p, &trivia);
    crossing = true;
  }
}

// A reading's token `la`, a winner of `mode`, read again while it is a
// terminal the mode flags and the stack refuses it: `takes(la)` asks the
// stack, `rescan(skip, from)` reads the token from `from`, where the winner
// began, without the terminals of `skip`. Whether any was refused.
template <const auto& A, class Takes, class Rescan>
CURSIV_NOINLINE bool lr_lex_refused(Takes&& takes, std::uint16_t mode,
                                    Token& la, Rescan&& rescan) {
  LrSkipped<A> skip{};
  return lr_refused(
      [&](SymbolId sym) { return sym < A.cfg.nterms && A.flagged(mode, sym); },
      takes, la, [&](const Token& t) {
        skip.set(t.sym);
        return rescan(skip, t.begin);
      });
}

// The drive's token, a winner of `mode` that the mode flags: read again from
// where it began while the stack refuses it. The trivia before it stays as the
// lexer recorded it; a terminal the walk skips never wins over a trivia.
template <const auto& A>
CURSIV_NOINLINE void lr_drive_exact(LrContext& c, std::uint16_t mode) {
  Token la = c.la;
  const bool refused = lr_lex_refused<A>(
      [&](const Token& t) {
        return lr_climb_takes<A>(
            LrOverlayClimb<LrStack>(c.stack), t.sym,
            [&](unsigned k) { return c.drive->cond(c, k, c.virt.from, t); },
            c.walkMemo);
      },
      mode, la, [&](const LrSkipped<A>& skip, TextOffset from) {
        return lr_scan_skipping<A>(
            c, mode, from, c.indent.depth, skip,
            [&](TextOffset end, const Token* t) {
              if (t != nullptr && A.cfg.terms[t->sym].recorded)
                c.pending.push_back(*t);
              c.pendEnd = end;
            });
      });
  if (!refused) return;
  c.la = la;
  c.la.mode = mode;
  c.laMode = mode;
  c.ncand = 0;
}

}  // namespace cursiv
