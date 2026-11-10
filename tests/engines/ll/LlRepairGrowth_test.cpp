// What repairing a broken document costs the predictive engine, in search
// reads, never time: twice the document or twice the faults is at most twice
// the work, by fault count, density and depth.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/ll/runtime/LlPredictionWork.hpp"
#include "cursiv/ll/runtime/LlSearch.hpp"
#include "cursiv/ll/table/LlSimulation.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/utils/Cancellation.hpp"
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
  cursiv::engine::LlSearchWork::current() = {};
  const cursiv::ParseResult result = parser.parse(text);
  CHECK(result.accepted && !result.diagnostics.empty());
  return cursiv::engine::LlSearchWork::current().work;
}

// ...and the tokens its predictions read.
std::uint64_t predictions_of(const cursiv::Parser& parser,
                             const std::string& text) {
  cursiv::engine::LlPredictionWork::current() = {};
  const cursiv::ParseResult result = parser.parse(text);
  CHECK(result.accepted && !result.diagnostics.empty());
  return cursiv::engine::LlPredictionWork::current().tokens;
}

void doubling_costs_at_most_double(
    const char* family, unsigned size,
    const std::function<std::string(unsigned)>& text,
    std::uint64_t (*cost)(const cursiv::Parser&,
                          const std::string&) = work_of) {
  const auto parser = tpar::generated::Ll::create_parser();
  std::uint64_t before = cost(*parser, text(size));
  CHECK(before != 0);
  for (unsigned step = 1; step <= 2; ++step) {
    const std::uint64_t after = cost(*parser, text(size << step));
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
  doubling_costs_at_most_double("unclosed parens", 128,
                                [](unsigned n) { return unclosed(n, "("); });
  doubling_costs_at_most_double("unclosed calls", 64,
                                [](unsigned n) { return unclosed(n, "f("); });
  doubling_costs_at_most_double(
      "extra tokens under parens", 100, [](unsigned n) {
        return "let x = " + repeat("(", n) + "1" + repeat(" ;", n) + ");\n";
      });
  doubling_costs_at_most_double("noise under parens", 100, [](unsigned n) {
    return "let x = " + repeat("(", n) + "1" + repeat(" @# $%", n) + ");\n";
  });
}

// A choice whose arms read the same nesting dies with all of them at the
// fault, and so does the same choice at every level below: the readings are
// read once, not once per level.
void a_dead_end_deep_in_nesting_is_read_once() {
  doubling_costs_at_most_double(
      "predictions under unclosed parens", 64,
      [](unsigned n) { return unclosed(n, "("); }, predictions_of);
  doubling_costs_at_most_double(
      "predictions under unclosed calls", 64,
      [](unsigned n) { return unclosed(n, "f("); }, predictions_of);
  // ...and the choices between the levels that one arm alone goes on from are
  // answered with them.
  doubling_costs_at_most_double(
      "predictions under unclosed sums", 64,
      [](unsigned n) { return "let x = " + repeat("1+(", n) + "1;\n"; },
      predictions_of);
  doubling_costs_at_most_double(
      "predictions under unclosed arguments", 64,
      [](unsigned n) { return "let x = " + repeat("f(1,", n) + "1;\n"; },
      predictions_of);
  doubling_costs_at_most_double(
      "predictions under extra tokens", 64,
      [](unsigned n) {
        return "let x = " + repeat("(", n) + "1" + repeat(" ;", n) + ");\n";
      },
      predictions_of);
}

// Every level asks the plan at the one place past the last token: the answers
// it has given, and the choices that never ask, are not read again.
void a_plan_is_read_once(const char* prefix) {
  const auto parser = tllrepairgrowth::generated::Ll::create_parser();
  const auto visits = [&](unsigned depth) {
    cursiv::engine::LlPredictionWork::current() = {};
    const std::string input = prefix + repeat("a", depth) + "b";
    const cursiv::ParseResult result =
        parser->parse(input);
    CHECK(result.accepted && result.diagnostics.empty());
    return cursiv::engine::LlPredictionWork::current().planVisits;
  };
  std::uint64_t before = visits(1024);
  CHECK(before != 0);
  for (const unsigned depth : {2048u, 4096u}) {
    const std::uint64_t after = visits(depth);
    const bool within =
        static_cast<double>(after) <= kLinear * static_cast<double>(before);
    CHECK(within);
    if (!within)
      std::printf("plan visits of %s at depth %u: %llu -> %llu\n", prefix, depth,
                  static_cast<unsigned long long>(before),
                  static_cast<unsigned long long>(after));
    before = after;
  }
}

// A dangling `else` is decided by the token in hand: no reading walks the
// levels that stand open, which a count of tokens read does not show of
// itself, and that would be the square of the levels in time and in memory.
void a_dangling_choice_reads_nothing_ahead() {
  const auto parser = tllrepairgrowth::generated::Ll::create_parser();
  for (const unsigned depth : {1024u, 2048u}) {
    const std::string open = repeat("a ", depth);
    for (const std::string& text :
         {"dangling: " + open + repeat("b ", depth),
          "dangling: " + open + repeat("b ", depth / 2),
          "danglingtwo: " + open + repeat("b c ", depth),
          "danglingtwo: " + open + repeat("c ", depth / 2) +
              repeat("b ", depth / 2)}) {
      cursiv::engine::LlPredictionWork::current() = {};
      const cursiv::ParseResult result = parser->parse(text);
      CHECK(result.accepted && result.diagnostics.empty());
      CHECK(cursiv::engine::LlPredictionWork::current().tokens == 0);
      if (cursiv::engine::LlPredictionWork::current().tokens != 0)
        std::printf("dangling read ahead: %llu tokens at depth %u\n",
                    static_cast<unsigned long long>(
                        cursiv::engine::LlPredictionWork::current().tokens),
                    depth);
    }
  }
}

// Two loops over one token read the same after the choice of the first:
// each step is decided by the next token, and one fault far along is one
// diagnostic where it stands, however far along.
void loops_that_read_the_same_do_not_read_to_the_fault() {
  const auto parser = tllrepairgrowth::generated::Ll::create_parser();
  const auto read = [&](const std::string& text) {
    cursiv::engine::LlPredictionWork::current() = {};
    return parser->parse(text);
  };
  std::uint64_t before = 0;
  for (const unsigned turns : {512u, 1024u, 2048u}) {
    const std::string turned = "two: " + repeat("a ", turns);
    CHECK(read(turned + "b").diagnostics.empty());
    const cursiv::ParseResult broken = read(turned + "!");
    CHECK(broken.accepted && broken.diagnostics.size() == 1);
    if (!broken.diagnostics.empty())
      CHECK(broken.diagnostics.front().begin == turned.size());
    const std::uint64_t after =
        cursiv::engine::LlPredictionWork::current().tokens;
    const bool within =
        before == 0 ||
        static_cast<double>(after) <= kLinear * static_cast<double>(before);
    CHECK(within);
    if (!within)
      std::printf("two loops: %u turns read %llu tokens, %zu diagnostics\n",
                  turns, static_cast<unsigned long long>(after),
                  broken.diagnostics.size());
    before = after;
  }
}

// A first reading with an unknown caller cannot join a known one. Nor can a
// one-frame stack join a two-frame stack. Neither prevents the two compatible
// two-frame stacks from sharing their intermediate and consuming positions.
void compatible_closure_contexts_share_past_incompatible_readings() {
  const cursiv::engine::LlAtnState states[] = {
      {.edge = 0, .nedges = 1},
      {.edge = 1, .nedges = 1},
      {.edge = 2, .nedges = 1},
      {.kind = cursiv::engine::LlAtnKind::ruleStop, .rule = 0},
      {.edge = 3, .nedges = 1},
      {.edge = 4, .nedges = 1},
      {.edge = 5, .nedges = 1},
  };
  const cursiv::engine::LlAtnEdge edges[] = {
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 7, .target = 1},
      {.kind = cursiv::engine::LlEdgeKind::epsilon, .target = 2},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 8, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 9, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 10, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 11, .target = 3},
  };
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::engine::LlAtnView atn{
      .states = states, .nstates = 7, .edges = edges, .callerBase = callerBase};
  cursiv::engine::LlSimulation plain(atn), shared(atn);
  shared.share_epsilon_contexts();
  const auto start = [](cursiv::engine::LlSimulation& sim) {
    auto& pool = sim.contexts();
    const auto a = pool.push(pool.push(cursiv::engine::kLlCtxNone, 4), 6);
    const auto shortStack = pool.push(cursiv::engine::kLlCtxNone, 6);
    const auto b = pool.push(pool.push(cursiv::engine::kLlCtxNone, 5), 6);
    sim.load({{0, cursiv::engine::kLlCtxNone, 0},
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
          config.ctx == cursiv::engine::kLlCtxNone)
        continue;
      ++readings;
      const auto& pool = shared.contexts();
      CHECK(pool.count(config.ctx) == 1);
      CHECK(pool.ret(config.ctx, 0) == 6);
      const auto parent = pool.parent(config.ctx, 0);
      if (parent != cursiv::engine::kLlCtxNone) {
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
  const cursiv::engine::LlAtnState states[] = {
      {.edge = 0, .nedges = 1},
      {.edge = 1, .nedges = 1},
      {.edge = 2, .nedges = 1},
      {.edge = 3, .nedges = 1},
      {.edge = 4, .nedges = 1},
      {.edge = 5, .nedges = 1},
      {.kind = cursiv::engine::LlAtnKind::ruleStop, .rule = 0},
      {.edge = 6, .nedges = 1},
      {.edge = 7, .nedges = 1},
  };
  const cursiv::engine::LlAtnEdge edges[] = {
      {.kind = cursiv::engine::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::condition, .label = 1, .target = 4},
      {.kind = cursiv::engine::LlEdgeKind::condition, .label = 2, .target = 5},
      {.kind = cursiv::engine::LlEdgeKind::condition, .label = 1, .target = 3},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 8, .target = 6},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 9, .target = 6},
      {.kind = cursiv::engine::LlEdgeKind::atom, .label = 10, .target = 6},
  };
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::engine::LlAtnView atn{
      .states = states, .nstates = 9, .edges = edges, .callerBase = callerBase};
  cursiv::engine::LlSimulation plain(atn), shared(atn);
  shared.share_epsilon_contexts();
  const auto start = [](cursiv::engine::LlSimulation& sim) {
    sim.forget();
    const auto a = sim.contexts().push(cursiv::engine::kLlCtxNone, 7);
    const auto b = sim.contexts().push(cursiv::engine::kLlCtxNone, 8);
    sim.load({{0, cursiv::engine::kLlCtxNone, 0}, {1, a, 0}, {2, b, 0}});
  };
  const auto readings = [](cursiv::engine::LlSimulation& sim) {
    std::vector<std::vector<std::uint32_t>> out;
    const auto& pool = sim.contexts();
    for (const auto& c : sim.configs()) {
      if (c.ctx == cursiv::engine::kLlCtxNone) {
        out.push_back({c.state, c.alt, cursiv::engine::kLlCtxNone});
      } else {
        for (unsigned i = 0; i < pool.count(c.ctx); ++i) {
          CHECK(pool.parent(c.ctx, i) == cursiv::engine::kLlCtxNone);
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

// A closure reads the parse's stop as it visits its positions: a walk down a
// long run of positions that read nothing stops within a run of turns, and
// one with no stop to read walks it whole.
void a_closure_reads_the_stop_as_it_walks() {
  using cursiv::engine::LlAtnEdge;
  using cursiv::engine::LlAtnState;
  using cursiv::engine::LlEdgeKind;
  constexpr std::uint32_t kRun = 20000;
  std::vector<LlAtnState> states;
  std::vector<LlAtnEdge> edges;
  for (std::uint32_t i = 0; i < kRun; ++i) {
    states.push_back({.edge = i, .nedges = 1});
    edges.push_back({.kind = LlEdgeKind::epsilon, .target = i + 1});
  }
  states.push_back({.edge = kRun, .nedges = 1});
  edges.push_back({.kind = LlEdgeKind::atom, .label = 7, .target = kRun + 1});
  states.push_back({.kind = cursiv::engine::LlAtnKind::ruleStop, .rule = 0});
  const std::uint32_t callerBase[] = {0, 0};
  const cursiv::engine::LlAtnView atn{.states = states.data(),
                                      .nstates = kRun + 2,
                                      .edges = edges.data(),
                                      .callerBase = callerBase};
  cursiv::engine::LlSimulation whole(atn);
  whole.seed(0, cursiv::engine::kLlCtxNone);
  CHECK(whole.step(7) && whole.visits() > kRun);
  cursiv::CancellationTokenSource source;
  source.request_stop();
  const cursiv::CancellationToken token = source.get_token();
  cursiv::engine::LlSimulation stopped(atn);
  stopped.watch(cursiv::engine::CancellationPoll(token));
  bool threw = false;
  try {
    stopped.seed(0, cursiv::engine::kLlCtxNone);
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(stopped.visits() <= cursiv::engine::CancellationPoll::kTurns);
}

// A parse repairs its faults up to a share of work, and says where it stopped:
// enough lines to pass it hold a fault each.
void a_parse_has_a_share_of_repair_and_says_so() {
  const auto parser = tpar::generated::Ll::create_parser();
  const auto told = [&](unsigned count) {
    const std::string input = lines(count, 1);
    const auto result = parser->parse(input);
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
    cursiv::engine::LlSearchWork::current() = {};
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.value && !result.diagnostics.empty());
    CHECK(cursiv::engine::LlSearchWork::current().work < 200000);
  }
  for (const std::string text :
       {"c", "bc", "ca", "cb", "caa", "ccc", "ccccccc"}) {
    const auto result = parser->parse(text);
    CHECK(result.accepted && result.value && result.diagnostics.empty());
  }
}

}  // namespace

int main() {
  a_closure_reads_the_stop_as_it_walks();
  compatible_closure_contexts_share_past_incompatible_readings();
  a_condition_reads_every_new_context_of_a_published_union();
  recursive_inventions_share_their_continuations();
  a_parse_has_a_share_of_repair_and_says_so();
  a_fault_costs_the_same_in_a_longer_document();
  a_burst_costs_its_length();
  faults_one_after_another_cost_each_the_same();
  a_parse_costs_the_same_whatever_was_parsed_before();
  a_fault_deep_in_nesting_costs_what_it_reads_not_how_deep_it_stands();
  a_dead_end_deep_in_nesting_is_read_once();
  a_dangling_choice_reads_nothing_ahead();
  loops_that_read_the_same_do_not_read_to_the_fault();
  a_plan_is_read_once("nested:");
  a_plan_is_read_once("mixed:");
  return cursiv::test::report("LlRepairGrowth_test");
}
