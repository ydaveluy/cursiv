// The bytes a lexer crosses blind are an optimization and nothing else: over any
// automaton, every text reads the same tokens with them as without them.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/shared/gen/LexFirstByte.hpp"
#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace {

using cursiv::kDeadState;

// Terminal 0 is the blank trivia, 1 a token, 2 a trivia the tape records.
struct Automaton {
  unsigned nclasses = 0;
  std::vector<std::uint16_t> rows{};
  std::vector<std::uint16_t> accepts{};
  std::vector<cursiv::TermInfo> terms = [] {
    std::vector<cursiv::TermInfo> out(3);
    out[0].trivia = true;
    out[2].trivia = true;
    out[2].recorded = true;
    return out;
  }();

  [[nodiscard]] std::uint32_t modes() const { return 1; }
  [[nodiscard]] std::uint16_t start(std::uint32_t) const { return 0; }
  [[nodiscard]] unsigned classes() const { return nclasses; }
  [[nodiscard]] std::uint8_t cls(unsigned b) const {
    return static_cast<std::uint8_t>(b < nclasses ? b : nclasses - 1);
  }
  [[nodiscard]] std::uint16_t next(std::uint16_t s, unsigned c) const {
    return rows[std::size_t{s} * nclasses + c];
  }
  [[nodiscard]] std::uint16_t accept(std::uint16_t s) const { return accepts[s]; }
  [[nodiscard]] bool bounded(std::uint16_t) const { return false; }
  [[nodiscard]] std::uint16_t relay(std::uint16_t) const { return cursiv::kNoRelay; }
  [[nodiscard]] const cursiv::TermInfo& term(unsigned t) const { return terms[t]; }
};

struct Read {
  std::size_t begin, end;
  unsigned term;
  bool operator==(const Read&) const = default;
};

// What a text reads, the blank trivia left out: its extent is nobody's. The
// walk's longest match, or a byte nothing reads, one at a time. A reader crosses
// a run of `skips`, and steps back onto its last byte where `stops` says so.
template <class Skips, class Stops>
std::vector<Read> tokens(const Automaton& a, std::string_view text, Skips&& skips,
                         Stops&& stops) {
  std::vector<Read> out;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t from = at;
    while (at < text.size() && skips(static_cast<unsigned char>(text[at]))) ++at;
    if (at == text.size()) break;
    if (at != from && stops(static_cast<unsigned char>(text[at]))) --at;
    std::uint16_t state = 0;
    std::size_t end = at, longest = at;
    unsigned term = 0;
    while (end < text.size()) {
      state = a.next(state, a.cls(static_cast<unsigned char>(text[end])));
      if (state == kDeadState) break;
      ++end;
      if (a.accept(state) != 0) {
        longest = end;
        term = a.accept(state);
      }
    }
    if (longest == at) {
      out.push_back({at, at + 1, 99});
      ++at;
      continue;
    }
    if (term != 1) out.push_back({at, longest, term - 1});
    at = longest;
  }
  return out;
}

// The earlier rule: every live edge of the run's state returns to it.
bool every_edge_loops(const Automaton& a, std::uint16_t s1) {
  for (unsigned c = 0; c < a.classes(); ++c) {
    const std::uint16_t n = a.next(s1, c);
    if (n != kDeadState && (n != s1 || a.next(0, c) != s1)) return false;
  }
  return true;
}

Automaton random_automaton(std::mt19937& random) {
  Automaton a;
  a.nclasses = 2 + random() % 3;
  const unsigned states = 2 + random() % 6;
  a.rows.resize(std::size_t{states} * a.nclasses);
  a.accepts.resize(states);
  for (unsigned s = 0; s < states; ++s) {
    for (unsigned c = 0; c < a.nclasses; ++c)
      a.rows[std::size_t{s} * a.nclasses + c] =
          random() % 3 == 0 ? kDeadState
                            : static_cast<std::uint16_t>(random() % states);
    // The start accepts nothing, as a token is never empty.
    const unsigned pick = random() % 6;
    a.accepts[s] = s == 0 || pick >= 4
                       ? 0
                       : static_cast<std::uint16_t>(pick < 2 ? 1 : pick);
  }
  return a;
}

