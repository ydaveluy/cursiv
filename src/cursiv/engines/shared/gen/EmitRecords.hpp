// The record facet of a grammar, written as C++ source: what each entry of the
// table owes the tree and what each terminal is called. The caller states the
// runs each entry answers with.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/engines/shared/gen/AstRowGen.hpp"

namespace cursiv {

struct AstRegistry;

// What one entry answers: the members its run writes on either side of the
// node it makes, the site it is named by, and the rule a conversion names.
struct RecordGen {
  std::vector<std::string_view> held;
  std::vector<std::string_view> over;
  bool made = false;
  std::string_view feature;
  std::uint16_t typeSlot = detail::kNoIndex;
  std::uint8_t narrow = kNoNarrow;
  std::string_view rule;
};

RecordGen record_gen(std::span<const AstRowGen> rows,
                     const std::vector<std::uint16_t>& held,
                     const std::vector<std::uint16_t>& over);

// What each terminal answers, written as a `GrammarTerminal` row: its name
// where it is not spelled, and the words it is spelled by -- a literal's text,
// or the words of a choice of them. The words stay `c`'s.
struct TerminalGen {
  std::string_view name;
  std::string_view doc;
  bool folded = false;
  std::vector<std::string_view> spellings;
};

template <class Cfg>
std::vector<TerminalGen> record_terminals(const Cfg& c) {
  std::vector<TerminalGen> terminals(c.nterms);
  for (std::uint16_t t = 0; t < c.nterms; ++t) {
    const bool spelled = c.terms[t].spelled;
    terminals[t] = {.name = spelled ? std::string_view{} : c.term_name(t),
                    .doc = c.termBuild[t].doc,
                    .folded = c.termBuild[t].folded,
                    .spellings = spelled ? std::vector{c.term_text(t)}
                                         : c.termBuild[t].words};
  }
  return terminals;
}

std::string emit_records(std::string_view ns,
                         const std::vector<TerminalGen>& terminals,
                         const std::vector<RecordGen>& records,
                         const AstRegistry& registry);

std::string emit_records_decl(std::string_view ns);

}  // namespace cursiv
