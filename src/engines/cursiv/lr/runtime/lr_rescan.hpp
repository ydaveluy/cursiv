// A misspelling is repaired by re-reading, never by rewriting: a cold search
// over the mode's DFA, a bounded edit distance, says how far a terminal the
// state can shift would have reached, and at what cost.
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/lr_lex_reading.hpp"
#include "cursiv/shared/runtime/TypoCandidates.hpp"
#include "cursiv/shared/runtime/TypoTape.hpp"
#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

// One walk's table, as `typo_each` reads it.
template <const auto& A>
struct LrTypoTape : TypoTape<A> {
  using Set = std::remove_cvref_t<decltype(A.cold.follow[0])>;

  // The terminals this state can act on, shift or reduce: a state reached
  // right after a modifier shifts nothing.
  Set* want;
  std::uint32_t* stops;

  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    return typo_successors(A.derived.typoSuccBase.data(),
                           A.derived.typoSucc.data(), s);
  }
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    ++*this->read;
    return A.next_byte(s, b);
  }
  // A walk standing where no terminal it wants can end offers nothing more:
  // inside a comment, it would read on to the comment's end for nothing.
  bool alive(std::uint16_t s) const {
    Set left = A.derived.typoReach[s];
    left &= *want;
    return left.any();
  }
  // A keyword the source as written spells reserves its spelling away from
  // every reading through it, unless its own condition refuses it there.
  bool original(std::uint16_t s, TextOffset p) const {
    if (reserves(s, p)) want->andnot(A.dfa_res_set(s));
    return want->any();
  }
  bool reserves(std::uint16_t s, TextOffset p) const {
    if (!A.dfa_res_set(s).any()) return false;
    TextOffset unseen = p;
    return lex_reserves<A>(
        s, this->src, p, this->docLen, unseen, [&](SymbolId t) {
          return !LexTape<A>{}.refuses(t, this->src + this->head,
                                       this->docLen - this->head);
        });
  }
  // A spelling a keyword reserved is not a word terminal, whatever it cost the
  // walk to reach it. Declaration order breaks ties.
  template <class Emit>
  void accepted(std::uint16_t s, TextOffset p, Emit emit) const {
    if (A.dfaAccept[s] == 0) return;
    Set open = A.dfa_acc_set(s);
    if (reserves(s, p)) open.andnot(A.dfa_res_set(s));
    open &= *want;
    for (unsigned t = open.take(); t < A.cfg.nterms; t = open.take()) {
      const auto term = static_cast<SymbolId>(t);
      if (this->closed(s, term, p) && !this->refused_after(term, p)) emit(term);
    }
  }
  void refused() const { ++*stops; }
};

// Where the text `mode` reads ends: the document's end, or in a section the
// first closing token, looked for up to `upto`. The bytes read go into `read`;
// a failed walk counts once.
template <const auto& A>
TextOffset lr_text_end(const LrContext& c, std::uint16_t mode, TextOffset at,
                       TextOffset upto, std::uint64_t& read) {
  const SymbolId close = lr_mode_close<A>(mode);
  if (close == kEofSymbol) return c.len;
  const std::uint16_t err = lr_mode_text<A>(mode);
  LexFailedWalks<LrLexTape<A>> failed;
  TextOffset reached = at;
  for (TextOffset p = at; p < c.len && p < upto;) {
    c.stop.poll();
    TextOffset halt = p;
    Scan s;
    const bool fails = failed.fails(LrLexTape<A>{}, c.src, c.len, err, p, halt,
                                    [&] { c.stop.poll(); });
    if (!fails) s = lr_match_halt<A>(c.src, c.len, err, p, halt);
    const TextOffset from = fails ? std::max(p, reached) : p;
    read += std::max(halt, from) - from;
    reached = std::max(reached, halt);
    if (s.term == kInvalidSymbol || s.end <= p) {
      ++p;
      continue;
    }
    if (s.term == close) return p;
    p = s.end;
  }
  return c.len;
}

// The lexer's refused reading at `p` under alphabet `m`, the text ending at
// `end` or at the token closing `mode`'s section, looked for only as far as the
// reading read. `read` adds the closer's search.
template <const auto& A>
LexReading lr_open_reading(const LrContext& c, std::uint16_t mode,
                           std::uint16_t m, TextOffset p, TextOffset end,
                           TextOffset& reached, std::uint64_t& read) {
  LexReading r = lex_reading(LrLexTape<A>{}, c.src, end, m, p, reached);
  if (const TextOffset close = lr_text_end<A>(c, mode, p, reached, read);
      close < reached) {
    reached = p;
    r = lex_reading(LrLexTape<A>{}, c.src, close, m, p, reached);
  }
  return r;
}

// The re-readings of a misspelling from `at` over the terminals `state` can act
// on, each (terminal, end) once at its least cost: cheapest, then longest, then
// declaration order. `reach` bounds how far a path reads and the bytes all of
// them read, which `read` counts.
template <const auto& A>
std::vector<Typo> lr_rescan_candidates(LrContext& c, std::uint32_t state,
                                       TextOffset at, TextOffset reach,
                                       std::uint64_t& read, bool& cut,
                                       TypoScratch* scratch = nullptr) {
  auto want = A.cold.follow[state];
  // A word the source as written spells behind a terminal's head refuses
  // that terminal to every reading from there.
  if constexpr (LexTape<A>::kRefuses)
    for (unsigned t = 0; t < A.cfg.nterms; ++t)
      if (A.cfg.terms[t].refuses && want.test(t) &&
          A.head_refuses(static_cast<SymbolId>(t), c.src + at, c.len - at))
        want.reset(t);
  if (want.none()) return {};
  const TextOffset len = lr_text_end<A>(
      c, A.mode[state], at, c.len - at > 2 * reach ? at + 2 * reach : c.len,
      read);
  // Head faults must end at `plain`, the widest ordinary token. An internal
  // fault may join fragments beyond it.
  const TextOffset cap = len - at > reach ? at + reach : len;
  TextOffset halt = at;
  const TextOffset plain =
      lr_match_halt<A>(c.src, cap, lr_err_mode<A>(A.mode[state]), at, halt).end;
  read += halt - at;
  if (halt >= cap && cap < len) {
    cut = true;
    ++c.stats.searchStops;
    return {};
  }
  const TextOffset horizon = typo_horizon(at, plain, len, reach);
  TypoCandidates found;
  const auto keep = [&](const Typo& candidate) { found.add(candidate); };
  const TypoBounds bounds{c.src, horizon, len,     &read,
                          &cut,  reach,   scratch, &c.stop};
  typo_each(LrTypoTape<A>{{bounds, at}, &want, &c.stats.searchStops},
            A.modeStart[A.mode[state]], at, plain, keep);
  return found.ranked();
}

}  // namespace cursiv::engine
