// A state of the lexer may accept several terminals, each with a frontier of
// its own. A misspelling read again as one of them is judged on the frontier of
// THAT terminal, as the LR engine judges it: the frontier of the one the state
// elects is no answer for the others.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_rescan.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/RescanBoundGrammar.hpp"
#include "rescannarrowll/generated/NarrowLl/Module.hpp"
#include "rescannarrowll/generated/NarrowLl/Table.hpp"
#include "rescannarrowlr/generated/NarrowLr/Module.hpp"
#include "rescanshadowll/generated/ShadowLl/Module.hpp"
#include "rescanshadowll/generated/ShadowLl/Table.hpp"
#include "rescanshadowlr/generated/ShadowLr/Module.hpp"

namespace {

namespace gen = tllrescan::generated;

template <const auto& T>
constexpr std::uint16_t term_of(std::string_view name) {
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_name(t) == name) return t;
  return cursiv::kInvalidSymbol;
}

// The first of the terminals a point owes, a set of them, that are read again
// at the end of `word`, with `text` behind the re-reading.
template <const auto& T>
cursiv::SymbolId owed_after(std::string_view word, std::string_view text,
                            std::initializer_list<std::string_view> owes) {
  std::array<std::uint64_t, T.termWords> want{};
  for (const std::string_view name : owes) {
    const std::uint16_t t = term_of<T>(name);
    want[t >> 6] |= std::uint64_t{1} << (t & 63u);
  }
  std::uint16_t state = T.modeStart[T.errMode];
  for (const char c : word)
    state = T.next_byte(state, static_cast<unsigned char>(c));
  std::uint64_t read = 0;
  const cursiv::LlTypoTape<T> tape{text.data(),
                                   static_cast<cursiv::TextOffset>(text.size()),
                                   static_cast<cursiv::TextOffset>(text.size()),
                                   want.data(),
                                   &read,
                                   0};
  cursiv::SymbolId first = cursiv::kInvalidSymbol;
  tape.owed(state, static_cast<cursiv::TextOffset>(word.size()),
            [&](cursiv::SymbolId t) {
              if (first == cursiv::kInvalidSymbol) first = t;
            });
  return first;
}

// A narrower scan keeps each terminal's own bound; a hint omitting one of
// the requested readings must fall back to the error alphabet.
template <const auto& T>
void hinted_readings_agree(std::string_view first, std::string_view second) {
  const auto a = term_of<T>(first), b = term_of<T>(second);
  for (const std::string_view text : {"abc;", "abc.", "abcd;", "abcd.", "abc"})
    for (unsigned mask = 1; mask < 4; ++mask) {
      cursiv::LlMask<T.termWords> wanted;
      if ((mask & 1u) != 0) wanted.set(a);
      if ((mask & 2u) != 0) wanted.set(b);
      std::vector<std::uint16_t> expected;
      cursiv::TextOffset inspected = 0;
      const auto end = cursiv::ll_match_in<T>(
          text.data(), static_cast<cursiv::TextOffset>(text.size()), 0, wanted,
          expected, &inspected);
      for (std::uint16_t mode = 0; mode < T.nmodes; ++mode) {
        std::vector<std::uint16_t> read;
        cursiv::TextOffset seen = 0;
        const auto matched = cursiv::ll_match_in<T>(
            text.data(), static_cast<cursiv::TextOffset>(text.size()), 0,
            wanted, read, &seen, nullptr, nullptr, mode);
        CHECK(matched == end && read == expected);
        CHECK(seen <= inspected);
      }
    }
}

using Marks = std::vector<std::array<unsigned, 3>>;

Marks marks(const cursiv::ParseResult& r) {
  Marks out;
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    out.push_back({static_cast<unsigned>(d.kind), d.begin, d.end});
  return out;
}

}  // namespace

int main() {
  // The frontier of the terminal ranked first refuses `;`; the other ends at a
  // word, and is the one that stands there.
  constexpr const auto& narrow = gen::NarrowLl::kTable;
  CHECK(owed_after<narrow>("abc", "abc;", {"BOUNDED", "LONGER"}) ==
        term_of<narrow>("LONGER"));
  CHECK(owed_after<narrow>("abc", "abc.", {"BOUNDED", "LONGER"}) ==
        term_of<narrow>("BOUNDED"));
  CHECK(owed_after<narrow>("abc", "abcd", {"BOUNDED", "LONGER"}) ==
        term_of<narrow>("BOUNDED"));

  // The one declared first has the frontier that refuses; the one elected
  // where both accept has none, and ends at a word.
  constexpr const auto& shadow = gen::ShadowLl::kTable;
  hinted_readings_agree<narrow>("BOUNDED", "LONGER");
  hinted_readings_agree<shadow>("BOUNDED", "SPELLED");
  CHECK(owed_after<shadow>("abc", "abc;", {"BOUNDED", "SPELLED"}) ==
        term_of<shadow>("SPELLED"));
  CHECK(owed_after<shadow>("abc", "abc.", {"BOUNDED", "SPELLED"}) ==
        term_of<shadow>("BOUNDED"));

  const auto nll = gen::NarrowLl::create_parser();
  const auto nlr = gen::NarrowLr::create_parser();
  const auto sll = gen::ShadowLl::create_parser();
  const auto slr = gen::ShadowLr::create_parser();
  for (const std::string_view text :
       {".a bd ac;", "bb.a.c;", ";aa;bc;", "c;a;bc;da", ". a;bc;."}) {
    const auto a = nll->parse(text), b = nlr->parse(text);
    CHECK(a.accepted && b.accepted && !a.diagnostics.empty());
    CHECK(marks(a) == marks(b));
  }
  for (const std::string_view text :
       {" .ac;c b;", "c;axc;c z", " .a;c;a", "b.ab;z zb", ";ba;a.c;"}) {
    const auto a = sll->parse(text), b = slr->parse(text);
    CHECK(a.accepted && b.accepted && !a.diagnostics.empty());
    CHECK(marks(a) == marks(b));
  }
  return cursiv::test::report("LlRescanBound_test");
}
