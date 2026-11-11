// A flagged terminal wins a match only because a merged state offers it for
// another caller, or a condition may refuse the gap: it is taken off where no
// reduction path of the stack shifts it.
#pragma once

#include <cstdint>

#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrSkipMemo.hpp"
#include "cursiv/lr/runtime/LrStack.hpp"
#include "cursiv/lr/runtime/LrStackOverlay.hpp"
#include "cursiv/lr/runtime/LrWalkMemo.hpp"
#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/lr/runtime/lr_refusal.hpp"
#include "cursiv/lr/runtime/lr_states.hpp"
#include "cursiv/lr/table/LrAction.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// Whether an arm of a cell of several takes `term`, `holds` answering the
// conditions of the gap. Conditions are read through empty rules, as the
// generator's flags are: an arm that reduces anything else counts as taken.
template <const auto& A, class Holds>
bool lr_cell_takes(std::uint32_t state, SymbolId term, Holds&& holds) {
  return lr_cell_walk(state, [&](std::uint32_t at, auto&& leads) {
    std::uint32_t first = 0;
    unsigned count = 0;
    lr_action_run<A>(at, term, first, count);
    for (unsigned k = 0; k < count; ++k) {
      const LrAction x = lr_action_of(A.programs.op[first + k]);
      if (x.kind == LrActionKind::accept) return true;
      if (x.kind != LrActionKind::reduce) {
        if (lr_shift_target<A>(x.kind, x.arg, holds) != kInvalidSymbol)
          return true;
        continue;
      }
      if (!lr_reduce_allowed<A>(x.arg, holds)) continue;
      if (A.cfg.prods[x.arg].len != 0) return true;
      const auto to = lr_reduce_target<A>(at, x.arg, kLrNoFirst, holds);
      if (to != kInvalidSymbol) leads(to);
    }
    return false;
  });
}

// What `state` alone answers on `term`, `holds` answering the conditions of
// the gap: 0 or 1, or -1 where it reduces by `prod`.
template <const auto& A, class Holds>
int lr_state_act(std::uint32_t state, SymbolId term, Holds&& holds,
                 std::uint32_t& prod) {
  std::uint32_t first = 0;
  unsigned count = 0;
  lr_action_run<A>(state, term, first, count);
  if (count == 0) return 0;
  if (count != 1) return lr_cell_takes<A>(state, term, holds) ? 1 : 0;
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
template <const auto& A, class Stack, class Holds>
bool lr_reduce_walk(Stack& stack, SymbolId term, Holds&& holds, bool& whole) {
  for (;;) {
    std::uint32_t prod = 0;
    const int act = lr_state_act<A>(stack.back(), term, holds, prod);
    if (act >= 0) return act != 0;
    if (!lr_reduce_allowed<A>(prod, holds)) return false;
    if (A.cfg.prods[prod].len + 1u > stack.size()) {
      whole = false;
      return false;
    }
    if (lr_apply_reduce<A>(stack, prod, holds) == kInvalidSymbol) return false;
  }
}

// The same, answered from `memo` where a walk read the frames this stack
// repeats, and told to it where the walk asked no condition.
template <const auto& A, class Stack, class Holds>
bool lr_stack_takes(Stack stack, SymbolId term, Holds&& holds,
                    LrWalkMemo& memo) {
  const std::uint32_t size = stack.size();
  std::uint32_t prod = 0;
  if (const int act = lr_state_act<A>(stack.peek(size - 1), term, holds, prod);
      act >= 0)
    return act != 0;
  const auto top = [&](std::uint32_t i) -> std::uint32_t {
    return i < size ? stack.peek(size - 1 - i) : LrWalkMemo::kNone;
  };
  if (const int known = memo.find(term, top); known >= 0) return known != 0;
  bool held = false, whole = true;
  const bool verdict = lr_reduce_walk<A>(
      stack, term,
      [&](unsigned k) {
        held = true;
        return holds(k);
      },
      whole);
  if (!held && whole && !stack.bottom())
    memo.put(term, stack.reach(), verdict, top);
  return verdict;
}

// The terminals a position is read without.
template <const auto& A>
using LrSkipped = typename LrLexTape<A>::Skip;

// The token at `from` in `mode` under bracket depth `depth`, trivia crossed,
// `skip` off the position. `crossed(end, trivia)` receives each stretch of
// trivia. A read that crossed none is the same on one side of a bracket.
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
    if (const Token* seen = c.skipMemo.find(from, mode, depth != 0, names, n))
      return *seen;
  const typename LrLexTape<A>::Skipping tape{.skip = &skip};
  bool crossing = false;
  const Token token = lr_cross_trivia<A>(
      c, mode, from, c.len, depth,
      [&](TextOffset p) { return lex_match(tape, c.src, c.len, mode, p); },
      [&](TextOffset to, const Token* trivia) {
        crossed(to, trivia);
        crossing = true;
      });
  if (named && !crossing && token.sym < A.cfg.nterms &&
      token.sym != kEofSymbol)
    c.skipMemo.put(from, mode, depth != 0, names, n, token);
  return token;
}

