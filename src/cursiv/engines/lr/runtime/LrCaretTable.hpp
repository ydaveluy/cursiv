// What a completion caret reads of one table, borrowed: the transition
// relation it replays the tape on, the shifts each state offers, the lex
// modes and the lexer. The answer is one function over this, compiled once.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/lr/runtime/LrGapMemo.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrExpectTable.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"
#include "cursiv/engines/shared/table/CondReach.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Per mode and terminal, the sample words the lexer reads whole as the
// terminal there, set when first asked and held with the top bit up. The cells
// come with the first completion that asks, so a parser that never completes
// holds none; of two completions racing for them, the second takes the
// first's.
class LrSampleReads {
 public:
  ~LrSampleReads() { delete[] _cells.load(std::memory_order_relaxed); }

  // The cells, `count` of them, made zero by the first call.
  std::atomic<std::uint32_t>* cells(std::size_t count) const;

 private:
  mutable std::atomic<std::atomic<std::uint32_t>*> _cells{nullptr};
};

struct LrCaretTable {
  // The lexer walks the frozen DFA, so it is bound rather than described.
  Scan (*lex)(const char* src, TextOffset len, std::uint16_t mode,
              TextOffset from, TextOffset& halt) = nullptr;
  // The candidates the walk outranked, where its own acceptance refused its
  // boundary; null where no position reads several terminals.
  std::uint32_t (*scan)(const char* src, TextOffset len, std::uint16_t mode,
                        TextOffset from, Scan* out,
                        std::uint32_t cap) = nullptr;
  // The terminals a mode takes off a position no stack takes them at, and
  // the walk that reads the position without the ones taken off; null where
  // no mode flags any.
  bool (*flagged)(std::uint16_t mode, SymbolId sym) = nullptr;
  Scan (*lexWithout)(const char* src, TextOffset len, std::uint16_t mode,
                     TextOffset from, std::span<const SymbolId> skip,
                     TextOffset& halt) = nullptr;
  bool (*cond)(std::string_view, unsigned, TextOffset, const Token&, bool,
               LrGapMemo*) = nullptr;
  std::span<const CondReach> condReach{};
  TextOffset (*condSeen)(std::string_view, unsigned, TextOffset,
                         const Token&) = nullptr;
  std::uint32_t maxCands = 0;
  // Whether the parse chooses among readings by reading ahead: a cell the
  // table leaves open, a peek, or a position several terminals match.
  bool chooses = false;
  // How a record of the tape names its row and the state that took it.
  StampLayout stamp{};

  // One run per state, closed by a sentinel; the items name the pool.
  std::span<const std::uint32_t> expectBase;
  std::span<const std::uint16_t> expectItems;
  std::span<const LrExpect> expectPool;
  std::span<const std::uint16_t> shiftTerm;
  std::span<const std::uint16_t> mode;
  std::span<const std::uint8_t> modeMulti;
  std::span<const std::uint8_t> modeTight;
  std::span<const std::uint64_t> skipSet;
  std::uint16_t errMode = 0;
  // The full transition relation: the running parser keeps no stack journal.
  struct Run {
    std::uint32_t first = 0;
    unsigned count = 0;
  };
  Run (*run)(std::uint16_t, SymbolId) = nullptr;
  // The action behind op `at`: a table keeps its ops as wide as it needs.
  LrAction (*action)(std::uint32_t) = nullptr;
  std::span<const std::uint64_t> (*follow)(std::uint16_t) = nullptr;
  std::span<const std::uint16_t> soleReduce;
  // The nonterminals a complete record may reduce to, by its row: one run
  // per row, closed by a sentinel.
  std::span<const std::uint32_t> rowLhsBase;
  std::span<const std::uint16_t> rowLhs;
  // Where a reduce lands, asked of the state under the caret's own.
  std::uint32_t (*go)(std::uint32_t, SymbolId) = nullptr;
  // What the gap decides, laid as `lr_guard.hpp` reads it: a goto cell above
  // the states names its guard, and a state's tag what held where its span
  // opened.
  std::span<const std::uint32_t> guardAt;
  std::span<const std::uint16_t> guardOps;
  std::span<const std::uint64_t> held;

