#include "cells/generated/G/Module.hpp"
#include "cells/generated/G/Parser.hpp"
#include "cells/generated/G/Table.hpp"
// The ceilings a parse meets, counted: look-ahead and repair budgets.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ambtail/generated/G/Parser.hpp"
#include "ambtail/generated/G/Table.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/test/Check.hpp"
#include "election/generated/Election/Module.hpp"
#include "election/generated/Election/Parser.hpp"
#include "election/generated/Election/Table.hpp"
#include "ceilings/generated/G/Module.hpp"
#include "ceilings/generated/G/Parser.hpp"
#include "ceilings/generated/G/Table.hpp"
#include "engines/lr/RankGrowthGrammar.hpp"
#include "room221/generated/G/Parser.hpp"
#include "room221/generated/G/Table.hpp"
#include "torture/DanglingAst.hpp"

namespace {

// A repetition longer than the look-ahead reads: past it no token has told the
// arms apart.
constexpr unsigned kPastTheLook = 40;

}  // namespace

// A ceiling the parse meets is counted, never silently taken.
namespace ceilings {

namespace g = tcells::generated::G;
namespace wide = tcells::generated::G;
namespace deep = telect::generated::Election;

namespace {

// The counters are the table's, not the erased door's: the language module's
// parser drops them, so this drives the generated table itself, on the
// sixteen-by-ten nest.
cursiv::LrParseStats stats_of(const std::string& body) {
  const std::string text = "nests sixteen " + body;
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, g::kDrivePtr);
  return stats;
}

// Sixteen arms over ten, with the deciding token behind a repetition the
// oracle cannot see past: the document asks for every branch at once.
std::string nested(unsigned run, unsigned inner = 1, unsigned outer = 0) {
  std::string s = "p q";
  for (unsigned i = 0; i < run; ++i) s += " a";
  return s + " e" + std::to_string(inner) + " o" + std::to_string(outer);
}

void a_document_that_forks_says_how_often() {
  CHECK(stats_of(nested(kPastTheLook)).splits > 0);
}

// A search keeps the readings it may still expand: a document that faults at
// every position leaves its pool under a tenth of the work spent.
void a_noisy_document_leaves_the_search_little_room() {
  std::string noise;
  std::uint32_t x = 12345;
  for (unsigned i = 0; i < 6000; ++i) {
    x = x * 1103515245u + 12345u;
    noise += "pqaeo01 "[(x >> 16) % 8];
  }
  const cursiv::LrParseStats s = stats_of(noise);
  CHECK(s.work > 100000);
  CHECK(s.searchRoom * 10 < s.work);
}

// The look budget is a depth in tokens: the same document one repetition
// under it is settled by the text, and over it by the ceiling. Only the
// counter tells the two apart -- both answer with an arm.
void the_look_budget_says_when_it_answered() {
  CHECK(stats_of(nested(20)).lookStops == 0);
  CHECK(stats_of(nested(kPastTheLook)).lookStops > 0);
}

// Twenty-four arms over twenty-four: every corner of the nest reads clean
// whichever arm the text names last.
void a_wide_nest_reads_clean_in_every_corner() {
  for (unsigned outer : {0u, 23u})
    for (unsigned inner : {0u, 23u}) {
      const std::string text = "huge " + nested(kPastTheLook, inner, outer);
      cursiv::Cst cst;
      cursiv::LrParseStats stats;
      const bool ok = cursiv::lr_parser<wide::kTable>.parse(text, cst, &stats,
                                                            wide::kDrivePtr);
      CHECK(ok);
      CHECK(cst.diagnostics().empty());
    }
}

// The far corner of the nest under a starved repair budget: the reading
// that names `e23` finishes at no cost, and no span is said on it.
void a_starved_budget_leaves_the_far_corner_clean() {
  const cursiv::RepairBudget starved{.floor = 1, .perByte = 0};
  cursiv::Cst cst;
  const bool ok = cursiv::lr_parser<wide::kTable>.parse(
      "huge " + nested(kPastTheLook, 23, 23), cst, nullptr, wide::kDrivePtr, starved);
  CHECK(ok);
  CHECK(cst.diagnostics().empty());
}

// What a chain of unclosed constructs is owed does not change with its
// length: one token, named once, over a document every byte of which is
// still under a record.
cursiv::LrParseStats a_deep_nest_is_owed_one_token(unsigned depth) {
  std::string text;
  for (unsigned i = 0; i < depth; ++i) text += "if a then ";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  cursiv::lr_parser<deep::kTable>.parse(text, cst, &stats, deep::kDrivePtr);
  CHECK(cst.diagnostics().size() == 1);
  cursiv::TextOffset covered = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    if (cst[i].end > covered) covered = cst[i].end;
  CHECK(covered == static_cast<cursiv::TextOffset>(text.size()) - 1);
  return stats;
}

void a_nest_says_the_same_thing_at_every_depth() {
  for (unsigned d : {1u, 8u, 12u, 24u, 40u, 64u})
    a_deep_nest_is_owed_one_token(d);
}

// A nest deeper than any scan is closed whole: the tokens it owes are one
// run, which no budget of the search cuts.
void a_nest_deeper_than_any_scan_is_closed_whole() {
  CHECK(a_deep_nest_is_owed_one_token(300).budgetStops == 0);
}

}  // namespace

