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
  std::string_view type;
  std::uint16_t typeSlot = detail::kNoIndex;
  std::uint8_t narrow = kNoNarrow;
  std::string_view rule;
};

// What a terminal answers: the text it is spelled with, or its name where it
// is not spelled.
struct RecordTerminal {
  bool spelled = false;
  std::string_view word;
  std::string_view doc;
  bool folded = false;
};

RecordGen record_gen(std::span<const AstRowGen> rows,
                     const std::vector<std::uint16_t>& held,
                     const std::vector<std::uint16_t>& over);

// Every entry's answer, `rows_of(e, held, over)` stating the runs of entry
// `e`.
template <class RowsOf>
std::vector<RecordGen> record_gens(std::span<const AstRowGen> rows,
                                   std::size_t n, RowsOf&& rows_of) {
  std::vector<RecordGen> records(n);
  std::vector<std::uint16_t> held, over;
  for (std::size_t e = 0; e < n; ++e) {
    rows_of(static_cast<std::uint16_t>(e), held, over);
    records[e] = record_gen(rows, held, over);
  }
  return records;
}

template <class Cfg>
std::vector<RecordTerminal> record_terminals(const Cfg& c) {
  std::vector<RecordTerminal> terminals(c.nterms);
  for (std::uint16_t t = 0; t < c.nterms; ++t)
    terminals[t] = {c.terms[t].spelled,
                    c.terms[t].spelled ? c.term_text(t) : c.term_name(t),
                    c.termBuild[t].doc, c.termBuild[t].folded};
  return terminals;
}

std::string emit_records(std::string_view ns, std::string_view header,
                         const std::vector<RecordTerminal>& terminals,
                         const std::vector<RecordGen>& records,
                         const AstRegistry& registry);

std::string emit_records_decl(std::string_view ns);

}  // namespace cursiv
