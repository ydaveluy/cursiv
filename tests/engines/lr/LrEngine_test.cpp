#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "cells/generated/G/Module.hpp"
#include "cells/generated/G/Parser.hpp"
#include "cells/generated/G/Table.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/lr_lex_reading.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "cycle/generated/G/Module.hpp"
#include "cycle/generated/G/Parser.hpp"
#include "cycle/generated/G/Table.hpp"
#include "edges/generated/G/Module.hpp"
#include "edges/generated/G/Parser.hpp"
#include "edges/generated/G/Table.hpp"
#include "engines/lr/ParseChecks.hpp"
#include "guard/generated/G/Table.hpp"
#include "lexicalpredicateslr/generated/Lr/Table.hpp"
#include "nested/generated/G/Table.hpp"
#include "shadowll/generated/Ll/Module.hpp"
#include "shadowlr/generated/Lr/Module.hpp"

// Cells and positions no look-ahead settles, split rather than guessed.

namespace {

// An item of the cells grammar: `x`, `run` repetitions, then arm `arm`'s end.
std::string item(unsigned run, unsigned arm) {
  std::string s = "x";
  for (unsigned i = 0; i < run; ++i) s += " a";
  return s + " end" + std::to_string(arm);
}

}  // namespace

// A precedence table in a region, whose two ASTs, the descent's and this
// engine's, must be the one AST. Then the cells and positions no look-ahead
// settles, which only this engine has to split.
namespace engine {

namespace {
struct Twin {
  std::unique_ptr<cursiv::Parser> ll, lr;
  std::unique_ptr<cursiv::AstReflection> llR, lrR;
};

void one_tree(const char* what, const Twin& t, std::string_view text) {
  const cursiv::ParseResult a = t.ll->parse(text);
  const cursiv::ParseResult b = t.lr->parse(text);
  CHECK(a.accepted && a.diagnostics.empty());
  CHECK(b.accepted && b.diagnostics.empty());
  CHECK(a.value != nullptr && b.value != nullptr);
  if (a.value == nullptr || b.value == nullptr) return;
  std::string x, y;
  cursiv::test::ast_of(*t.llR, *a.value, x);
  cursiv::test::ast_of(*t.lrR, *b.value, y);
  if (x != y)
    std::printf("  %s %.*s\n    ll %s\n    lr %s\n", what,
                static_cast<int>(text.size()), text.data(), x.c_str(),
                y.c_str());
  CHECK(x == y);
}

using checks::clean, checks::repaired;

// A precedence table a region reaches.
void a_table_may_stand_inside_a_region() {
  namespace ll = tshadow::cursiv::std::generated::Ll;
  namespace lr = tshadow::cursiv::std::generated::Lr;
  const Twin t{ll::create_parser(), lr::create_parser(),
               ll::create_ast_reflection(), lr::create_ast_reflection()};
  for (const std::string_view text : {"[1]", "[1+2]", "[1+2+3]"})
    one_tree("region", t, text);
  // The boundary before the region is the caller's, and the one a call opens
  // is the region's: the leading blank is skipped, and every one inside is
  // refused -- the climb's own boundaries included, which it mints rather
  // than reads off a body.
  CHECK(clean(*t.lr, " [1+2] "));
  CHECK(repaired(*t.lr, "[ 1+2]"));
  CHECK(repaired(*t.lr, "[1 + 2]"));
  CHECK(repaired(*t.lr, "[1+ 2]"));
  CHECK(repaired(*t.lr, "[1+2 ]"));
}

// The walk keeps one acceptance per state. Where that one's guard refuses at
// the cursor, the terminals it outranked still match, and only the sweep holds
// them: asking it is what tells a refusal from a lexical failure. The reader a
// look-ahead uses owes the same answer, so the `x q` entry asks it there too.
void a_refused_guard_leaves_the_reading_it_outranked() {
  const auto p = tedges::generated::G::create_parser();
  CHECK(clean(*p, "fallback:a_"));
  CHECK(clean(*p, "fallback:b_"));
  CHECK(clean(*p, "fallback:aa_"));
  CHECK(clean(*p, "fallback:a;"));
  CHECK(clean(*p, "fallback:x z"));
  CHECK(clean(*p, "fallback:x q b_"));
  CHECK(clean(*p, "fallback:x q aa_"));
  CHECK(clean(*p, "fallback:x q a_"));
  CHECK(clean(*p, "fallback:x q a;"));
  // A keyword choice hides a same-width literal choice while its word guard
  // holds. Named-terminal probes must also account for the fallback.
  CHECK(clean(*p, "fallback:[ax]"));
  CHECK(clean(*p, "fallback:[bx]"));
  CHECK(clean(*p, "fallback:[a x]"));
  CHECK(repaired(*p, "fallback:[ay]"));
}

// What the drive read past a refused guard, the caret reads too: the leaf
// `a` of `a_` is the identifier the sweep kept, and the `_` behind it is
// owed. After `a;` the leaf is the guarded terminal, which outranks the
// identifier where it stands: `;` is offered and `_` is not.
void a_refused_guard_leaves_the_caret_its_reading() {
  const auto p = tedges::generated::G::create_parser();
  const auto offers = [&](std::string_view text, cursiv::TextOffset from,
                          cursiv::TextOffset to, std::string_view literal) {
    return checks::offers(*p, "fallback:" + std::string(text), 9 + from,
                          9 + to, literal);
  };
  CHECK(offers("a_", 1, 2, "_"));
  CHECK(offers("b_", 1, 2, "_"));
  CHECK(offers("aa_", 2, 3, "_"));
  CHECK(offers("a;", 1, 2, ";"));
  CHECK(!offers("a;", 1, 2, "_"));
}

// The two readings run side by side through a repetition nothing bounds, so
// no budget separates them before the last token. Out of look-ahead, the cell
// is split rather than guessed: the text refutes the loser.
void a_cell_no_look_ahead_settles_is_split() {
  const auto p = tedges::generated::G::create_parser();
  std::string b = "cell:x", c = "cell:x";
  for (unsigned i = 0; i < 40; ++i) {
    b += " a";
    c += " a";
    CHECK(clean(*p, b + " b"));
    CHECK(clean(*p, c + " c"));
  }
}

// What the arms a split left standing owe a caret. The leaf names one of
// them; the others are as much the parse as it is, and the table carries the
// states they stand in.
void a_caret_over_a_split_cell_owes_every_arm() {
  const auto p = tedges::generated::G::create_parser();
  for (const std::string_view text : {"x", "x a", "x a a", "x a b", "x a c"}) {
    const std::string doc = "cell:" + std::string(text);
    const cursiv::ParseResult r = p->parse(doc);
    // Also inspect the same split inside each clean, complete reading:
    // EOF recovery must not hide a missing sibling.
    const bool complete = doc.ends_with(" b") || doc.ends_with(" c");
    if (complete) CHECK(r.accepted && r.diagnostics.empty());
    const auto at = static_cast<cursiv::TextOffset>(doc.size() - (complete ? 2 : 0));
    const auto out = checks::features(*p, r, at, at);
    // `b` closes one arm and `c` the other: an answer holding only one of
    // them is the cell decided by the caret instead of by the text.
    for (const std::string_view arm : {"a", "b", "c"})
      CHECK(checks::spelled(*p, out, arm));
  }
}

// Two terminals match the first byte and what tells them apart lies past a
// repetition nothing bounds. Out of look-ahead, the position is split like a
// cell: one branch per candidate, on the token it stands for.
void a_position_no_look_ahead_settles_is_split() {
  const auto p = tedges::generated::G::create_parser();
  std::string t = "split x";
  for (unsigned i = 0; i < 40; ++i) {
    t += " a";
    CHECK(clean(*p, t + " b"));
    CHECK(clean(*p, t + " c"));
  }
}

// The same position item after item, each past the look-ahead: the look-ahead
// runs out once on each, and the graph each opens gives the text back after
// its item, so a graph holds as much however many items follow.
void a_position_given_up_on_is_not_read_again() {
  const auto read = [](unsigned items) {
    std::string t = "split";
    for (unsigned k = 0; k < items; ++k) {
      t += " x";
      for (unsigned i = 0; i < 40; ++i) t += " a";
      t += k % 2 == 0 ? " b" : " c";
    }
    cursiv::Cst cst;
    cursiv::LrParseStats stats;
    CHECK(cursiv::lr_parser<tedges::generated::G::kTable>.parse(
        t, cst, &stats, &tedges::generated::G::kDrive));
    CHECK(cst.diagnostics().empty());
    CHECK(stats.lookStops <= items);
    return stats;
  };
  CHECK(read(8).alternatives == read(16).alternatives);
}

// Items decided one token past their tie, after one item past the look-ahead:
// an arm dies at every item, and still the graph gives the text back after the
// long one, so it holds as much however many items follow.
void short_items_after_a_long_one_leave_the_graph() {
  const auto read = [](unsigned items) {
    std::string t = "split x";
    for (unsigned i = 0; i < 40; ++i) t += " a";
    t += " b";
    for (unsigned k = 0; k < items; ++k) t += k % 2 == 0 ? " x c" : " x b";
    cursiv::Cst cst;
    cursiv::LrParseStats stats;
    CHECK(cursiv::lr_parser<tedges::generated::G::kTable>.parse(
        t, cst, &stats, &tedges::generated::G::kDrive));
    CHECK(cst.diagnostics().empty());
    CHECK(stats.splits == 1);
    return stats.alternatives;
  };
  CHECK(read(8) == read(64));
}

// Forty alternatives, with a repetition between the arm's terminal and its
// closing literal. Within the look-ahead's depth the oracle still reaches
// the literal and every arm is settled: a cell is not decided by how many
// arms it has, so forty of them read as far as two.
std::string wide_arm(unsigned k, unsigned run) {
  return "wide " + item(run, k);
}

void a_wide_cell_reads_as_far_as_a_narrow_one() {
  const auto p = tcells::generated::G::create_parser();
  for (unsigned run = 0; run <= 10; ++run)
    for (unsigned k = 0; k < 40; ++k) CHECK(clean(*p, wide_arm(k, run)));
}

// Past that depth the text is out of reach and the cell splits. A split is
// not a guess -- the text refutes all but one arm -- so the turn must hold
// every arm the cell has, whatever the floor of sixteen says. Forty arms and
// a hundred and twenty repetitions: every one of them reads clean.
void a_cell_splits_as_wide_as_it_is() {
  const auto p = tcells::generated::G::create_parser();
  for (unsigned k = 0; k < 40; ++k) CHECK(clean(*p, wide_arm(k, 120)));
}

// More shadow readings than the oracle carries: two readings of the first
// token, each opening a rule that offers forty of the next. A candidate the
// pool had no room for is unread, not refuted, and a cell decided from what
// was read would delete text the author wrote.
void a_reading_the_pool_had_no_room_for_refutes_nothing() {
  const auto p = tcells::generated::G::create_parser();
  for (unsigned k = 0; k < 40; ++k)
    for (const char* tail : {"b", "c"})
      CHECK(clean(*p, "nested x y end" + std::to_string(k) + " " + tail));
}

// A cell the reading gave up on once is not given up on for good: the depth
// it takes is the text's, not the state's. Read twice in one document, the
// long occurrence must leave the short one alone.
void a_cell_given_up_on_once_is_read_again() {
  const auto p = tcells::generated::G::create_parser();
  CHECK(clean(*p, item(1, 16)));
  CHECK(clean(*p, item(120, 0)));
  CHECK(clean(*p, item(1, 0) + ";" + item(1, 16)));
  CHECK(clean(*p, item(120, 0) + ";" + item(1, 16)));
}

}  // namespace

void run() {
  a_table_may_stand_inside_a_region();
  a_refused_guard_leaves_the_reading_it_outranked();
  a_refused_guard_leaves_the_caret_its_reading();
  a_cell_no_look_ahead_settles_is_split();
  a_position_no_look_ahead_settles_is_split();
  a_position_given_up_on_is_not_read_again();
  short_items_after_a_long_one_leave_the_graph();
  a_caret_over_a_split_cell_owes_every_arm();
  a_wide_cell_reads_as_far_as_a_narrow_one();
  a_cell_splits_as_wide_as_it_is();
  a_reading_the_pool_had_no_room_for_refutes_nothing();
  a_cell_given_up_on_once_is_read_again();
}

}  // namespace engine