  std::span<const TermInfo> terms;
  // The terminals' names and spellings, as `TermInfo` places them.
  const char* names = nullptr;
  std::span<const LrProduction> prods;
  // The sample words each terminal is read as, per mode; null where the table
  // asks no condition.
  const LrSampleReads* sampleReads = nullptr;

  // A token the indentation or the parse lays: no lexer reads it, and no
  // author types it.
  [[nodiscard]] bool laid(SymbolId sym) const {
    return sym < terms.size() && terms[sym].laid != 0;
  }

  [[nodiscard]] std::span<const std::uint16_t> row_lhs(ElementId row) const {
    if (row + 1u >= rowLhsBase.size()) return {};
    return rowLhs.subspan(rowLhsBase[row], rowLhsBase[row + 1u] - rowLhsBase[row]);
  }

  // The conditions an encoded guard asks; none for a direct state.
  [[nodiscard]] std::span<const std::uint16_t> conditions_of(
      std::uint32_t target) const {
    if (target == kInvalidSymbol || target < mode.size()) return {};
    const std::uint32_t at = guardAt[target - mode.size()];
    return guardOps.subspan(at + 1u, guardOps[at]);
  }

  // A direct state or every landing of an encoded guard. `may(c)` answers
  // condition `c` 0 false, 1 unknown, 2 true: a gap is not always known here.
  template <class May, class F>
  void land_each(std::uint32_t target, May&& may, F&& f) const {
    land_vectors(target, may,
                 [&](std::uint32_t to, auto, unsigned, unsigned) { f(to); });
  }

  // The same walk retaining the conditions and their chosen truth vector:
  // a deferred landing records the unknown bits as issues of its reading.
  template <class May, class F>
  void land_vectors(std::uint32_t target, May&& may, F&& f) const {
    if (target == kInvalidSymbol) return;
    if (target < mode.size()) {
      f(target, std::span<const std::uint16_t>{}, 0u, 0u);
      return;
    }
    const std::uint32_t at = guardAt[target - mode.size()];
    const auto conditions = conditions_of(target);
    const auto n = static_cast<unsigned>(conditions.size());
    unsigned known = 0, truth = 0;
    for (unsigned b = 0; b < n; ++b) {
      const unsigned m = may(guardOps[at + 1u + b]);
      if (m != 1u) known |= 1u << b;
      if (m == 2u) truth |= 1u << b;
    }
    const std::uint16_t* const to = &guardOps[at + 1u + n];
    if (known == (1u << n) - 1u) {
      if (to[truth] != 0xffffu) f(to[truth], conditions, known, truth);
      return;
    }
    for (unsigned v = 0; v < (1u << n); ++v)
      if ((v & known) == truth && to[v] != 0xffffu)
        f(to[v], conditions, known, v);
  }
  // Whether condition `k` held where the span of state `s` opened, as
  // `lr_held` reads it.
  [[nodiscard]] bool holds(std::uint32_t s, unsigned k) const {
    const std::size_t words = (mode.size() + 63u) / 64u;
    return ((held[k * words + (s >> 6)] >> (s & 63u)) & 1u) != 0;
  }
};

// The prefix's stacks are reconstructed from the tape and the final
// action/goto tables, a complete record as one goto; a continuation must
// reach a shift on one of them. A tape no engine stamped -- `CstBuilder` is
// public -- gets no answer rather than a wrong one.
void lr_next_features_at(const LrCaretTable& table,
                         const StampedGrammar& grammar, const Cst& cst,
                         TextOffset at, TextOffset resume,
                         std::vector<NextFeature>& out);

}  // namespace cursiv
