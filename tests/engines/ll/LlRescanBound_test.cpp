// A lexer state may accept several terminals, each with its own frontier: a
// misspelling read again as one of them is judged on that terminal's frontier.
// Fixed recovery oracles pin the cases for both backends.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/ll/runtime/ll_lookahead.hpp"
#include "cursiv/ll/runtime/ll_rescan.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "engines/ll/LlTerms.hpp"
#include "engines/ll/RescanBoundAst.hpp"
#include "rescannarrowll/generated/NarrowLl/Module.hpp"
#include "rescannarrowll/generated/NarrowLl/Table.hpp"
#include "rescanshadowll/generated/ShadowLl/Module.hpp"
#include "rescanshadowll/generated/ShadowLl/Table.hpp"

namespace {

namespace gen = tllrescan::generated;
using ll_terms::named;

// The first of the terminals `names` that is read again at the end of `word`,
// with `text` after the re-reading.
template <const auto& T>
cursiv::engine::SymbolId accepted_after(
    std::string_view word, std::string_view text,
    std::initializer_list<std::string_view> names) {
  std::array<std::uint64_t, T.termWords> want{};
  for (const std::string_view name : names) {
    const std::uint16_t t = named<T>(name);
    want[t >> 6] |= std::uint64_t{1} << (t & 63u);
  }
  std::uint16_t state = T.modeStart[T.errMode];
  for (const char c : word)
    state = T.next_byte(state, static_cast<unsigned char>(c));
  std::uint64_t read = 0;
  const cursiv::engine::LlTypoTape<T> tape{
      {{text.data(), static_cast<cursiv::TextOffset>(text.size()),
        static_cast<cursiv::TextOffset>(text.size()), &read},
       0},
      want.data()};
  cursiv::engine::SymbolId first = cursiv::engine::kInvalidSymbol;
  tape.accepted(state, static_cast<cursiv::TextOffset>(word.size()),
                [&](cursiv::engine::SymbolId t) {
                  if (first == cursiv::engine::kInvalidSymbol) first = t;
                });
  return first;
}

// The ends of the re-readings of `text` from its start, as any terminal.
template <const auto& T>
std::vector<cursiv::TextOffset> typo_ends(std::string_view text) {
  std::array<std::uint64_t, T.termWords> want{};
  want.fill(~std::uint64_t{0});
  std::uint64_t read = 0;
  const auto len = static_cast<cursiv::TextOffset>(text.size());
  const cursiv::engine::LlTypoTape<T> tape{{{text.data(), len, len, &read}, 0},
                                           want.data()};
  std::vector<cursiv::TextOffset> ends;
  cursiv::engine::typo_each(
      tape, T.modeStart[T.errMode], 0, 0,
      [&](const cursiv::engine::Typo& typo) { ends.push_back(typo.end); });
  return ends;
}

// A skipped run of bytes ends where a letter ends: it finishes a well-formed
// scalar it cut into, and a byte that continues no scalar is one more fault.
template <const auto& T>
void a_skipped_run_ends_with_its_letter() {
  // "ab", two three-byte letters, "c": the run of four bytes ends inside the
  // second letter and takes the rest of it.
  const std::vector<cursiv::TextOffset> letters = typo_ends<T>(
      "ab\xE2\x82\xAC\xE2\x82\xAC"
      "c");
  CHECK(std::ranges::find(letters, 9u) != letters.end());
  // A long run of bytes that make no letter is never one fault: bytes that
  // continue none, letters cut short, bytes that start none, overlong forms.
  for (const std::string_view bad :
       {std::string_view("\x80"), std::string_view("\xE2\x82"),
        std::string_view("\xF8"), std::string_view("\xC0\x80")}) {
    std::string illFormed = "ab";
    while (illFormed.size() < 4096) illFormed += bad;
    illFormed += 'c';
    for (const cursiv::TextOffset end : typo_ends<T>(illFormed))
      CHECK(end < 16u);
  }
}

// A walk reads the parse's stop as its paths step over bytes: once a stop is
// asked for, a long token is not read to its end.
template <const auto& T>
void a_walk_reads_the_stop_as_it_steps() {
  // Blanks: a token the walk still wants at its end.
  const std::string word(100000, ' ');
  const auto len = static_cast<cursiv::TextOffset>(word.size());
  std::array<std::uint64_t, T.termWords> want{};
  want.fill(~std::uint64_t{0});
  for (const bool stopped : {false, true}) {
    cursiv::CancellationTokenSource source;
    if (stopped) source.request_stop();
    const cursiv::CancellationToken token = source.get_token();
    const cursiv::engine::CancellationPoll stop(token);
    std::uint64_t read = 0;
    const cursiv::engine::TypoBounds bounds{.src = word.data(),
                                            .extent = len,
                                            .docLen = len,
                                            .read = &read,
                                            .stop = &stop};
    const cursiv::engine::LlTypoTape<T> tape{{bounds, 0}, want.data()};
    bool threw = false;
    try {
      cursiv::engine::typo_each(tape, T.modeStart[T.errMode], 0, 0,
                                [](const cursiv::engine::Typo&) {});
    } catch (const cursiv::OperationCancelled&) {
      threw = true;
    }
    CHECK(threw == stopped);
    CHECK(stopped ? read <= cursiv::engine::CancellationPoll::kTurns
                  : read >= len);
  }
}

// A narrower scan keeps each terminal's own bound; a hint omitting one of
// the requested readings must fall back to the error alphabet.
template <const auto& T>
void hinted_readings_agree(std::string_view first, std::string_view second) {
  const auto a = named<T>(first), b = named<T>(second);
  for (const std::string_view text : {"abc;", "abc.", "abcd;", "abcd.", "abc"})
    for (unsigned mask = 1; mask < 4; ++mask) {
      cursiv::engine::LlMask<T.termWords> wanted;
      if ((mask & 1u) != 0) wanted.set(a);
      if ((mask & 2u) != 0) wanted.set(b);
      std::vector<std::uint16_t> expected;
      cursiv::TextOffset inspected = 0;
      const auto end = cursiv::engine::ll_match_in<T>(
          text.data(), static_cast<cursiv::TextOffset>(text.size()), 0, wanted,
          expected, &inspected);
      for (std::uint16_t mode = 0; mode < T.nmodes; ++mode) {
        std::vector<std::uint16_t> read;
        cursiv::TextOffset seen = 0;
        const auto matched = cursiv::engine::ll_match_in<T>(
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
  CHECK(accepted_after<narrow>("abc", "abc;", {"BOUNDED", "LONGER"}) ==
        named<narrow>("LONGER"));
  CHECK(accepted_after<narrow>("abc", "abc.", {"BOUNDED", "LONGER"}) ==
        named<narrow>("BOUNDED"));
  CHECK(accepted_after<narrow>("abc", "abcd", {"BOUNDED", "LONGER"}) ==
        named<narrow>("BOUNDED"));

  // The one declared first has the frontier that refuses; the one chosen
  // where both accept has none, and ends at a word.
  constexpr const auto& shadow = gen::ShadowLl::kTable;
  a_skipped_run_ends_with_its_letter<narrow>();
  a_walk_reads_the_stop_as_it_steps<narrow>();
  hinted_readings_agree<narrow>("BOUNDED", "LONGER");
  hinted_readings_agree<shadow>("BOUNDED", "SPELLED");
  CHECK(accepted_after<shadow>("abc", "abc;", {"BOUNDED", "SPELLED"}) ==
        named<shadow>("SPELLED"));
  CHECK(accepted_after<shadow>("abc", "abc.", {"BOUNDED", "SPELLED"}) ==
        named<shadow>("BOUNDED"));

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