int run() {
  a_document_that_forks_says_how_often();
  a_noisy_document_leaves_the_search_little_room();
  the_look_budget_says_when_it_answered();
  a_wide_nest_reads_clean_in_every_corner();
  a_starved_budget_leaves_the_far_corner_clean();
  a_nest_says_the_same_thing_at_every_depth();
  a_nest_deeper_than_any_scan_is_closed_whole();
  return 0;
}

}  // namespace ceilings

// A valid document is never said to be bounded: the drawn witnesses whose
// search once ran out over a room of frozen readings, each reduced, read
// clean.
namespace frozen_room {

namespace g221 = troom::seed221::generated::G;

namespace {

template <const auto& T>
void reads_clean(std::string_view text, const cursiv::LrDrive* drive) {
  cursiv::Cst cst;
  const bool ok = cursiv::lr_parser<T>.parse(text, cst, nullptr, drive);
  CHECK(ok);
  CHECK(cst.diagnostics().empty());
}

void the_seed_221_reads_clean() {
  reads_clean<g221::kTable>("acaccdaaaad", g221::kDrivePtr);
}



}  // namespace

int run() {
  the_seed_221_reads_clean();
  return 0;
}

}  // namespace frozen_room

// A drawn nest of dangling elses, deep and long: it reads clean within the
// lane's timeout.
namespace room_scale {

namespace deep = telect::generated::Election;

namespace {

struct Draw {
  std::uint64_t s;
  double next() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(s >> 11) / 9007199254740992.0;
  }
};

std::string nest(Draw& d, unsigned depth) {
  if (depth == 0 || d.next() < 0.15) return "x";
  std::string s = "if c then " + nest(d, depth - 1);
  if (d.next() < 0.6) s += " else " + nest(d, depth - 1);
  return s;
}

void a_full_room_reads_the_nest_clean() {
  Draw d{1};
  const std::string text = nest(d, 14);
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<deep::kTable>.parse(text, cst, &stats,
                                              deep::kDrivePtr));
  CHECK(cst.diagnostics().empty());
}

}  // namespace

int run() {
  a_full_room_reads_the_nest_clean();
  return 0;
}

}  // namespace room_scale

// Readings that end on one stack are one reading, whichever way they got
// there: a document does not fork at every operand two rules derive, nor at a
// cell whose arms reduce by chains of different lengths.
namespace folding {

namespace twin = tceilings::generated::G;
namespace tail = tambtail::generated::G;

template <const auto& T>
void reads_without_forking(const std::string& text, const cursiv::LrDrive* d) {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<T>.parse(text, cst, &stats, d));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits == 0);
}

void an_operand_two_rules_derive_is_one_reading() {
  std::string text = "a";
  for (unsigned i = 0; i < 60; ++i) text += "?a:a";
  reads_without_forking<twin::kTable>("fold " + text, twin::kDrivePtr);
}

void a_cell_whose_arms_reduce_by_different_chains_is_one_reading() {
  std::string text;
  for (unsigned i = 0; i < 40; ++i) text += "p(n) ";
  reads_without_forking<tail::kTable>(text, tail::kDrivePtr);
}

int run() {
  an_operand_two_rules_derive_is_one_reading();
  a_cell_whose_arms_reduce_by_different_chains_is_one_reading();
  return 0;
}

}  // namespace folding

// A few elses under a deep nest of dangling ifs: each else may close any of
// the open ifs, and the document reads clean.
namespace waiting_budget {

namespace deep = telect::generated::Election;

void a_deep_nest_reads_its_elses_clean() {
  std::string text;
  for (unsigned i = 0; i < 800; ++i) text += "if c then ";
  text += "x";
  for (unsigned i = 0; i < 10; ++i) text += " else x";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<deep::kTable>.parse(text, cst, &stats,
                                              deep::kDrivePtr));
  CHECK(cst.diagnostics().empty());
}

