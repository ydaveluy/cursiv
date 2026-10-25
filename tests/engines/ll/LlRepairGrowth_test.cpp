// What repairing a broken document costs the predictive engine, counted in the
// reads its searches spend -- never in time. Twice the document, or twice the
// fault, at most twice the work: the faults a document holds, their number and
// their density, and how deep they stand.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/LlSearch.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/test/Check.hpp"
#include "parityll/generated/Ll/Module.hpp"
#include "repairgrowthll/generated/Ll/Module.hpp"

namespace {

constexpr double kLinear = 2.2;

const char* const kGood = "let x = a + b * (c - 3)! / f(1, 2);\n";
const char* const kFaults[] = {
    "let x = a + b * (c - 3)! / f(1, 2)\n",
    "let x = = a + b * (c - 3)! / f(1, 2);\n",
    "let x = a + b * (c - 3! / f(1, 2);\n",
    "let x = a + b * (c - 3)! / f(1, 2;\n",
    "let x = a + * (c - 3)! / f(1, 2);\n",
    "let 5 = a + b * (c - 3)! / f(1, 2);\n",
    "let x = a + b @@ (c - 3)! / f(1, 2);\n",
    "let x a + b * (c - 3)! / f(1, 2);\n",
};

std::string repeat(const std::string& unit, unsigned n) {
  std::string text;
  for (unsigned i = 0; i < n; ++i) text += unit;
  return text;
}

std::string lines(unsigned count, unsigned every) {
  std::string text;
  unsigned fault = 0;
  for (unsigned i = 0; i < count; ++i)
    text += i % every == 0 ? kFaults[fault++ % std::size(kFaults)] : kGood;
  return text;
}

std::string burst(unsigned length) {
  return repeat(kGood, 50) + "let y = 1 + " + repeat("@# ", length) + ";\n" +
         repeat(kGood, 50);
}

std::string unclosed(unsigned depth, const std::string& open) {
  return "let a = 1;\nlet x = " + repeat(open, depth) + "1;\nlet b = 2;\n";
}

std::uint64_t work_of(const cursiv::Parser& parser, const std::string& text) {
  cursiv::LlSearchWork::current() = {};
  const cursiv::ParseResult result = parser.parse(text);
  CHECK(result.accepted && !result.diagnostics.empty());
  return cursiv::LlSearchWork::current().work;
}

void doubling_costs_at_most_double(
    const char* family, unsigned size,
    const std::function<std::string(unsigned)>& text) {
  const auto parser = tpar::generated::Ll::create_parser();
  std::uint64_t before = work_of(*parser, text(size));
  CHECK(before != 0);
  for (unsigned step = 1; step <= 2; ++step) {
    const std::uint64_t after = work_of(*parser, text(size << step));
    const bool within =
        static_cast<double>(after) <= kLinear * static_cast<double>(before);
    CHECK(within);
    if (!within)
      std::printf("%s: %u -> %u: %llu -> %llu\n", family, size << (step - 1),
                  size << step, static_cast<unsigned long long>(before),
                  static_cast<unsigned long long>(after));
    before = after;
  }
}

void a_fault_costs_the_same_in_a_longer_document() {
  // A whole number of cycles of kFaults, so doubling keeps the same mix.
  doubling_costs_at_most_double("a fault every 100 lines", 800,
                                [](unsigned n) { return lines(n, 100); });
  doubling_costs_at_most_double("a fault every 10 lines", 400,
                                [](unsigned n) { return lines(n, 10); });
  doubling_costs_at_most_double("a fault on every line", 100,
                                [](unsigned n) { return lines(n, 1); });
}

void a_burst_costs_its_length() {
  doubling_costs_at_most_double("burst", 100, burst);
}

void faults_one_after_another_cost_each_the_same() {
  doubling_costs_at_most_double("missing let", 100, [](unsigned n) {
    return "let a = 1;\n" + repeat("x = a + b;\n", n) + "let b = 2;\n";
  });
  doubling_costs_at_most_double("missing paren", 100, [](unsigned n) {
    return "let a = 1;\n" + repeat("let x = (1 + 2;\n", n) + "let b = 2;\n";
  });
}

void a_parse_costs_the_same_whatever_was_parsed_before() {
  const auto parser = tpar::generated::Ll::create_parser();
  const std::string text = lines(40, 4);
  const std::uint64_t first = work_of(*parser, text);
  work_of(*parser, lines(40, 1));
  CHECK(work_of(*parser, text) == first);
}

void a_fault_deep_in_nesting_costs_what_it_reads_not_how_deep_it_stands() {
  doubling_costs_at_most_double("owed parens", 128,
                                [](unsigned n) { return unclosed(n, "("); });
  doubling_costs_at_most_double("owed calls", 64,
                                [](unsigned n) { return unclosed(n, "f("); });
  doubling_costs_at_most_double("strays under parens", 100, [](unsigned n) {
    return "let x = " + repeat("(", n) + "1" + repeat(" ;", n) + ");\n";
  });
  doubling_costs_at_most_double("noise under parens", 100, [](unsigned n) {
    return "let x = " + repeat("(", n) + "1" + repeat(" @# $%", n) + ");\n";
  });
}

// A first reading with an unknown caller cannot join a known one. Nor can a
// one-frame stack join a two-frame stack. Neither prevents the two compatible
// two-frame stacks from sharing their intermediate and consuming positions.
void compatible_closure_contexts_share_past_incompatible_readings() {
  const cursiv::LlAtnState states[] = {
      {.edge = 0, .nedges = 1},
      {.edge = 1, .nedges = 1},
      {.edge = 2, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.edge = 3, .nedges = 1},
      {.edge = 4, .nedges = 1},
      {.edge = 5, .nedges = 1},
  };
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::atom, .label = 7, .target = 1},
      {.kind = cursiv::LlEdgeKind::epsilon, .target = 2},
      {.kind = cursiv::LlEdgeKind::atom, .label = 8, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 9, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 10, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 11, .target = 3},
  };
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::LlAtnView atn{
      .states = states, .nstates = 7, .edges = edges, .callerBase = callerBase};
  cursiv::LlSimulation plain(atn), shared(atn);
  shared.share_epsilon_contexts();
  const auto start = [](cursiv::LlSimulation& sim) {
    auto& pool = sim.contexts();
    const auto a = pool.push(pool.push(cursiv::kLlCtxNone, 4), 6);
    const auto shortStack = pool.push(cursiv::kLlCtxNone, 6);
    const auto b = pool.push(pool.push(cursiv::kLlCtxNone, 5), 6);
    sim.load({{0, cursiv::kLlCtxNone, 0},
              {0, a, 0},
              {0, shortStack, 0},
              {0, b, 0},
              {0, a, 1}});
  };
  start(plain);
  start(shared);
  CHECK(plain.step(7) && shared.step(7));
  CHECK(plain.configs().size() == 10);
  CHECK(shared.configs().size() == 8);
  for (const auto state : {1u, 2u}) {
    unsigned readings = 0;
    for (const auto& config : shared.configs()) {
      if (config.state != state || config.alt != 0 ||
          config.ctx == cursiv::kLlCtxNone)
        continue;
      ++readings;
      const auto& pool = shared.contexts();
      CHECK(pool.count(config.ctx) == 1);
      CHECK(pool.ret(config.ctx, 0) == 6);
      const auto parent = pool.parent(config.ctx, 0);
      if (parent != cursiv::kLlCtxNone) {
        CHECK(pool.count(parent) == 2);
        CHECK(pool.ret(parent, 0) == 4 && pool.ret(parent, 1) == 5);
      }
    }
    CHECK(readings == 2);
  }
  CHECK(plain.step(8) && shared.step(8));
  CHECK(plain.step(11) && shared.step(11));
  const auto p = plain.configs(), s = shared.configs();
  for (const auto term : {9, 10}) {
    plain.load(p);
    shared.load(s);
    CHECK(plain.step(term) == shared.step(term));
    CHECK(plain.lowest() == shared.lowest());
  }
  // The sharing tables belong to one closure, including after a reset.
  shared.forget();
  start(shared);
  CHECK(shared.step(7) && shared.configs().size() == 8);
}

