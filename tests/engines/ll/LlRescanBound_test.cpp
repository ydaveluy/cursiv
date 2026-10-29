// A state of the lexer may accept several terminals, each with a frontier of
// its own. A misspelling read again as one of them is judged on the frontier of
// that terminal: the frontier of the one the state elects is no answer for
// the others. Fixed recovery oracles pin the cases independently of a backend.

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
#include "engines/ll/RescanBoundAst.hpp"
#include "rescannarrowll/generated/NarrowLl/Module.hpp"
#include "rescannarrowll/generated/NarrowLl/Table.hpp"
#include "rescanshadowll/generated/ShadowLl/Module.hpp"
#include "rescanshadowll/generated/ShadowLl/Table.hpp"

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
  const cursiv::LlTypoTape<T> tape{
      {text.data(), static_cast<cursiv::TextOffset>(text.size()),
       static_cast<cursiv::TextOffset>(text.size()), &read},
      want.data(),
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

struct Mark {
  cursiv::DiagnosticKind kind;
  cursiv::TextOffset begin, end;
  bool operator==(const Mark&) const = default;
};
using Marks = std::vector<Mark>;

Marks marks(const cursiv::ParseResult& r) {
  Marks out;
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    out.push_back({d.kind, d.begin, d.end});
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

  struct Case {
    std::string_view text;
    Marks diagnostics;
    std::vector<std::string_view> items;
  };
  const auto check = [](const cursiv::Parser& parser, const Case& test) {
    const auto parsed = parser.parse(test.text);
    CHECK(parsed.accepted);
    CHECK(marks(parsed) == test.diagnostics);
    const auto* doc = parsed.value_as<tllrescan::Doc>();
    CHECK(doc != nullptr);
    if (doc == nullptr) return;
    std::vector<std::string_view> items;
    for (const auto* item : doc->items) items.push_back(item->text);
    CHECK(items == test.items);
  };
  using enum cursiv::DiagnosticKind;
  const auto nll = gen::NarrowLl::create_parser();
  for (const Case& test : {
           Case{".a bd ac;", {{inserted, 0, 0}, {replaced, 1, 8}}, {"", ""}},
           Case{"bb.a.c;", {{replaced, 0, 2}, {replaced, 3, 6}}, {"", "a.c"}},
           Case{";aa;bc;", {{inserted, 0, 0}, {replaced, 1, 6}}, {"", "aa;bc"}},
           Case{"c;a;bc;da", {{replaced, 0, 1}, {replaced, 2, 6}, {deleted, 7, 9}},
                {"", "a;bc"}},
           Case{". a;bc;.", {{inserted, 0, 0}, {replaced, 2, 6}, {inserted, 7, 7}},
                {"", "a;bc", ""}}})
    check(*nll, test);
  const auto sll = gen::ShadowLl::create_parser();
  for (const Case& test : {
           Case{" .ac;c b;", {{inserted, 0, 0}, {replaced, 2, 4}, {replaced, 5, 8}},
                {"", "ac", ""}},
           Case{"c;axc;c z", {{replaced, 0, 1}, {replaced, 2, 5}, {deleted, 6, 9}},
                {"", "axc"}},
           Case{" .a;c;a", {{inserted, 0, 0}, {replaced, 2, 5}, {deleted, 6, 7}},
                {"", "a;c"}},
           Case{"b.ab;z zb", {{replaced, 0, 1}, {replaced, 2, 4}, {deleted, 5, 9}},
                {"", "ab"}},
           Case{";ba;a.c;", {{inserted, 0, 0}, {replaced, 1, 3}, {replaced, 4, 7}},
                {"", "", "a.c"}}})
    check(*sll, test);
  return cursiv::test::report("LlRescanBound_test");
}