int run() {
  a_deep_nest_reads_its_elses_clean();
  return 0;
}

}  // namespace waiting_budget

// A caret behind a right-nested chain: every level reduces to the one node,
// which gathers a predecessor per level, so its predecessors outgrow the
// block they start in many times over and must all still be found.
namespace caret_chain {

// A node whose predecessors grow is read again for the one it gained,
// not for all of them: sixteen hundred levels answer in a moment, where every
// level read afresh made the cube of them.
void a_caret_behind_a_longer_chain_answers_in_a_moment() {
  const auto parser = telect::generated::Election::create_parser();
  std::string text = "if c then x";
  for (unsigned i = 0; i < 1600; ++i) text += " else if c then x";
  text += " ";
  const auto r = parser->parse(text);
  CHECK(r.accepted && r.diagnostics.empty());
  std::vector<cursiv::NextFeature> out;
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  const auto begin = std::chrono::steady_clock::now();
  parser->nextFeaturesAt(r.cst(), at, at, out);
  const auto took = std::chrono::steady_clock::now() - begin;
  bool offered = false;
  for (const auto& f : out)
    offered = offered || parser->getGrammar().terminalText(f.feature) == "else";
  CHECK(offered);
  CHECK(took < std::chrono::seconds(5));
}

int run() {
  a_caret_behind_a_longer_chain_answers_in_a_moment();
  return 0;
}

}  // namespace caret_chain

// A graph that forks at every statement and never has fewer than two readings
// alive is read in a moment, not in the square of its statements: two hundred
// thousand of them, one graph, which writes and gives back what every later
// reading stands on.
namespace long_episode {

namespace gl = tceilings::generated::G;

std::string statements(unsigned n) {
  std::string text = "episode x";
  for (unsigned i = 0; i < kPastTheLook; ++i) text += " a";
  text += " c";
  for (unsigned i = 0; i < n; ++i) text += " x a c";
  return text;
}

void a_long_episode_releases_what_it_opened() {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<gl::kTable>.parse(statements(200000), cst, &stats,
                                            gl::kDrivePtr));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits >= 1);
}

void local_conflicts_share_a_deep_frozen_stack() {
  constexpr unsigned depth = 512;
  std::string text = "episode " + std::string(depth, '{');
  for (unsigned i = 0; i < depth; ++i) {
    text += "x";
    for (unsigned j = 0; j < kPastTheLook; ++j) text += " a";
    text += " b {} {} {} ";
  }
  text.append(depth, '}');
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<gl::kTable>.parse(text, cst, &stats, gl::kDrivePtr));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits > 0 && stats.splits < depth / 2);
}

// The same graph refused at the end, long after it last wrote: the readings it
// froze there are still the repair's to set out from.
void a_long_episode_repairs_its_end() {
  cursiv::Cst cst;
  CHECK(cursiv::lr_parser<gl::kTable>.parse(statements(20000) + " x a", cst,
                                            nullptr, gl::kDrivePtr));
  CHECK(cst.diagnostics().size() == 1);
}

int run() {
  a_long_episode_releases_what_it_opened();
  local_conflicts_share_a_deep_frozen_stack();
  a_long_episode_repairs_its_end();
  return 0;
}

}  // namespace long_episode

