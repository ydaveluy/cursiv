// A misspelling is repaired by re-reading, never by rewriting: a cold walk
// over the mode's DFA, k faults and no edit distance, says how far a terminal
// the state can shift would have reached.
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/shared/runtime/TypoCandidates.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// One walk's table, as `typo_each` reads it.
template <const auto& A>
struct LrTypoTape : TypoBounds {
  using Set = std::remove_cvref_t<decltype(A.cold.follow[0])>;

  // The terminals this state can act on, shift or reduce: a state reached
  // right after a modifier shifts nothing.
  Set* want;
  std::uint32_t* stops;
  // Where the token begins: a keyword's own refusal is asked of its head.
  TextOffset head = 0;

  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    return typo_successors(A.derived.typoSuccBase.data(),
                           A.derived.typoSucc.data(), s);
  }
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    ++*read;
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
    const auto refused = lr_refused_from<A>(src, head, docLen);
    return lex_reserves<A>(s, src, p, docLen, unseen,
                           [&](SymbolId t) { return !refused(t); });
  }
  // A spelling a keyword reserved is not a word terminal, whatever the walk
  // paid to reach it. Declaration order breaks ties.
  template <class Emit>
  void owed(std::uint16_t s, TextOffset p, Emit emit) const {
    if (A.dfaAccept[s] == 0) return;
    Set open = A.dfa_acc_set(s);
    if (reserves(s, p)) open.andnot(A.dfa_res_set(s));
    open &= *want;
    for (unsigned t = open.take(); t < A.cfg.nterms; t = open.take())
      if (closed(s, t, p) && !trails(t, p)) emit(static_cast<SymbolId>(t));
  }
  // A word the terminal refuses where its token ends, spelled behind the
  // re-reading: its head may be the one misspelt.
  bool trails(unsigned t, TextOffset p) const {
    return A.cfg.terms[t].refuses &&
           A.tail_refuses(static_cast<SymbolId>(t), src + p, docLen - p);
  }
  // The boundary is the one the ACCEPTANCE declares: a repair may not read
  // what the lexer would refuse. An unbounded terminal still ends at a word
  // edge -- a repair cutting a word in two is a cascade, not a reading.
  bool closed(std::uint16_t s, unsigned t, TextOffset p) const {
    if (p >= docLen) return true;
    if (inside_scalar(src, p, docLen)) return false;
    const std::uint16_t set = A.dfa_acc_bound(s, t);
    return set == kNoSet ? !lex_boundary(A.cfg).word_at(src, p, docLen)
                         : !lex_boundary(A.cfg).at(set, src, p, docLen);
  }
  void refused() const { ++*stops; }
};

// Where the text a state reads ends: the document's end, or in a section the
// first token that closes it. Looked for no further than `upto`. The bytes
// read are paid into `read`; a failed walk counts once however many starts
// it serves.
template <const auto& A>
TextOffset lr_text_end(const LrContext& c, std::uint32_t state, TextOffset at,
                       TextOffset upto, std::uint64_t& read) {
  const SymbolId close = lr_text_close<A>(state);
  if (close == kEofSymbol) return c.len;
  const std::uint16_t err = lr_err_mode<A>(A.mode[state]);
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

// The re-readings of a misspelling from `at`, over the terminals `state` can
// act on, each (terminal, end) once at its fewest faults, in rank order:
// fewer faults first, then the longer span, then the walk's declaration
// order. The walk bounds how many there are; `reach` bounds how far past the
// token it reads, `most` what all its paths read together, and `read` counts
// what it read.
template <const auto& A>
std::vector<Typo> lr_rescan_candidates(LrContext& c, std::uint32_t state,
                                       TextOffset at, TextOffset reach,
                                       std::uint64_t& read, bool& cut,
                                       std::uint64_t most = ~std::uint64_t{0}) {
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
      c, state, at, c.len - at > 2 * reach ? at + 2 * reach : c.len, read);
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
  typo_each(LrTypoTape<A>{{c.src, horizon, len, &read, &cut, most}, &want,
                          &c.stats.searchStops, at},
            A.modeStart[A.mode[state]], at, plain, keep);
  return found.ranked();
}

}  // namespace cursiv
