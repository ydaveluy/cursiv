// The decisions a reduction writes into a derivation, read off the table: the
// graph and the look-ahead lay them alike, so both elect one derivation.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/lr/table/LrColdTables.hpp"

namespace cursiv {

inline constexpr std::uint32_t kLrNoRun = ~std::uint32_t{0};

// The run of pass-throughs the table may skip from `rule` down to `sym`.
template <const auto& A>
std::uint32_t lr_elect_run(std::uint32_t rule, std::uint32_t sym) {
  const auto& keys = A.cold.electChainKey;
  const std::uint32_t key = rule << 16 | sym;
  const auto it = std::lower_bound(keys.begin(), keys.end(), key);
  return it != keys.end() && *it == key
             ? static_cast<std::uint32_t>(it - keys.begin())
             : kLrNoRun;
}

// The run a node of `symbol` laid into state `to` stands under, where the
// table skipped pass-throughs into it.
template <const auto& A>
std::uint32_t lr_elect_stands(std::uint32_t to, std::uint32_t symbol) {
  if constexpr (A.cold.electSym.size() != 0) {
    const std::uint32_t stands = A.cold.electSym[to];
    if (stands != kLrNoStateSym && stands != symbol && stands >= A.cfg.nterms)
      return lr_elect_run<A>(stands - A.cfg.nterms, symbol);
  }
  return kLrNoRun;
}

// Of the productions fused states name as `prod`, the one children standing
// on `syms` read; a seed's frame stands on no symbol and reads as any.
template <const auto& A>
std::uint16_t lr_elect_twin(std::uint16_t prod,
                            std::span<const std::uint32_t> syms) {
  const std::uint32_t first = A.cold.electTwinAt[prod];
  const std::uint32_t end = A.cold.electTwinAt[prod + 1u];
  for (std::uint32_t k = first; k != end; ++k) {
    const std::uint16_t q = A.cold.electTwin[k];
    bool reads = true;
    for (std::uint32_t i = A.cold.electRhsAt[q], j = 0;
         reads && i != A.cold.electRhsAt[q + 1u]; ++i, ++j) {
      const std::uint32_t want = A.cold.electRhs[i];
      reads = syms[j] == want || syms[j] == 0 ||
              (want >= A.cfg.nterms &&
               lr_elect_run<A>(want - A.cfg.nterms, syms[j]) != kLrNoRun);
    }
    if (reads) return q;
  }
  return prod;
}

// The decisions of run `run` on `side` of its callee, before (0) or after
// (1), each to `each`.
template <const auto& A, class Each>
void lr_elect_chain(std::uint32_t run, std::uint16_t side, Each&& each) {
  for (std::uint32_t k = A.cold.electChainAt[run];
       k < A.cold.electChainAt[run + 1u]; ++k)
    if (A.cold.electChainPos[k] == side) each(A.cold.electChainWord[k]);
}

// `words` with the decisions of the runs skipped under children standing on
// `syms` around them, laid in `out` and `at`; false where none was skipped.
template <const auto& A>
bool lr_elect_restore(std::uint16_t prod, std::span<const std::uint32_t> words,
                      std::span<const std::uint16_t> pos,
                      std::span<const std::uint32_t> syms,
                      std::vector<std::uint32_t>& out,
                      std::vector<std::uint16_t>& at) {
  const std::uint32_t first = A.cold.electWatchAt[prod];
  const std::uint32_t end = A.cold.electWatchAt[prod + 1u];
  const auto skipped = [&](std::uint32_t w) {
    const std::uint32_t rule = w & 0xFFFFu;
    const std::uint32_t sym = syms[w >> 16];
    return sym == A.cfg.nterms + rule ? kLrNoRun : lr_elect_run<A>(rule, sym);
  };
  std::uint32_t w = first;
  while (w != end && skipped(A.cold.electWatch[w]) == kLrNoRun) ++w;
  if (w == end) return false;
  out.clear();
  at.clear();
  std::size_t k = 0;
  std::uint32_t open = kLrNoRun;
  w = first;
  const std::uint16_t len = A.cfg.prods[prod].len;
  // At one position: the run under the child before it closes, the
  // production's own words, then the run under the child after it opens.
  // The last position may be 65 535: the walk stops there, not past it.
  for (std::uint16_t q = 0;; ++q) {
    const auto put = [&](std::uint32_t word) {
      out.push_back(word);
      at.push_back(q);
    };
    if (open != kLrNoRun) lr_elect_chain<A>(open, 1, put);
    open = kLrNoRun;
    for (; k != words.size() && pos[k] == q; ++k) put(words[k]);
    if (w != end && (A.cold.electWatch[w] >> 16) == q) {
      open = skipped(A.cold.electWatch[w++]);
      if (open != kLrNoRun) lr_elect_chain<A>(open, 0, put);
    }
    if (q == len) break;
  }
  return true;
}

struct LrElectWords {
  std::span<const std::uint32_t> words;
  std::span<const std::uint16_t> pos;
};

// The words `prod` lays over children standing on `syms`, the twin's and the
// skipped runs' decisions in; `room` and `at` hold them where a run was
// restored.
template <const auto& A>
LrElectWords lr_elect_words(std::uint16_t prod,
                            std::span<const std::uint32_t> syms,
                            std::vector<std::uint32_t>& room,
                            std::vector<std::uint16_t>& at) {
  if constexpr (A.cold.electTwin.size() != 0)
    prod = lr_elect_twin<A>(prod, syms);
  const std::uint32_t first = A.cold.electAt[prod];
  const std::uint32_t n = A.cold.electAt[prod + 1u] - first;
  LrElectWords w{std::span(A.cold.electWord).subspan(first, n),
                 std::span(A.cold.electPos).subspan(first, n)};
  if constexpr (A.cold.electWatch.size() != 0)
    if (lr_elect_restore<A>(prod, w.words, w.pos, syms, room, at))
      w = {room, at};
  return w;
}

}  // namespace cursiv