// What ties with `la`, a token a refusal elected. `skip` holds what was
// refused. The count is `room`'s, with `la` first; none where the mode holds no
// competitors.
template <const auto& A, class Takes>
CURSIV_NOINLINE std::uint32_t lr_refused_candidates(
    const LrContext& c, std::uint16_t mode, std::uint32_t depth,
    const Token& la, LrSkipped<A> skip, Scan* room, Takes&& takes) {
  if (A.modeMulti[mode] != 1 || la.sym >= A.cfg.nterms) return 0;
  const auto priority = [&](const Token& t) {
    return static_cast<std::uint8_t>(lex_reading_priority(
        LrLexTape<A>{}, c.src, c.len, mode, t.begin, t.end, t.sym));
  };
  room[0] = Scan{la.end, la.sym, priority(la)};
  std::uint32_t n = 1;
  lr_refused_ties(
      la, A.cfg.nterms,
      [&](const Token& at) {
        skip.set(at.sym);
        return lr_scan_skipping<A>(c, mode, la.begin, depth, skip,
                                   [](TextOffset, const Token*) {
                                     /* what crosses a trivia ties with nothing */
                                   });
      },
      [&](const Token& at) { return lex_weight(priority(at)); },
      [&](SymbolId sym) { return A.flagged(mode, sym); }, takes,
      [&](const Token& at) {
        room[n++] = Scan{at.end, at.sym, priority(at)};
        return n < A.maxCands;
      });
  return n;
}

// `la`, a winner `mode` flags, read again from where it began while `takes(la)`
// refuses it. `crossed` receives the trivia left; ties with the new winner go
// to `room`, `tied` its count. Whether any was refused.
template <const auto& A, class Takes, class Crossed>
CURSIV_NOINLINE bool lr_lex_refused(const LrContext& c, std::uint16_t mode,
                                    std::uint32_t depth, Token& la, Takes&& takes,
                                    Crossed&& crossed, Scan* room,
                                    std::uint32_t& tied) {
  LrSkipped<A> skip{};
  const bool refused = lr_refused(
      [&](SymbolId sym) { return sym < A.cfg.nterms && A.flagged(mode, sym); },
      takes, la, [&](const Token& t) {
        skip.set(t.sym);
        return lr_scan_skipping<A>(c, mode, t.begin, depth, skip, crossed);
      });
  if constexpr (A.maxCands > 1)
    if (refused)
      tied = lr_refused_candidates<A>(c, mode, depth, la, skip, room, takes);
  return refused;
}

// The drive's token, a flagged winner, read again while the stack refuses it;
// ties with the new winner stay candidates. Trivia left where the terminal is
// refused is crossed and recorded.
template <const auto& A>
CURSIV_NOINLINE void lr_drive_exact(LrContext& c, std::uint16_t mode) {
  Token la = c.la;
  const auto takes = [&](const Token& t) {
    return lr_stack_takes<A>(
        LrStackOverlay<LrStack>(c.stack), t.sym,
        [&](unsigned k) { return c.drive->cond(c, k, c.virt.from, t); },
        c.walkMemo);
  };
  std::uint32_t tied = 0;
  const bool refused = lr_lex_refused<A>(
      c, mode, c.indent.depth, la, takes,
      [&](TextOffset end, const Token* t) {
        if (t != nullptr && A.cfg.terms[t->sym].recorded) c.pending.push_back(*t);
        c.pendEnd = end;
      },
      c.cand, tied);
  if (!refused) return;
  c.la = la;
  c.la.mode = mode;
  c.laMode = mode;
  c.ncand = tied > 1 ? tied : 0;
}

}  // namespace cursiv::engine