// T reaches S after S's first known caller has already passed its condition.
// Widening that caller's published union must also traverse the condition
// for T's incoming caller; updating the old position alone loses its return.
void a_condition_reads_every_new_context_of_a_published_union() {
  const cursiv::LlAtnState states[] = {
      {.edge = 0, .nedges = 1},
      {.edge = 1, .nedges = 1},
      {.edge = 2, .nedges = 1},
      {.edge = 3, .nedges = 1},
      {.edge = 4, .nedges = 1},
      {.edge = 5, .nedges = 1},
      {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0},
      {.edge = 6, .nedges = 1},
      {.edge = 7, .nedges = 1},
  };
  const cursiv::LlAtnEdge edges[] = {
      {.kind = cursiv::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::LlEdgeKind::condition, .label = 1, .target = 4},
      {.kind = cursiv::LlEdgeKind::condition, .label = 2, .target = 5},
      {.kind = cursiv::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::LlEdgeKind::atom, .label = 8, .target = 6},
      {.kind = cursiv::LlEdgeKind::atom, .label = 9, .target = 6},
      {.kind = cursiv::LlEdgeKind::atom, .label = 10, .target = 6},
  };
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::LlAtnView atn{
      .states = states, .nstates = 9, .edges = edges, .callerBase = callerBase};
  cursiv::LlSimulation plain(atn), shared(atn);
  shared.share_epsilon_contexts();
  const auto start = [](cursiv::LlSimulation& sim) {
    sim.forget();
    const auto a = sim.contexts().push(cursiv::kLlCtxNone, 7);
    const auto b = sim.contexts().push(cursiv::kLlCtxNone, 8);
    sim.load({{0, cursiv::kLlCtxNone, 0}, {1, a, 0}, {2, b, 0}});
  };
  const auto readings = [](cursiv::LlSimulation& sim) {
    std::vector<std::vector<std::uint32_t>> out;
    const auto& pool = sim.contexts();
    for (const auto& c : sim.configs()) {
      if (c.ctx == cursiv::kLlCtxNone) {
        out.push_back({c.state, c.alt, cursiv::kLlCtxNone});
      } else {
        for (unsigned i = 0; i < pool.count(c.ctx); ++i) {
          CHECK(pool.parent(c.ctx, i) == cursiv::kLlCtxNone);
          out.push_back({c.state, c.alt, pool.ret(c.ctx, i)});
        }
      }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  };
  start(plain);
  start(shared);
  for (auto* sim : {&plain, &shared}) {
    const auto before = sim->configs();
    std::vector<std::uint16_t> terms;
    sim->possible_terms([&](auto term) { terms.push_back(term); });
    CHECK(!terms.empty() && std::all_of(terms.begin(), terms.end(),
                                        [](auto term) { return term == 8; }));
    CHECK(sim->configs() == before);
    CHECK(!sim->filter([](auto condition) { return condition != 2; }));
  }
  start(plain);
  start(shared);
  CHECK(plain.filter([](auto) { return true; }));
  CHECK(shared.filter([](auto) { return true; }));
  CHECK(readings(plain) == readings(shared));
  CHECK(plain.step(8) && shared.step(8));
  CHECK(readings(plain) == readings(shared));
  const auto p = plain.configs(), s = shared.configs();
  for (const auto term : {9, 10}) {
    plain.load(p);
    shared.load(s);
    CHECK(plain.step(term));
    CHECK(shared.step(term));
    CHECK(readings(plain) == readings(shared));
  }
}

// A parse repairs its faults up to a share of work, and says where it stopped:
// enough lines to pass it hold a fault each.
void a_parse_has_a_share_of_repair_and_says_so() {
  const auto parser = tpar::generated::Ll::create_parser();
  const auto told = [&](unsigned count) {
    const auto result = parser->parse(lines(count, 1));
    CHECK(result.accepted && result.value);
    return std::ranges::any_of(result.diagnostics, [](const auto& d) {
      return d.kind == cursiv::DiagnosticKind::bounded;
    });
  };
  CHECK(!told(4000));
  CHECK(told(20000));
}

void recursive_inventions_share_their_continuations() {
  const auto parser = tllrepairgrowth::generated::Ll::create_parser();
  for (const std::string text : {"abacbbb", "abacbb", "abacbbbb"}) {
    cursiv::LlSearchWork::current() = {};
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.value && !result.diagnostics.empty());
    CHECK(cursiv::LlSearchWork::current().work < 200000);
  }
  for (const std::string text :
       {"c", "bc", "ca", "cb", "caa", "ccc", "ccccccc"}) {
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.value && result.diagnostics.empty());
  }
}

}  // namespace

int main() {
  compatible_closure_contexts_share_past_incompatible_readings();
  a_condition_reads_every_new_context_of_a_published_union();
  recursive_inventions_share_their_continuations();
  a_parse_has_a_share_of_repair_and_says_so();
  a_fault_costs_the_same_in_a_longer_document();
  a_burst_costs_its_length();
  faults_one_after_another_cost_each_the_same();
  a_parse_costs_the_same_whatever_was_parsed_before();
  a_fault_deep_in_nesting_costs_what_it_reads_not_how_deep_it_stands();
  return cursiv::test::report("LlRepairGrowth_test");
}