// A valid document the graph holds a long while reads clean, and as the
// derivation the election ranks first: the one a grammar that takes it
// outright reads.
namespace held_graph {

namespace loose = tceilings::generated::G;
namespace taken = tceilings::generated::G;
namespace deep = telect::generated::Election;

using Spans = std::vector<std::array<cursiv::TextOffset, 3>>;

std::string equations(unsigned n) {
  std::string text;
  for (unsigned i = 0; i < n; ++i)
    text += "f_" + std::to_string(i) + " x = y where y = x\n";
  return text;
}

template <const auto& T>
Spans read(const std::string& text, const cursiv::LrDrive* drive,
           cursiv::LrParseStats& stats) {
  cursiv::Cst cst;
  CHECK(cursiv::lr_parser<T>.parse(text, cst, &stats, drive));
  CHECK(cst.diagnostics().empty());
  Spans spans;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    spans.push_back({cst[i].begin, cst[i].end, cst[i].subtreeSize});
  return spans;
}

void a_short_document_reads_as_the_taken_block() {
  cursiv::LrParseStats held, reference;
  const std::string text = equations(8);
  const Spans spans = read<loose::kTable>("open " + text, loose::kDrivePtr, held);
  CHECK(held.splits >= 1);
  CHECK(spans == read<taken::kTable>("take " + text, taken::kDrivePtr, reference));
}

void a_long_document_reads_as_the_taken_block() {
  cursiv::LrParseStats held, reference;
  const std::string text = equations(100);
  const Spans spans = read<loose::kTable>("open " + text, loose::kDrivePtr, held);
  CHECK(spans == read<taken::kTable>("take " + text, taken::kDrivePtr, reference));
}

void a_long_document_repairs_its_end_as_the_taken_block() {
  const std::string text = equations(100) + "g x =";
  cursiv::Cst held, reference;
  cursiv::LrParseStats stats;
  const std::string open = "open " + text, take = "take " + text;
  CHECK(cursiv::lr_parser<loose::kTable>.parse(open, held, &stats,
                                               loose::kDrivePtr));
  CHECK(stats.episodeSearches > 0);
  CHECK(cursiv::lr_parser<taken::kTable>.parse(take, reference, nullptr,
                                               taken::kDrivePtr));
  CHECK(!held.diagnostics().empty());
  CHECK(held.diagnostics().size() == reference.diagnostics().size());
  CHECK(held.size() == reference.size());
  for (cursiv::NodeId i = 0; i < std::min(held.size(), reference.size()); ++i) {
    CHECK(held[i].begin == reference[i].begin);
    CHECK(held[i].end == reference[i].end);
    CHECK(held[i].subtreeSize == reference[i].subtreeSize);
    CHECK(held[i].recovered == reference[i].recovered);
  }
  for (std::size_t i = 0; i < std::min(held.diagnostics().size(),
                                       reference.diagnostics().size());
       ++i) {
    CHECK(held.diagnostics()[i].kind == reference.diagnostics()[i].kind);
    CHECK(held.diagnostics()[i].begin == reference.diagnostics()[i].begin);
    CHECK(held.diagnostics()[i].end == reference.diagnostics()[i].end);
  }
}

// Two hundred open ifs and a hundred elses: each else closes the nearest if
// left open, as the election ranks them.
void a_deep_nest_binds_each_else_to_the_nearest_if() {
  constexpr unsigned kIfs = 200, kElses = 100;
  std::string text;
  for (unsigned i = 0; i != kIfs; ++i) text += "if c then ";
  text += "x";
  for (unsigned i = 0; i != kElses; ++i) text += " else e" + std::to_string(i);
  const auto result = deep::create_parser()->parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  const auto* stmt = result.value_as<tdang::ast::Stmt>();
  for (unsigned level = 0; level != kIfs; ++level) {
    CHECK(stmt != nullptr);
    if (stmt == nullptr) return;
    const bool bound = level >= kIfs - kElses;
    CHECK((stmt->else_ != nullptr) == bound);
    if (bound && stmt->else_ != nullptr)
      CHECK(stmt->else_->cond == "e" + std::to_string(kIfs - 1 - level));
    stmt = stmt->then_;
  }
}

int run() {
  a_short_document_reads_as_the_taken_block();
  a_long_document_reads_as_the_taken_block();
  a_deep_nest_binds_each_else_to_the_nearest_if();
  a_long_document_repairs_its_end_as_the_taken_block();
  return 0;
}

}  // namespace held_graph

// A long ambiguity shares its growing choice rank, and the election still
// respects an equal or a late winner.
namespace rank_growth {

int run() {
  namespace g = tceilings::generated::G;
  std::string text = "rank x";
  for (unsigned i = 0; i != 8000; ++i) text += " a";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, g::kDrivePtr));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits > 0);

  const auto parser = g::create_parser();
  for (const bool second : {false, true}) {
    const auto result = parser->parse(second ? text + "!" : text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* node = result.value_as<trankgrowth::Node>();
    CHECK(node && node->first == !second && node->second == second);
  }
  return 0;
}

}  // namespace rank_growth

int main() {
  int status = 0;
  status |= ceilings::run();
  status |= frozen_room::run();
  status |= room_scale::run();
  status |= folding::run();
  status |= waiting_budget::run();
  status |= caret_chain::run();
  status |= long_episode::run();
  status |= held_graph::run();
  status |= rank_growth::run();
  return cursiv::test::report("LrCeilings_test") | status;
}
