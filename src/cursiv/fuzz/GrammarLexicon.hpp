// What a grammar spells, asked of its declarative expressions alone: a
// terminal's bytes, the trivia between two tokens, the regions that admit
// none. No table is read here -- the tables are what this lane judges.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/fuzz/DecisionSource.hpp"

namespace cursiv::fuzzer {

// How a character set is spent. `low` and `high` are the set's own
// boundaries, which is where a lexer's off-by-one lives.
enum class SpellStyle : std::uint8_t { shortest, low, high, random };

inline constexpr unsigned kSpellStyles = 4;

struct GrammarLexicon {
  virtual ~GrammarLexicon() = default;

  // The bytes a terminal rule spells; empty when its expression spells none.
  [[nodiscard]] virtual std::string spell(ElementId, SpellStyle,
                                          DecisionSource&) const = 0;
  // A spelling the grammar already owns as a literal belongs to that literal,
  // never to a terminal that happens to reach it.
  [[nodiscard]] virtual bool reserved(std::string_view) const = 0;
  // The shortest ignored word, which leaves no node; empty when the grammar
  // ignores nothing.
  [[nodiscard]] virtual std::string_view separator() const = 0;
  // An adjacency island -- a region whose skipper runs nothing -- so two of
  // its tokens admit no separator between them.
  [[nodiscard]] virtual bool glues(ElementId) const = 0;
  // A terminal whose own expression begins or ends on a separator byte
  // eats the trivia beside it: what would separate two tokens becomes
  // part of one.
  [[nodiscard]] virtual bool devours(ElementId) const = 0;
};

}  // namespace cursiv::fuzzer