// A cell whose arms outnumber the look-ahead's budget: the reading it settles
// on may not depend on what the same parser read before. Two parsers over one
// corpus, one forward and one backward, must answer it the same.
namespace audit {

namespace {

// One verdict per document, in a form two orders can be compared on.
int verdict(const cursiv::Parser& p, const std::string& text) {
  const cursiv::ParseResult r = p.parse(text);
  return (r.accepted ? 1000 : 0) + static_cast<int>(r.diagnostics.size());
}

void both_orders_answer_the_same(const char* name,
                                 std::unique_ptr<cursiv::Parser> p,
                                 std::unique_ptr<cursiv::Parser> q,
                                 const std::vector<std::string>& corpus) {
  std::vector<int> fwd, rev(corpus.size(), -1);
  for (const std::string& t : corpus) fwd.push_back(verdict(*p, t));
  for (std::size_t i = corpus.size(); i-- > 0;) rev[i] = verdict(*q, corpus[i]);
  for (std::size_t i = 0; i < corpus.size(); ++i)
    if (fwd[i] != rev[i])
      std::printf("  ORDER-DEPENDENT %s %s: fwd=%d rev=%d\n", name,
                  corpus[i].c_str(), fwd[i], rev[i]);
  CHECK(fwd == rev);
}

}  // namespace

void run() {
  // Seventeen arms under a branch cap of seventeen, reached by runs long
  // enough to spend the budget.
  std::vector<std::string> cells;
  for (unsigned run : {1u, 5u, 120u})
    for (unsigned arm : {0u, 16u}) {
      cells.push_back(item(run, arm));
      cells.push_back(item(run, 0) + ";" + item(1, arm));
    }
  both_orders_answer_the_same("repeated cell",
                              tcells::generated::G::create_parser(),
                              tcells::generated::G::create_parser(), cells);

  // A three-arm cell standing over a forty-arm one.
  std::vector<std::string> flood;
  for (const char* tail : {"endA", "endB", "endC"})
    for (unsigned k : {0u, 7u, 39u})
      for (const char* run : {"", " a", " a a"})
        flood.push_back(std::string("flood x y") + run + " e" + std::to_string(k) +
                        " " + tail);
  both_orders_answer_the_same("flood", tcells::generated::G::create_parser(),
                              tcells::generated::G::create_parser(), flood);
}

}  // namespace audit

