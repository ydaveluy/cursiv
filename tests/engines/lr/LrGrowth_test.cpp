// Readings that end on one stack are one reading: a text that reads many ways
// costs a power of its length -- in alternatives the graph holds, never in the
// ways it reads -- each alternative costs the same whatever the length, and the
// text reads as the tree the election ranks first.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cursiv/bench/PerfCounters.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/LrShadow.hpp"
#include "cursiv/engines/lr/runtime/LrTrailStep.hpp"
#include "cursiv/engines/lr/runtime/lr_trail.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/GrowthGrammars.hpp"
#include "growth/generated/G/Module.hpp"
#include "growth/generated/G/Parser.hpp"
#include "growth/generated/G/Table.hpp"
#include "growthfastalr/generated/Lr/Module.hpp"
#include "growthfastalr/generated/Lr/Parser.hpp"
#include "growthfastalr/generated/Lr/Table.hpp"

namespace {

// Count the elected elements and text nodes independently. The grammar
// permits void elements and does not require matching opening/closing names.
void html_tree(const std::string& text, unsigned elements, unsigned texts) {
  static const auto parser = tgrowth::generated::G::create_parser();
  const std::string source = "html " + text;
  const auto result = parser->parse(source);
  CHECK(result.accepted && result.diagnostics.empty() && result.value != nullptr);
  const auto* root = result.value_as<tghtml::ast::Elem>();
  CHECK(root != nullptr && root->name == (texts == 0 ? "a" : "div"));
  std::vector<const cursiv::AstNode*> pending{result.value};
  unsigned gotElements = 0, gotTexts = 0;
  while (!pending.empty()) {
    const auto* node = pending.back();
    pending.pop_back();
    if (const auto* elem = cursiv::ast_cast<tghtml::ast::Elem>(node)) {
      ++gotElements;
      CHECK(elem->name == "a" || elem->name == "div" ||
            elem->name == "p" || elem->name == "br");
      CHECK(elem->closeName.empty() || elem->closeName == "a" ||
            elem->closeName == "div" || elem->closeName == "p");
      for (const auto* child : elem->content) pending.push_back(child);
    } else if (const auto* word = cursiv::ast_cast<tghtml::ast::Text>(node)) {
      ++gotTexts;
      CHECK(word->text == "X");
    } else {
      CHECK(false);
    }
  }
  CHECK(gotElements == elements && gotTexts == texts);
}

std::string repeat(const std::string& unit, unsigned n) {
  std::string s;
  for (unsigned i = 0; i < n; ++i) s += unit;
  return s;
}

// One read of `text` by the table itself: the counters are the table's, not
// the erased door's.
struct Read {
  cursiv::LrParseStats stats;
  bool clean = false;
};

template <const auto& T>
Read read(const std::string& text, const cursiv::LrDrive* drive) {
  Read r;
  cursiv::Cst cst;
  const bool ok = cursiv::lr_parser<T>.parse(text, cst, &r.stats, drive);
  r.clean = ok && cst.diagnostics().empty();
  return r;
}

// The graph for twice the text holds at most `ratio` times the alternatives:
// a power of the length, not a number of ways to read it.
void grows_by_at_most(const Read& small, const Read& large, double ratio) {
  CHECK(small.clean && large.clean);
  CHECK(small.stats.splits > 0 && large.stats.splits > 0);
  CHECK(small.stats.alternatives != 0);
  CHECK(static_cast<double>(large.stats.alternatives) <=
        ratio * static_cast<double>(small.stats.alternatives));
}

// No stack was dropped for the order the table lays its arms in: the words of
// the derivations and the lexer's ranks told every pair apart.
void is_decided_by_the_text(const Read& r) {
  CHECK(r.stats.tableElections == 0);
}

// Linear: twice the text, twice the alternatives, with room for the table.
constexpr double kLinear = 2.2;
// Quadratic in the text's length, with room for the table.
constexpr double kQuadratic = 4.6;
// Cubic: the spans of a run, each cut where its last item starts.
constexpr double kCubic = 8.0;

// The instructions one read retires per alternative the graph held -- per byte
// where it opened none -- 0 where the machine counts none, or where the code
// is not optimised and counts what inlining would remove.
template <const auto& T>
double per_alternative(const cursiv::bench::PerfCounters& counters,
                       const std::string& text, const cursiv::LrDrive* drive) {
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  const cursiv::bench::PerfSample from = counters.read();
  counters.enable();
  const bool ok = cursiv::lr_parser<T>.parse(text, cst, &stats, drive);
  counters.disable();
  const cursiv::bench::PerfSample spent =
      cursiv::bench::perf_delta(from, counters.read());
  CHECK(ok && cst.diagnostics().empty());
#ifndef NDEBUG
  return 0;
#endif
  if (!spent.valid || spent.scaled) return 0;
  return static_cast<double>(spent.instructions) /
         (stats.alternatives != 0 ? stats.alternatives : text.size());
}

// The cost per alternative of a text, twice it and four times it; the smallest
// is read once before to settle what a first read builds.
template <const auto& T, class Text>
std::array<double, 3> series(const cursiv::bench::PerfCounters& counters,
                             const cursiv::LrDrive* drive, const Text& text,
                             unsigned first) {
  (void)per_alternative<T>(counters, text(first), drive);
  return {per_alternative<T>(counters, text(first), drive),
          per_alternative<T>(counters, text(2 * first), drive),
          per_alternative<T>(counters, text(4 * first), drive)};
}

// Twice the text, an alternative costs what it did, within a third either way.
void cost_stays_flat(const std::array<double, 3>& cost, double ratio = 1.3) {
  if (cost[0] == 0 || cost[1] == 0 || cost[2] == 0) return;
  CHECK(cost[1] <= ratio * cost[0] && cost[1] >= cost[0] / ratio);
  CHECK(cost[2] <= ratio * cost[1] && cost[2] >= cost[1] / ratio);
}

namespace fasta = tgfasta::generated;

void a_run_inside_a_run_never_forks() {
  const std::string text = ">h\n" + repeat("ACGT\n", 30000);
  const Read r = read<fasta::Lr::kTable>(text, &fasta::Lr::kDrive);
  CHECK(r.clean && r.stats.splits == 0);

  auto lr = fasta::Lr::create_parser();
  const auto result = lr->parse(text);
  const auto* doc = result.value_as<tgfasta::ast::Document>();
  CHECK(doc != nullptr && doc->sections.size() == 2);
  const auto* run =
      doc ? cursiv::ast_cast<tgfasta::ast::Run>(doc->sections[1]) : nullptr;
  CHECK(run != nullptr && run->lines.size() == 30000);

}

namespace html = tgrowth::generated;

void an_optional_close_after_content_costs_a_power() {
  const auto nested = [](unsigned n) {
    return repeat("<a>", n) + repeat("</a>", n);
  };
  const Read a = read<html::G::kTable>("html " + nested(16), &html::G::kDrive);
  const Read b = read<html::G::kTable>("html " + nested(32), &html::G::kDrive);
  grows_by_at_most(a, b, kCubic);
  is_decided_by_the_text(b);

  const auto voids = [](unsigned n) {
    return "<div>" + repeat("<p><br>X</p>", n) + "</div>";
  };
  const Read c = read<html::G::kTable>("html " + voids(15), &html::G::kDrive);
  const Read d = read<html::G::kTable>("html " + voids(30), &html::G::kDrive);
  grows_by_at_most(c, d, kCubic);
  is_decided_by_the_text(d);

  html_tree(nested(24), 24, 0);
  html_tree(voids(16), 33, 16);
}

// Open elements waiting for a close tag the document gives to its last one:
// every reading but one dies at the end, and the one that lives is read whole.
void an_element_left_void_long_after_it_opened_reads_as_the_election_ranks_it() {
  const auto breaks = [](unsigned n) {
    return "<div>" + repeat("<br>X", n) + "</div>";
  };
  const auto voids = [](unsigned n) {
    return "<div>" + repeat("<p><br>X</p>", n) + "</div>";
  };
  for (const unsigned n : {60u, 200u, 400u}) html_tree(breaks(n), n + 1, n);
  for (const unsigned n : {60u, 100u, 150u}) html_tree(voids(n), 2 * n + 1, n);
  html_tree(repeat("<a>", 100) + repeat("</a>", 100), 100, 0);
}

namespace call = tgrowth::generated;

void an_undelimited_argument_list_costs_a_power() {
  const auto names = [](unsigned n) { return "f " + repeat("a ", n) + "!"; };
  const Read a = read<call::G::kTable>("call " + names(40), &call::G::kDrive);
  const Read b = read<call::G::kTable>("call " + names(80), &call::G::kDrive);
  const Read c = read<call::G::kTable>("call " + names(160), &call::G::kDrive);
  grows_by_at_most(a, b, kQuadratic);
  grows_by_at_most(b, c, kQuadratic);
  is_decided_by_the_text(c);

  auto lr = call::G::create_parser();
  const std::string source = "call " + names(160);
  const auto result = lr->parse(source);
  const auto* program = result.value_as<tgcall::ast::Program>();
  CHECK(program != nullptr && program->statements.size() == 1);
  const auto* head =
      program ? cursiv::ast_cast<tgcall::ast::Call>(program->statements[0])
              : nullptr;
  CHECK(head != nullptr && head->args.size() == 160);

}

namespace chain = tgrowth::generated;

void a_chain_written_twice_costs_its_length() {
  const auto dots = [](unsigned n) { return "a" + repeat(".b", n); };
  const Read a = read<chain::G::kTable>("chain " + dots(256), &chain::G::kDrive);
  const Read b = read<chain::G::kTable>("chain " + dots(512), &chain::G::kDrive);
  const Read c = read<chain::G::kTable>("chain " + dots(1024), &chain::G::kDrive);
  grows_by_at_most(a, b, kLinear);
  grows_by_at_most(b, c, kLinear);
  // Each suffix is the operand's or the loop's, and nothing in the words says
  // which: the table's order elects, as the grammar wrote it.
  CHECK(c.stats.tableElections != 0);
}

}  // namespace

