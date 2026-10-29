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
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/lr/runtime/LrCaretTable.hpp"
#include "cursiv/engines/lr/runtime/LrGssGraph.hpp"
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

// The reduction reached after S -> '(' S; on the caller's terminal it folds
// every pending opening before the outer state can shift that terminal.
// Only the actions and gotos the lexical probe reads are needed here.
struct ProbeTable {
  struct Cfg {
    unsigned nprods = 1;
    std::array<cursiv::LrProduction, 1> prods{{{.lhs = 0, .len = 2}}};
  } cfg;
  std::array<std::uint16_t, 0> guardOps{};
  struct Programs {
    std::array<std::uint32_t, 2> op{
        (2u << 2) | static_cast<unsigned>(cursiv::LrActionKind::shift),
        static_cast<unsigned>(cursiv::LrActionKind::reduce)};
    constexpr unsigned cell(unsigned state, unsigned) const {
      return state == 3 ? 2 : state == 2 ? 1 : 0;
    }
    constexpr unsigned first(unsigned cell) const { return cell ? cell - 1 : 0; }
    constexpr unsigned count(unsigned cell) const { return cell != 0; }
  } programs;
  struct Gotos {
    unsigned* visits = nullptr;
    constexpr unsigned get(unsigned state, unsigned) const {
      if (visits != nullptr) ++*visits;
      return state == 0 ? 2 : state == 1 ? 3 : cursiv::kInvalidSymbol;
    }
  } gotoPrograms;
};
constexpr ProbeTable kProbeTable{};
unsigned probeLandings = 0;
constexpr unsigned kProbeDiamonds = 24;
constexpr ProbeTable kWideProbeTable = [] {
  ProbeTable table;
  table.cfg.prods[0].len = 2u * kProbeDiamonds;
  table.gotoPrograms.visits = &probeLandings;
  return table;
}();

void lexical_probes_fold_deep_stacks_without_recursion() {
  constexpr std::uint32_t depth = 65536;
  cursiv::LrStack stack;
  stack.push({0, 0, 0, 0, 0});
  for (std::uint32_t i = 0; i < depth; ++i) stack.push({1, 0, 0, 0, 0});
  stack.push({3, 0, 0, 0, 0});
  cursiv::LrGssGraph graph;
  cursiv::LrGssGraph::Reading start;
  start.run.state = 3;
  for (const std::uint32_t window : {32u, 128u, stack.size()}) {
    graph.seed(stack, start, {}, window);
    const bool takes = cursiv::lr_gss_takes<kProbeTable>(
        graph, stack.size() - graph.seed_base() - 1u, 1,
        [](unsigned) { return true; });
    CHECK(takes == (window == stack.size()));
    CHECK(graph.underflow() == (window != stack.size()));
  }
}

void lexical_probes_share_paths_at_each_remaining_depth() {
  using Graph = cursiv::LrGssGraph;
  Graph graph;
  cursiv::LrStack stack;
  stack.push({0, 0, 0, 0, 0});
  graph.seed(stack, {});
  const auto shift = [&](Graph::Id from, unsigned at, unsigned state) {
    auto reading = graph.reading(from);
    Graph::Step step;
    step.vertex = from;
    step.reading = reading;
    step.reading.la = {reading.at, at, 1};
    step.shifts = true;
    reading.at = at;
    reading.run.state = state;
    const auto to = graph.vertex(reading);
    graph.edge(to, from, 1, {}, graph.add(step));
    return to;
  };
  Graph::Id top = 0;
  for (unsigned i = 0; i < kProbeDiamonds; ++i) {
    const auto first = shift(top, 2u * i + 1u, 1);
    const auto second = shift(top, 2u * i + 1u, 2);
    top = shift(first, 2u * i + 2u, 3);
    CHECK(shift(second, 2u * i + 2u, 3) == top);
  }
  probeLandings = 0;
  CHECK(cursiv::lr_gss_takes<kWideProbeTable>(
      graph, top, 1, [](unsigned) { return true; }));
  // The 2^24 paths all end on the seed's one bottom vertex.
  CHECK(probeLandings == 1);
  CHECK(!graph.underflow());
}

constexpr ProbeTable kUnequalProbeTable = [] {
  ProbeTable table;
  table.cfg.prods[0].len = 4;
  return table;
}();