void any_text_reads_the_same_with_the_blind_bytes() {
  std::mt19937 random(20261104);
  unsigned skipping = 0, gained = 0, kept = 0, stopping = 0;
  std::string text;
  for (unsigned round = 0; round < 40000; ++round) {
    const Automaton a = random_automaton(random);
    const cursiv::LexSkipRuns set = cursiv::lex_skip_runs(a);
    const auto holds = [](const std::vector<std::uint64_t>& words,
                          unsigned char b) {
      return ((words[b >> 6] >> (b & 63u)) & 1u) != 0;
    };
    const auto blind = [&](unsigned char b) { return holds(set.skip, b); };
    const auto stop = [&](unsigned char b) { return holds(set.stop, b); };
    bool any = false, earlier = false, stepping = false;
    for (unsigned c = 0; c < a.nclasses; ++c) {
      stepping = stepping || stop(static_cast<unsigned char>(c));
      const std::uint16_t s1 = a.next(0, c);
      if (s1 == kDeadState || a.accept(s1) != 1) continue;
      any = any || blind(static_cast<unsigned char>(c));
      if (every_edge_loops(a, s1)) {
        earlier = true;
        // The rule is wider than the one before, never narrower.
        CHECK(blind(static_cast<unsigned char>(c)));
      }
    }
    skipping += any ? 1u : 0u;
    gained += any && !earlier ? 1u : 0u;
    kept += earlier ? 1u : 0u;
    stopping += stepping ? 1u : 0u;
    if (!any) continue;
    // Every text up to a length that goes round any of these automata.
    const unsigned longest = a.nclasses <= 3 ? 7 : 6;
    std::vector<unsigned> digits;
    for (unsigned length = 1; length <= longest; ++length) {
      digits.assign(length, 0);
      for (;;) {
        text.clear();
        for (const unsigned d : digits) text.push_back(static_cast<char>(d));
        const auto no = [](unsigned char) { return false; };
        const auto seen = tokens(a, text, blind, stop);
        const auto whole = tokens(a, text, no, no);
        CHECK(seen == whole);
        if (seen != whole) {
          std::printf("round %u:", round);
          for (const unsigned d : digits) std::printf(" %u", d);
          std::printf("\n");
          return;
        }
        std::size_t i = 0;
        while (i < length && ++digits[i] == a.nclasses) digits[i++] = 0;
        if (i == length) break;
      }
    }
  }
  // Not an empty proof: the automata reach every branch of the rule.
  CHECK(skipping > 1000);
  CHECK(kept > 500);
  CHECK(gained > 300);
  CHECK(stopping > 300);
}

// A hand-made automaton, a state per row: terminal plus one in `accept`.
struct Row {
  std::uint16_t cells[4];
  std::uint16_t accept;
};

Automaton from_rows(std::initializer_list<Row> rows) {
  Automaton a;
  a.nclasses = 4;
  for (const Row& row : rows) {
    a.rows.insert(a.rows.end(), std::begin(row.cells), std::end(row.cells));
    a.accepts.push_back(row.accept);
  }
  return a;
}

constexpr std::uint16_t kX = kDeadState;

// Bytes: 0 a blank, 1 and 2 a longer blank character, 3 a word byte.
void the_rule_says_what_it_should_on_each_shape() {
  const auto blind_blank = [](const Automaton& a) {
    return (cursiv::lex_skip_runs(a).skip[0] & 1u) != 0;
  };
  const auto stop_on = [](const Automaton& a, unsigned b) {
    return ((cursiv::lex_skip_runs(a).stop[0] >> b) & 1u) != 0;
  };
  // A run with a longer character: read from the start, it reaches one state.
  const Automaton longer = from_rows({{{1, 2, kX, 3}, 0},
                                      {{1, 2, kX, kX}, 1},
                                      {{kX, kX, 1, kX}, 0},
                                      {{kX, kX, kX, 3}, 2}});
  CHECK(blind_blank(longer) && !stop_on(longer, 1) && !stop_on(longer, 3));
  // A word that may hold the first byte of that character: the two walks part
  // there but read that character alike, and only one of them reads on.
  const Automaton shared = from_rows({{{1, 4, kX, 3}, 0},
                                      {{1, 2, kX, kX}, 1},
                                      {{kX, kX, 1, kX}, 0},
                                      {{kX, kX, kX, 3}, 2},
                                      {{kX, kX, 1, 3}, 0}});
  CHECK(blind_blank(shared) && !stop_on(shared, 1));
  // A character of the run that a longer token also begins: cut before it, that
  // token would win.
  const Automaton longest = from_rows({{{1, 2, kX, kX}, 0},
                                       {{1, 3, kX, kX}, 1},
                                       {{kX, kX, 4, kX}, 0},
                                       {{kX, kX, 1, kX}, 0},
                                       {{1, kX, kX, 5}, 1},
                                       {{kX, kX, kX, kX}, 2}});
  CHECK(blind_blank(longest) && stop_on(longest, 1));
  // A line splice: nothing reads its first byte from the start.
  const Automaton splice = from_rows({{{1, kX, kX, 3}, 0},
                                      {{1, 2, kX, kX}, 1},
                                      {{1, kX, kX, kX}, 0},
                                      {{kX, kX, kX, 3}, 2}});
  CHECK(blind_blank(splice) && stop_on(splice, 1));
  // A token that begins with the blank: it says where it began.
  CHECK(!blind_blank(from_rows({{{1, kX, kX, 3}, 0},
                                {{1, kX, kX, 3}, 1},
                                {{kX, kX, kX, kX}, 0},
                                {{kX, kX, kX, kX}, 2}})));
  // A recorded trivia behind the blank: the tape keeps where it began.
  CHECK(!blind_blank(from_rows({{{1, kX, kX, kX}, 0},
                                {{1, 2, kX, kX}, 1},
                                {{kX, kX, kX, kX}, 3}})));
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  any_text_reads_the_same_with_the_blind_bytes();
  the_rule_says_what_it_should_on_each_shape();
  return cursiv::test::report("LexSkipSet_test");
}