void colliding_fingerprints_do_not_elect_a_derivation() {
  cursiv::Cst cst("");
  cursiv::CstBuilder builder(cst);
  cursiv::LrContext c("", builder);
  c.stack.push({0, 0, 0, 0, cursiv::kLrNoWord});
  cursiv::LrShadow a, b;
  a.run.stand_on(c.stack);
  b.run.stand_on(c.stack);
  a.run.push(0);
  b.run.push(0);
  const auto cell = [&](unsigned left, unsigned right) {
    c.trailRoom.restart();
    c.trail = {{cursiv::kLrNoTrail, 0, left, 1, cursiv::kLrNoProd, 1, 0,
                cursiv::kNoMode},
               {cursiv::kLrNoTrail, 0, right, 1, cursiv::kLrNoProd, 1, 0,
                cursiv::kNoMode}};
    a.trail = 0;
    b.trail = 1;
    a.path = 123;
    b.path = 456;
    return cursiv::lr_trail_first<chain::G::kTable>(c, a, b);
  };
  CHECK(cell(1, 2) == -1);
  CHECK(cell(1, 2) == -1);
  CHECK(cell(2, 1) == 1);
  for (unsigned i = 0; i < 5000; ++i) {
    CHECK(cell(1, 2) == -1);
    a.path = 1000u + i;
    CHECK(cursiv::lr_trail_first<chain::G::kTable>(c, a, b) == -1);
    CHECK(cursiv::lr_trail_first<chain::G::kTable>(c, a, b) == -1);
  }
  CHECK(cell(2, 1) == 1);
  c.trail = {{cursiv::kLrNoTrail, 0, ~std::uint32_t{0}, 1, cursiv::kLrNoProd, 1,
              0, cursiv::kNoMode},
             {cursiv::kLrNoTrail, 0, ~std::uint32_t{2}, 2, cursiv::kLrNoProd, 1,
              0, cursiv::kNoMode}};
  CHECK(cursiv::lr_trail_lexical<chain::G::kTable>(c, a, b) == 1);
}

