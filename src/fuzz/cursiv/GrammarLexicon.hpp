// What a grammar spells, asked of its declarative expressions alone: a
// terminal's bytes, the trivia between two tokens, the regions that admit
// none. No table is read here -- the tables are what this lane judges.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/DecisionSource.hpp"
#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/utils/ctstring.hpp"

namespace cursiv::fuzzer::detail {

// How a byte of a character set is chosen. `low` and `high` are the set's own
// boundaries, where a lexer's off-by-one errors are.
enum class SpellStyle : std::uint8_t { shortest, low, high, random };

inline constexpr unsigned kSpellStyles = 4;

// What an assertion forbids the next byte spelled: the refusal ends there.
struct Veto {
  cursiv::detail::CharSet bytes{};
  bool on = false;

  void forbid(const cursiv::detail::CharSet& s) {
    bytes.merge(s);
    on = true;
  }
  void clear() {
    if (!on) return;
    bytes = cursiv::detail::CharSet{};
    on = false;
  }
  [[nodiscard]] bool forbids(unsigned char b) const {
    return on && bytes.test(b);
  }
};

struct GrammarLexicon {
  virtual ~GrammarLexicon() = default;

  // The bytes a terminal rule or a literal spells, empty when its expression
  // spells none; `after` is what its last assertion forbids the byte after
  // them.
  [[nodiscard]] virtual std::string spell(ElementId, SpellStyle,
                                          DecisionSource&,
                                          Veto& after) const = 0;
  // A spelling the grammar already owns as a literal belongs to that literal,
  // never to a terminal that happens to reach it.
  [[nodiscard]] virtual bool reserved(std::string_view) const = 0;
  // The shortest ignored word, which leaves no node; empty when the grammar
  // ignores nothing.
  [[nodiscard]] virtual std::string_view separator() const = 0;
  // A region whose skipper runs nothing: two of its tokens admit no separator
  // between them.
  [[nodiscard]] virtual bool ignores_nothing(ElementId) const = 0;
  // A terminal whose own expression begins or ends on a separator byte
  // includes the trivia beside it: what would separate two tokens becomes
  // part of one.
  [[nodiscard]] virtual bool reads_separator(ElementId) const = 0;
};

}  // namespace cursiv::fuzzer::detail