void lexical_probes_keep_a_vertex_at_different_remaining_depths() {
  using Graph = cursiv::LrGssGraph;
  Graph graph;
  cursiv::LrStack stack;
  stack.push({0, 0, 0, 0, 0});
  graph.seed(stack, {});
  const auto edge = [&](Graph::Id from, unsigned at) {
    Graph::Reading reading;
    reading.at = at;
    reading.run.state = at == 4 ? 3 : 1;
    const auto to = graph.vertex(reading);
    Graph::Step step;
    step.vertex = from;
    graph.edge(to, from, 1, {}, graph.add(step));
    return to;
  };
  const auto first = edge(0, 1), second = edge(first, 2);
  const auto third = edge(second, 3);
  const auto top = edge(second, 4);  // A shorter path, published first.
  CHECK(edge(third, 4) == top);
  // Only the four-edge path reaches the seed at the requested depth. A
  // global vertex mark would discard the second visit of `second`.
  CHECK(cursiv::lr_gss_takes<kUnequalProbeTable>(
      graph, top, 1, [](unsigned) { return true; }));
}

// A flagged token is probed before it is shifted. Its reductions may cross
// the seed's floor without entering the ordinary reduction agenda.
void a_lexical_probe_requests_the_frames_below_a_partial_seed() {
  for (const std::uint32_t window : {32u, 128u}) {
    cursiv::LrStack stack;
    for (std::uint32_t i = 0; i < window + 9; ++i)
      stack.push({static_cast<std::uint16_t>(i), cursiv::kNoElement, 0, 0,
                  cursiv::kLrNoWord});
    cursiv::LrGssGraph graph;
    cursiv::LrGssGraph::Reading start;
    start.run.state = stack.top().state;
    const auto reaches = [&](auto&& self, cursiv::LrGssGraph::Id vertex,
                             std::uint32_t depth) -> bool {
      if (depth == 0) return true;
      bool reached = false;
      graph.each_below(vertex, [&](cursiv::LrGssGraph::Id below) {
        reached = self(self, below, depth - 1) || reached;
      });
      return reached;
    };
    graph.seed(stack, start, {}, window);
    CHECK(graph.seed_base() == 9 && !graph.underflow());
    CHECK(reaches(reaches, window - 1, window - 1));
    CHECK(!graph.underflow());
    CHECK(!reaches(reaches, window - 1, window));
    CHECK(graph.underflow());

    graph.seed(stack, start, {}, window * 4);
    CHECK(graph.seed_base() == 0 && !graph.underflow());
    CHECK(reaches(reaches, stack.size() - 1, window));
    CHECK(!graph.underflow());
    CHECK(!reaches(reaches, stack.size() - 1, stack.size()));
    CHECK(!graph.underflow());
  }
}

}  // namespace

// A ceiling the parse meets is counted, never silently taken.
namespace ceilings {

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
  cursiv::lr_parser<wide::kTable>.parse(text, cst, &stats, &wide::kDrive);
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
                                                            &wide::kDrive);
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
      "huge " + nested(kPastTheLook, 23, 23), cst, nullptr, &wide::kDrive, starved);
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
  cursiv::lr_parser<deep::kTable>.parse(text, cst, &stats, &deep::kDrive);
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

void run() {
  a_document_that_forks_says_how_often();
  a_noisy_document_leaves_the_search_little_room();
  the_look_budget_says_when_it_answered();
  a_wide_nest_reads_clean_in_every_corner();
  a_starved_budget_leaves_the_far_corner_clean();
  a_nest_says_the_same_thing_at_every_depth();
  a_nest_deeper_than_any_scan_is_closed_whole();
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
  reads_clean<g221::kTable>("acaccdaaaad", &g221::kDrive);
}



}  // namespace

void run() {
  the_seed_221_reads_clean();
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
                                              &deep::kDrive));
  CHECK(cst.diagnostics().empty());
}

}  // namespace

void run() {
  a_full_room_reads_the_nest_clean();
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
  reads_without_forking<twin::kTable>("fold " + text, &twin::kDrive);
}

void a_cell_whose_arms_reduce_by_different_chains_is_one_reading() {
  std::string text;
  for (unsigned i = 0; i < 40; ++i) text += "p(n) ";
  reads_without_forking<tail::kTable>(text, &tail::kDrive);
}

void run() {
  an_operand_two_rules_derive_is_one_reading();
  a_cell_whose_arms_reduce_by_different_chains_is_one_reading();
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
                                              &deep::kDrive));
  CHECK(cst.diagnostics().empty());
}

void run() {
  a_deep_nest_reads_its_elses_clean();
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
    for (const auto s : parser->getGrammar().terminalSpellings(f.feature))
      offered = offered || s == "else";
  CHECK(offered);
  CHECK(took < std::chrono::seconds(5));
}

void run() {
  a_caret_behind_a_longer_chain_answers_in_a_moment();
}

}  // namespace caret_chain