namespace nested_split {

namespace {

int bad = 0;

const char* kindName(cursiv::DiagnosticKind k) {
  switch (k) {
    case cursiv::DiagnosticKind::inserted:
      return "inserted";
    case cursiv::DiagnosticKind::deleted:
      return "deleted";
    case cursiv::DiagnosticKind::replaced:
      return "replaced";
    case cursiv::DiagnosticKind::incomplete:
      return "incomplete";
    default:
      return "conversion";
  }
}

std::string doc(unsigned outer, unsigned inner, unsigned run) {
  std::string s = "p q";
  for (unsigned i = 0; i < run; ++i) s += " a";
  s += " e" + std::to_string(inner);
  s += " o" + std::to_string(outer);
  return s;
}

// `step` samples the arm matrix. A wide nest costs its width in readings at
// every document, so the widest shape is read on a grid, not on every pair.
void sweep(const char* what, const cursiv::Parser& p, const std::string& head,
           unsigned n, unsigned m, unsigned run,
           const cursiv::AstReflection* refl = nullptr, unsigned step = 1) {
  for (unsigned i = 0; i < n; i += step)
    for (unsigned j = 0; j < m; j += step) {
      const std::string t = head + doc(i, j, run);
      const cursiv::ParseResult r = p.parse(t);
      if (!r.accepted || !r.diagnostics.empty()) {
        std::printf("  %s REFUSED accepted=%d diags=%zu : %s\n", what,
                    static_cast<int>(r.accepted), r.diagnostics.size(),
                    t.c_str());
        for (const auto& d : r.diagnostics)
          std::printf("      %s [%u,%u) \"%s\"\n", kindName(d.kind), d.begin,
                      d.end, t.substr(d.begin, d.end - d.begin).c_str());
        if (refl != nullptr && r.value != nullptr) {
          std::string a;
          cursiv::test::ast_of(*refl, *r.value, a);
          std::printf("      ast %s\n", a.c_str());
        }
        ++bad;
      }
    }
}

}  // namespace

void run() {
  const auto lr = tcells::generated::G::create_parser();
  const auto lrR = tcells::generated::G::create_ast_reflection();
  // The lengths that move the verdict: none, one, and long enough to put the
  // deciding token out of the oracle's reach. Between them the answer never
  // changed over the audit's wider sweep.
  for (unsigned run : {0u, 1u, 40u}) {
    std::printf("run=%u\n", run);
    sweep("nested5x5", *lr, "nests five ", 5, 5, run, lrR.get());
    sweep("nested16x10", *lr, "nests sixteen ", 16, 10, run, nullptr, 4);
  }
  // Forks and repairs in the same document: what a stale segment table or a
  // dead branch's record would show up in, under a sanitizer. One shape is
  // enough -- what this hunts is the interaction, not the width, and the
  // sweep above drives the wide one.
  {
    std::mt19937 rng(7);
    const char alphabet[] = "pqaeo0123456789 xz;";
    std::size_t records = 0;
    for (unsigned round = 0; round < 4000; ++round) {
      std::string t = doc(rng() % 5, rng() % 5, 1 + rng() % 60);
      const unsigned edits = 1 + rng() % 8;
      for (unsigned k = 0; k < edits && !t.empty(); ++k) {
        const std::size_t at = rng() % t.size();
        switch (rng() % 3) {
          case 0:
            t.erase(at, 1);
            break;
          case 1:
            t.insert(at, 1, alphabet[rng() % (sizeof(alphabet) - 1)]);
            break;
          default:
            t[at] = alphabet[rng() % (sizeof(alphabet) - 1)];
            break;
        }
      }
      const std::string text = "nests five " + t;
      const cursiv::ParseResult r = lr->parse(text);
      records += r.cst().size();
    }
    std::printf("fuzz records=%zu\n", records);
  }
  std::printf("bad=%d\n", bad);
  CHECK(bad == 0);
}

}  // namespace nested_split