void an_alternative_costs_the_same_however_long_the_text() {
  const cursiv::bench::PerfCounters counters;
  if (!counters.active()) return;
  const auto nested = [](unsigned n) {
    return "html " + repeat("<a>", n) + repeat("</a>", n);
  };
  const auto names = [](unsigned n) { return "call f " + repeat("a ", n) + "!"; };
  const auto dots = [](unsigned n) { return "chain a" + repeat(".b", n); };
  const auto lines = [](unsigned n) { return ">h\n" + repeat("ACGT\n", n); };
  cost_stays_flat(
      series<html::G::kTable>(counters, &html::G::kDrive, nested, 64));
  cost_stays_flat(
      series<call::G::kTable>(counters, &call::G::kDrive, names, 200));
  cost_stays_flat(
      series<chain::G::kTable>(counters, &chain::G::kDrive, dots, 1024));
  cost_stays_flat(
      series<fasta::Lr::kTable>(counters, &fasta::Lr::kDrive, lines, 1000));
}

int main() {
  colliding_fingerprints_do_not_elect_a_derivation();
  a_run_inside_a_run_never_forks();
  an_optional_close_after_content_costs_a_power();
  an_element_left_void_long_after_it_opened_reads_as_the_election_ranks_it();
  an_undelimited_argument_list_costs_a_power();
  a_chain_written_twice_costs_its_length();
  an_alternative_costs_the_same_however_long_the_text();
  return cursiv::test::report("LrGrowth_test");
}