// Root -> E0 E1 ... E32759 ('x' | 'y'), each Ei -> epsilon. A candidate
// reaches thousands of different nodes at one position before its shift.
namespace caret_nullable {

constexpr unsigned count = 32760;

cursiv::LrCaretTable::Run run(std::uint16_t state, cursiv::SymbolId term) {
  return (term == 1 || term == 2) && state <= count
             ? cursiv::LrCaretTable::Run{2u * state + term - 1u, 1}
             : cursiv::LrCaretTable::Run{};
}
cursiv::LrAction action(std::uint32_t index) {
  const unsigned state = index / 2, term = index % 2 + 1;
  return {static_cast<cursiv::SymbolId>(term),
          state < count ? cursiv::LrActionKind::reduce
                        : cursiv::LrActionKind::shift,
          static_cast<std::uint16_t>(state < count ? state : count + term)};
}
std::uint32_t go(std::uint32_t, cursiv::SymbolId nt) { return nt + 1; }
cursiv::Scan lex(const char* src, cursiv::TextOffset len, std::uint16_t,
                 cursiv::TextOffset from, cursiv::TextOffset& halt) {
  halt = len;
  if (from == len) return {from, 0};
  if (src[from] == 'x') return {from + 1, 1};
  if (src[from] == 'y') return {from + 1, 2};
  return {from, cursiv::kInvalidSymbol};
}
std::span<const std::uint64_t> follow(std::uint16_t) {
  static constexpr std::uint64_t bits[] = {6};
  return bits;
}

void a_wide_nullable_closure_keeps_candidate_order() {
  std::vector<cursiv::LrProduction> prods(count);
  for (unsigned i = 0; i < count; ++i)
    prods[i].lhs = static_cast<cursiv::SymbolId>(i);
  std::vector<cursiv::TermInfo> terms(3);
  std::vector<std::uint16_t> mode(count + 3, 0);
  std::vector<std::uint16_t> sole(count + 3, cursiv::kLrNoProd);
  std::vector<std::uint32_t> base(count + 4, 0);
  std::fill(base.begin() + count + 1, base.end(), 2);
  const std::uint16_t items[] = {0, 1};
  const cursiv::LrExpect pool[] = {
      {1, cursiv::kNoElement, cursiv::kNoElement},
      {2, cursiv::kNoElement, cursiv::kNoElement}};
  const std::uint8_t tight[] = {0};
  const std::uint64_t skips[] = {0, 0, 0, 0};
  cursiv::LrCaretTable table;
  table.lex = lex;
  table.run = run;
  table.action = action;
  table.go = go;
  table.follow = follow;
  table.mode = mode;
  table.modeTight = tight;
  table.modeMulti = tight;
  table.skipSet = skips;
  table.soleReduce = sole;
  table.terms = terms;
  table.prods = prods;
  table.expectBase = base;
  table.expectItems = items;
  table.expectPool = pool;
  const std::string_view spellings[] = {"x", "y"};
  const cursiv::GrammarTerminal words[] = {
      {}, {{}, {}, false, {spellings, 1}}, {{}, {}, false, {spellings + 1, 1}}};
  const cursiv::GrammarMetaData meta{.terminals = words, .terminalCount = 3};
  const cursiv::StampedGrammar grammar(meta, table.stamp);
  cursiv::Cst c("");
  cursiv::CstBuilder builder(c);
  builder.bind_stream();
  std::vector<cursiv::NextFeature> out;
  const auto start = std::chrono::steady_clock::now();
  cursiv::lr_next_features_at(table, grammar, c, 0, 0, out);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  CHECK(out.size() == 2);
  if (out.size() != 2) return;
  CHECK(out[0].feature == 1 && out[1].feature == 2);
  for (const auto& feature : out)
    CHECK(feature.property.empty() && feature.narrow == cursiv::kNoNarrow &&
          feature.type == nullptr);
}

void run() {
  a_wide_nullable_closure_keeps_candidate_order();
}

}  // namespace caret_nullable

// One statement past the look-ahead is read by a graph, which gives the text
// back once a reading past it validates the arm that died there: the two
// hundred thousand statements after it read as if it were not there, and the
// graph holds as much however many follow.
namespace long_episode {

namespace gl = tceilings::generated::G;

std::string statements(unsigned n) {
  std::string text = "episode x";
  for (unsigned i = 0; i < kPastTheLook; ++i) text += " a";
  text += " c";
  for (unsigned i = 0; i < n; ++i) text += " x a c";
  return text;
}

void a_long_statement_slows_only_itself() {
  cursiv::LrParseStats one;
  {
    cursiv::Cst cst;
    CHECK(cursiv::lr_parser<gl::kTable>.parse(statements(1), cst, &one,
                                              &gl::kDrive));
  }
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<gl::kTable>.parse(statements(200000), cst, &stats,
                                            &gl::kDrive));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits == 1);
  CHECK(stats.alternatives == one.alternatives);
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
  CHECK(cursiv::lr_parser<gl::kTable>.parse(text, cst, &stats, &gl::kDrive));
  CHECK(cst.diagnostics().empty());
  // Each conflict's graph copies the frames it reduces, not the deep stack.
  CHECK(stats.splits > 0 && stats.seeded <= std::uint64_t{64} * depth);
}

