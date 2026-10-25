// A parse's repair keeps to its budget: past it the parse still ends, its
// document accepted with a tree, and a run of owed insertions grows no bound.
// A repair that never ends trips the watchdog, not the lane's timeout.

#include "cursiv/engines/shared/runtime/RepairBudget.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "arithmetics/generated/Arith/Parser.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "asi/generated/G/Parser.hpp"
#include "asi/generated/G/Table.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/test/Check.hpp"
#include "degraded/generated/G/Parser.hpp"
#include "degraded/generated/G/Table.hpp"
#include "owed/generated/G/Parser.hpp"
#include "owed/generated/G/Table.hpp"
#include "requirements/generated/Requirements/Parser.hpp"
#include "requirements/generated/Requirements/Table.hpp"

namespace ga = ::tasi::generated::G;
namespace garith = ::arith::generated::Arith;
namespace gd = ::tdegraded::generated::G;
namespace go = ::towed::generated::G;
namespace gr = ::req::generated::Requirements;

using namespace std::chrono_literals;

namespace {

struct Parsed {
  bool accepted = false;
  bool entry = false;
  std::vector<cursiv::ParseDiagnostic> notes;
  cursiv::LrParseStats stats;
};

template <const auto& A>
Parsed parse(std::string_view text, const cursiv::LrDrive& drive,
             const cursiv::RepairBudget& budget = {}) {
  cursiv::Cst cst;
  Parsed p;
  p.accepted = cursiv::lr_parser<A>.parse(text, cst, &p.stats, &drive, budget);
  p.entry = cst.has_entry();
  p.notes.assign(cst.diagnostics().begin(), cst.diagnostics().end());
  return p;
}

cursiv::RepairBudget floor_only(std::uint64_t floor) {
  return {.floor = floor,
          .perByte = 0,
          .perSearch = ~std::uint64_t{0},
          .perParse = ~std::uint64_t{0}};
}

// Past the budget at the end of the text, an owed insertion that leads back
// to a state owing it again, one frame higher, stops where the stack the run
// set out on could owe no more.
void an_owed_run_that_grows_its_stack_ends() {
  for (const cursiv::RepairBudget& b :
       {cursiv::RepairBudget{}, floor_only(1), floor_only(64)}) {
    const Parsed loop = parse<go::kTable>("loop:c a b b", go::kDrive, b);
    CHECK(loop.accepted && loop.entry && !loop.notes.empty());
    const Parsed upe = parse<go::kTable>("upe:c a a", go::kDrive, b);
    CHECK(upe.accepted && upe.entry && !upe.notes.empty());
    const Parsed s201 =
        parse<go::kTable>("201:c b\nd c , d c ,\nd c\n", go::kDrive, b);
    CHECK(s201.accepted && s201.entry && !s201.notes.empty());
    const Parsed s383 = parse<go::kTable>("383:c c\nd b d", go::kDrive, b);
    CHECK(s383.accepted && s383.entry && !s383.notes.empty());
  }
}

// Past the budget, the closers a deep nest owes at the end are inserted as
// many as the stack the run set out on owes, however deep: the fault that
// reaches the end is closed, not wrapped whole.
void a_deep_nest_past_the_budget_is_closed() {
  std::string deep = "module m " + std::string(60, '(');
  for (unsigned i = 0; i < 3000; ++i) deep += "1 : ";
  deep += "1";
  for (const auto& [text, b] :
       {std::pair{deep, cursiv::RepairBudget{}},
        std::pair{"module m " + std::string(50, '('), floor_only(3)}}) {
    const Parsed p = parse<garith::kTable>(text, garith::kDrive, b);
    CHECK(p.accepted && p.stats.degradedPauses != 0);
    bool wrapped = false;
    bool closed = false;
    for (const cursiv::ParseDiagnostic& n : p.notes) {
      wrapped = wrapped || n.kind == cursiv::DiagnosticKind::incomplete;
      closed = closed || (n.kind != cursiv::DiagnosticKind::bounded &&
                          n.end == text.size());
    }
    CHECK(!wrapped && closed);
  }
}

// The closers of a deep nest are found one reading at a time: the words a
// state offers before the owed closer -- forty here -- are not attempted, and
// the memory a level costs is its step.
void the_closers_of_a_deep_nest_hold_one_reading() {
  const Parsed p = parse<gd::kTable>("nest " + std::string(20000, '('), gd::kDrive,
                                        floor_only(3));
  CHECK(p.accepted && p.stats.degradedPauses != 0);
  CHECK(p.stats.degradedHeld < 64);
  CHECK(p.stats.work <= std::uint64_t{8} * 20000);
}

// A closer is one reading however often the table forks over it: the ladder
// forks four times, 2^4 readings if every arm were read, and what a closer
// costs does not move with the number a text owes.
void a_forked_closer_costs_one_reading() {
  std::uint64_t before = 0;
  for (const unsigned depth : {1000u, 2000u, 4000u}) {
    const Parsed p = parse<gd::kTable>("ladder " + std::string(depth, '('),
                                      gd::kDrive, floor_only(3));
    CHECK(p.accepted && p.stats.degradedPauses != 0);
    CHECK(p.stats.degradedHeld < 64);
    CHECK(p.stats.work <= std::uint64_t{32} * depth);
    if (before != 0) CHECK(p.stats.work <= 2 * before + 64);
    before = p.stats.work;
  }
}

// The closers pay on the share of one search, like any search: a nest that
// owes more than that is closed as far as the share reaches, and the parse
// says it stopped.
void a_run_of_closers_stops_at_a_searchs_share() {
  constexpr std::uint64_t kShare = 1000;
  const cursiv::RepairBudget budget{.floor = 3,
                                    .perByte = 0,
                                    .perSearch = kShare,
                                    .perParse = ~std::uint64_t{0}};
  const Parsed p =
      parse<gd::kTable>("nest " + std::string(4000, '('), gd::kDrive, budget);
  CHECK(p.accepted && p.entry && p.stats.degradedStops != 0);
  CHECK(p.stats.work <= 2 * kShare);
}

// A completion the grammar makes as long as 2^63 tokens is cut at the share
// one search has, and the parse says so.
void a_completion_the_grammar_makes_long_is_cut() {
  const Parsed p = parse<gd::kTable>("deep x", gd::kDrive);
  CHECK(p.accepted && p.stats.degradedStops != 0);
  bool bounded = false;
  for (const cursiv::ParseDiagnostic& n : p.notes)
    bounded = bounded || n.kind == cursiv::DiagnosticKind::bounded;
  CHECK(bounded);
}

// The parse's ceiling on repair work holds whatever its document: the ends a
// string left open on a line of two megabytes may stop at are each a
// re-reading, and the search stops offering them at its share.
void every_loop_stops_at_its_share() {
  std::string line = "req R1 \"";
  for (unsigned i = 0; i < 1000000; ++i) line += "a ";
  line += "\nreq R2 \"t\"\n";
  const Parsed p = parse<gr::kTable>(line, gr::kDrive);
  CHECK(p.accepted && p.stats.work <= cursiv::kRepairPerParse + 64);
}

// A megabyte no token reads is one fault: where it ends is walked once per
// search, and paid for.
void unmatched_text_is_one_fault() {
  const std::string text = "module m f(1, " + std::string(1000000, '@') + " );";
  const Parsed p = parse<garith::kTable>(text, garith::kDrive);
  CHECK(p.accepted && p.notes.size() == 1 &&
        p.notes[0].kind == cursiv::DiagnosticKind::replaced);
}

// A condition reads a long gap once, whoever asks, and the search pays for
// it: twenty faults, each behind two hundred thousand blanks, cost the gaps
// once. Asked again by every reading, they took seconds.
void a_long_gap_is_read_once() {
  const std::string gap(200000, ' ');
  std::string text;
  for (unsigned i = 0; i < 20; ++i) text += "a " + gap + " ) ";
  const auto start = std::chrono::steady_clock::now();
  const Parsed p = parse<ga::kTable>(text, ga::kDrive);
  CHECK(std::chrono::steady_clock::now() - start < 3s);
  CHECK(p.accepted && p.stats.work <= cursiv::kRepairPerParse + 64);
}

// A bound given as the largest count stays the largest, whatever the length:
// the parse's allotment saturates instead of wrapping to a small one.
void an_unbounded_budget_stays_unbounded() {
  constexpr std::uint64_t kAll = ~std::uint64_t{0};
  CHECK(cursiv::repair_allot({kAll, 64, kAll, kAll}, 1000) == kAll);
  CHECK(cursiv::repair_allot({1u << 16, kAll, kAll, kAll}, 1000) == kAll);
  CHECK(cursiv::repair_allot({}, 1000) ==
        cursiv::kRepairFloor + 1000 * cursiv::kRepairPerByte);
  CHECK(cursiv::repair_allot({}, 1u << 30) == cursiv::kRepairPerParse);
}

}  // namespace

int main() {
  std::jthread watchdog([](std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    wake.wait_for(lock, stop, 30s, [] { return false; });
    if (!stop.stop_requested()) std::abort();
  });
  an_owed_run_that_grows_its_stack_ends();
  a_deep_nest_past_the_budget_is_closed();
  the_closers_of_a_deep_nest_hold_one_reading();
  a_forked_closer_costs_one_reading();
  a_run_of_closers_stops_at_a_searchs_share();
  a_completion_the_grammar_makes_long_is_cut();
  an_unbounded_budget_stays_unbounded();
  every_loop_stops_at_its_share();
  unmatched_text_is_one_fault();
  a_long_gap_is_read_once();
  return cursiv::test::report("LrRepairBudget_test");
}
