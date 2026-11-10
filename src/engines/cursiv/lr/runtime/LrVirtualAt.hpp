// Where a reading stands with the gaps and implied tokens: where the last were
// inserted, once per position; where the next gap begins; and the implied token
// in hand, which a state lexing in its own mode hands back.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

struct LrVirtualAt {
  TextOffset insertedAt = 0xFFFFFFFFu;
  // A condition's gap begins here, not at the cursor, which a reduce's flush
  // moves onto the token.
  TextOffset from = 0;
  // The implied tokens inserted at `insertedAt`, one bit each by rank.
  std::uint32_t inserted = 0;
  SymbolId pending = 0;

  // The ranks already inserted where `la` stands.
  [[nodiscard]] std::uint32_t inserted_ranks(const Token& la) const {
    return insertedAt == la.begin ? inserted : 0;
  }
  // What decides a reading's future from `at`: past where the last ones were
  // inserted, with none in hand, the ranks decide nothing, so two readings that
  // differ only there are one.
  [[nodiscard]] LrVirtualAt seen_from(TextOffset at) const {
    LrVirtualAt v = *this;
    if (v.pending == 0 && v.insertedAt < at) {
      v.insertedAt = 0xFFFFFFFFu;
      v.inserted = 0;
    }
    return v;
  }
  // The implied token in hand, zero wide where it was inserted.
  [[nodiscard]] Token in_hand() const {
    return Token{insertedAt, insertedAt, pending};
  }
  [[nodiscard]] bool implies(const Token& t) const {
    return pending != 0 && t.sym == pending && t.begin == t.end;
  }
  // `t` shifted: an implied one leaves the hand, a token of the text opens the
  // next gap.
  void shift(const Token& t) {
    if (t.sym == pending) pending = 0;
    if (t.end != t.begin) from = t.end;
  }
  // Rank `q`, terminal `t`, inserted at `la` and in hand until shifted.
  void insert(Token& la, unsigned q, SymbolId t) {
    inserted = inserted_ranks(la) | std::uint32_t{1} << q;
    insertedAt = la.begin;
    pending = t;
    la = Token{la.begin, la.begin, t};
  }

  constexpr bool operator==(const LrVirtualAt&) const = default;
};

}  // namespace cursiv::engine