// Refused at the end, long after the first graph gave the text back: the cell
// whose arms the end refutes opens a graph of its own, and the repair sets out
// from the readings it froze.
void a_long_episode_repairs_its_end() {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<gl::kTable>.parse(statements(20000) + " x a", cst,
                                            &stats, &gl::kDrive));
  CHECK(cst.diagnostics().size() == 1);
  CHECK(stats.splits == 2 && stats.episodeSearches == 1);
}

void run() {
  a_long_statement_slows_only_itself();
  local_conflicts_share_a_deep_frozen_stack();
  a_long_episode_repairs_its_end();
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
  const Spans spans = read<loose::kTable>("open " + text, &loose::kDrive, held);
  CHECK(held.splits >= 1);
  CHECK(spans == read<taken::kTable>("take " + text, &taken::kDrive, reference));
}

void a_long_document_reads_as_the_taken_block() {
  cursiv::LrParseStats held, reference;
  const std::string text = equations(100);
  const Spans spans = read<loose::kTable>("open " + text, &loose::kDrive, held);
  CHECK(spans == read<taken::kTable>("take " + text, &taken::kDrive, reference));
}

void a_long_document_repairs_its_end_as_the_taken_block() {
  const std::string text = equations(100) + "g x =";
  cursiv::Cst held, reference;
  cursiv::LrParseStats stats;
  const std::string open = "open " + text, take = "take " + text;
  CHECK(cursiv::lr_parser<loose::kTable>.parse(open, held, &stats,
                                               &loose::kDrive));
  CHECK(stats.episodeSearches > 0);
  CHECK(cursiv::lr_parser<taken::kTable>.parse(take, reference, nullptr,
                                               &taken::kDrive));
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

// A fault is repaired, the text after it forks, and the graph cuts readings
// it cannot hold. The reading has gone on from the fault, so the span the cut
// says is laid behind the fault's note: notes lie in the order their texts end.
void a_cut_span_is_said_behind_the_fault_before_it() {
  const cursiv::RepairBudget starved{.floor = 1, .perByte = 0};
  cursiv::Cst cst;
  CHECK(cursiv::lr_parser<loose::kTable>.parse(
      "open z9 = y where z9 = f ab ab ab x g z9 x x g z9 x = g where y = x "
      "where f = g ab ab f z9 ab z9 f y z9 ab f y g ; f",
      cst, nullptr, &loose::kDrive, starved));
  const auto& found = cst.diagnostics();
  CHECK(found.size() > 1);
  if (found.size() < 2) return;
  CHECK(found[0].kind == cursiv::DiagnosticKind::deleted);
  CHECK(found[1].kind == cursiv::DiagnosticKind::bounded);
  for (std::size_t i = 1; i < found.size(); ++i)
    CHECK(found[i - 1].end <= found[i].end);
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

void run() {
  a_short_document_reads_as_the_taken_block();
  a_long_document_reads_as_the_taken_block();
  a_deep_nest_binds_each_else_to_the_nearest_if();
  a_long_document_repairs_its_end_as_the_taken_block();
  a_cut_span_is_said_behind_the_fault_before_it();
}

}  // namespace held_graph

// A long ambiguity shares its growing choice rank, and the election still
// respects an equal or a late winner.
namespace rank_growth {

void run() {
  namespace g = tceilings::generated::G;
  std::string text = "rank x";
  for (unsigned i = 0; i != 8000; ++i) text += " a";
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, &g::kDrive));
  CHECK(cst.diagnostics().empty());
  CHECK(stats.splits > 0);

  const auto parser = g::create_parser();
  for (const bool second : {false, true}) {
    const auto result = parser->parse(second ? text + "!" : text);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* node = result.value_as<trankgrowth::Node>();
    CHECK(node && node->first == !second && node->second == second);
  }
}

}  // namespace rank_growth

int main() {
  lexical_probes_fold_deep_stacks_without_recursion();
  lexical_probes_share_paths_at_each_remaining_depth();
  lexical_probes_keep_a_vertex_at_different_remaining_depths();
  a_lexical_probe_requests_the_frames_below_a_partial_seed();
  ceilings::run();
  frozen_room::run();
  room_scale::run();
  folding::run();
  waiting_budget::run();
  caret_chain::run();
  caret_nullable::run();
  long_episode::run();
  held_graph::run();
  rank_growth::run();
  return cursiv::test::report("LrCeilings_test");
}