namespace case_lrunmatched {
// The text no token starts in is read by a walk from each byte, and a walk
// that goes far and finds nothing ends where the walks before it ended: the
// text is read once, and the ends and the reach are those of a walk per byte.

namespace {

namespace g = tcycle::generated::G;
constexpr const auto& kTable = g::kTable;

// An independent walk per byte supplies the extent and inspection frontier.
cursiv::TextOffset unmatched_by_walks(const char* src, cursiv::TextOffset len,
                                      std::uint16_t mode, cursiv::TextOffset p,
                                      cursiv::TextOffset& reached) {
  const cursiv::ByteSet& opens = kTable.derived.lexOpeners[kTable.errMode];
  cursiv::TextOffset q = p + 1;
  while (q < len && !cursiv::lex_line_end_at(src, q) &&
         !cursiv::lr_skips<kTable>(mode, static_cast<unsigned char>(src[q]))) {
    cursiv::TextOffset halt = q;
    const cursiv::Scan s =
        cursiv::lr_match_halt<kTable>(src, len, kTable.errMode, q, halt);
    if (halt > reached) reached = halt;
    if (s.term != cursiv::kInvalidSymbol && s.end > q) break;
    if (opens.has(static_cast<unsigned char>(src[q])) &&
        cursiv::lr_lex_reading<kTable>(src, len, kTable.errMode, q, reached)
                .term != cursiv::kInvalidSymbol)
      break;
    ++q;
  }
  if (q > reached) reached = q;
  return q;
}

std::string random_text(std::mt19937& rng) {
  constexpr std::string_view alphabet = "abcdexyz  \n";
  std::string text;
  for (unsigned n = rng() % 80; n != 0; --n)
    text += alphabet[rng() % alphabet.size()];
  for (unsigned n = rng() % 4; n != 0; --n) {
    const std::string_view unit = (rng() % 2) ? "ab" : "dd";
    for (unsigned k = rng() % 40; k != 0; --k) text += unit;
  }
  return text;
}

// A walk said to fail reads no token, and halts where the real one does.
void a_failed_walk_is_the_walk() {
  std::mt19937 rng(20260929);
  unsigned long known = 0, asked = 0;
  for (unsigned trial = 0; trial < 3000; ++trial) {
    const std::string text = random_text(rng);
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    cursiv::LexFailedWalks<cursiv::LrLexTape<kTable>> failed;
    for (unsigned q = 0; q < 2 * len + 1 && len != 0; ++q, ++asked) {
      const cursiv::TextOffset from = rng() % len;
      cursiv::TextOffset halt = from, realHalt = from;
      const cursiv::Scan real = cursiv::lr_match_halt<kTable>(
          text.data(), len, kTable.errMode, from, realHalt);
      if (!failed.fails(cursiv::LrLexTape<kTable>{}, text.data(), len,
                        kTable.errMode, from, halt))
        continue;
      ++known;
      CHECK(real.term == cursiv::kInvalidSymbol || real.end <= from);
      CHECK(halt == realHalt);
    }
  }
  CHECK(known * 4 > asked);
  std::printf("  a failed walk is the walk (%lu of %lu known)\n", known, asked);
}

// The same in every mode of a table whose words refuse heads, echo, or assert
// what follows them.
template <const auto& A>
void a_failed_walk_halts_as_the_lexer(
    const char* name, std::initializer_list<std::string_view> units,
    std::uint32_t seed) {
  std::mt19937 rng(seed);
  const std::vector<std::string_view> pool(units);
  unsigned long known = 0, asked = 0;
  for (unsigned trial = 0; trial < 1000; ++trial) {
    std::string text;
    for (unsigned n = rng() % 40; n != 0; --n)
      text += pool[rng() % pool.size()];
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    if (len == 0) continue;
    for (unsigned m = 0; m < A.modeStart.size(); ++m) {
      const auto mode = static_cast<std::uint16_t>(m);
      cursiv::LexFailedWalks<cursiv::LrLexTape<A>> failed;
      for (unsigned q = 0; q < len; ++q, ++asked) {
        const cursiv::TextOffset from = rng() % len;
        cursiv::TextOffset halt = from, realHalt = from;
        const cursiv::Scan real =
            cursiv::lr_match_halt<A>(text.data(), len, mode, from, realHalt);
        if (!failed.fails(cursiv::LrLexTape<A>{}, text.data(), len, mode, from,
                          halt))
          continue;
        ++known;
        CHECK(real.term == cursiv::kInvalidSymbol || real.end <= from);
        CHECK(halt == realHalt);
      }
    }
  }
  CHECK(known != 0);
  std::printf("  %s: a failed walk halts as the lexer (%lu of %lu known)\n",
              name, known, asked);
}

// The unmatched text ends, and is read to, where a walk per byte took it.
void the_unmatched_text_ends_where_it_did() {
  std::mt19937 rng(947);
  for (unsigned trial = 0; trial < 4000; ++trial) {
    const std::string text = random_text(rng);
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    if (len == 0) continue;
    for (unsigned k = 0; k < 6; ++k) {
      const cursiv::TextOffset p = rng() % len;
      cursiv::TextOffset want = p, got = p;
      const auto wend = unmatched_by_walks(text.data(), len, 0, p, want);
      cursiv::LexReadBudget budget(len - p);
      const auto gend = cursiv::lr_unmatched_end<kTable>(text.data(), len, 0, p,
                                                         got, budget, [] {});
      CHECK(gend == wend);
      CHECK(got == want);
    }
  }
  std::printf("  the unmatched text ends as it did\n");
}

// A hundred thousand walks, each reading to the end of the text and finding
// nothing, in one alignment and in two.
void unmatched_text_is_read_once() {
  constexpr unsigned n = 200000;
  for (const std::string_view unit : {"ab", "d", "dd"}) {
    std::string text = "!";
    for (unsigned k = 0; k < n; ++k) text += unit;
    const auto len = static_cast<cursiv::TextOffset>(text.size());
    cursiv::TextOffset reached = 0;
    cursiv::LexReadBudget budget(len);
    CHECK(cursiv::lr_unmatched_end<kTable>(text.data(), len, 0, 0, reached,
                                           budget, [] {}) == len);
    CHECK(reached == len);
  }
  static const auto parser = g::create_parser();
  for (const std::string_view unit : {"ab", "d"}) {
    std::string text;
    for (unsigned k = 0; k < n; ++k) text += unit;
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(!r.diagnostics.empty());
  }
  std::printf("  the text no token starts in is read once\n");
}

}  // namespace

void run() {
  a_failed_walk_is_the_walk();
  a_failed_walk_halts_as_the_lexer<tguard::generated::G::kTable>(
      "refused heads",
      {"a", "an", "and", "if", "in", "x", "_", "9", " ", "\n", "!"}, 7);
  a_failed_walk_halts_as_the_lexer<tnestedlex::generated::G::kTable>(
      "pragmas",
      {"{-", "-}", "{-#", "#-}", "{-$", "a", "-", "{", "#", "$", " ", "\n"},
      11);
  a_failed_walk_halts_as_the_lexer<tlexicalpredicates::generated::Lr::kTable>(
      "lexical predicates",
      {"a", "b",  "c",  "d", "x", "'",  "<",     ">",        "-", "[", "]",
       "=", "/*", "*/", "|", "#", "AS", "name{", "\xce\xb1", " ", "\n"},
      13);
  the_unmatched_text_ends_where_it_did();
  unmatched_text_is_read_once();
}
}  // namespace case_lrunmatched

int main() {
  case_lrunmatched::run();
  engine::run();
  audit::run();
  nested_split::run();
  return cursiv::test::report("LrEngine_test");
}
