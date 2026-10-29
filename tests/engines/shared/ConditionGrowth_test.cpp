// A condition tested at each token of a run reads each round of the run once
// per parse: four times the run costs four times the work, on both engines.

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "condgrowthll/generated/Ll/Module.hpp"
#include "condgrowthlr/generated/Lr/Module.hpp"
#include "cursiv/bench/PerfCounters.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/shared/runtime/PredicateRuns.hpp"
#include "cursiv/engines/shared/runtime/PredicateState.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/CaretOffers.hpp"
#include "engines/shared/ConditionGrowthGrammar.hpp"

namespace {

using cursiv::PredicateCursor;
using cursiv::PredicateState;
using cursiv::TextOffset;
using Items = std::vector<std::string>;

std::string repeat(std::string_view unit, unsigned n) {
  std::string s;
  for (unsigned i = 0; i < n; ++i) s += unit;
  return s;
}

// The items a parser reads, or one mark where it refuses the text.
Items items(const cursiv::Parser& parser, const std::string& text) {
  const auto r = parser.parse(text);
  const auto* doc = r.value_as<tcondgrowth::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return {"refused"};
  return {doc->items.begin(), doc->items.end()};
}

// A data rule's text joins its tokens.
void each_round_asks_the_rest_of_its_condition(const cursiv::Parser& p) {
  CHECK(items(p, "- - ;") == (Items{"-", "-", ";"}));
  CHECK(items(p, "- -->") == (Items{"--->"}));
  CHECK(items(p, "- - ; -- >") == (Items{"-", "-", ";", "-->"}));
  CHECK(items(p, "-- -- + ->") == (Items{"-", "-", "-", "-", "+", "->"}));
  CHECK(items(p, "! -") == (Items{"!-"}));
  CHECK(items(p, "! >") == (Items{"refused"}));
  CHECK(items(p, "m - = =") == (Items{"m-", "=", "="}));
  CHECK(items(p, "m - = = )") == (Items{"m-==)"}));
  CHECK(items(p, "m - )") == (Items{"m-)"}));
  CHECK(items(p, "c - = =") == (Items{"c-", "=", "="}));
  CHECK(items(p, "c - = = ;") == (Items{"c-==;"}));
  CHECK(items(p, "c - (") == (Items{"c-("}));
  CHECK(items(p, "t = = + + ;") == (Items{"t=", "=", "+", "+", ";"}));
  CHECK(items(p, "t = = ?") == (Items{"t==?"}));
  CHECK(items(p, "t = = ;") == (Items{"refused"}));
}

// A condition that reads past the caret is asked of what is typed there: the
// dash before it is an item where `;` follows, the arrow's where `>` does.
void a_caret_asks_the_run_of_what_is_typed(const cursiv::Parser& p) {
  const Items items{"!", "+", "-", ";", "=", ">", "c", "m", "t"};
  CHECK(caret_offers::offered(p, "- -", 3) == items);
  CHECK(caret_offers::offered(p, "m - - >", 5) == items);
}

// The instructions a parse retires per token; 0 where the counters are
// denied.
double per_token(const cursiv::bench::PerfCounters& counters,
                 const cursiv::Parser& parser, const std::string& text) {
  const cursiv::bench::PerfSample from = counters.read();
  counters.enable();
  const auto r = parser.parse(text);
  counters.disable();
  const cursiv::bench::PerfSample spent =
      cursiv::bench::perf_delta(from, counters.read());
  CHECK(r.accepted && r.diagnostics.empty());
  if (!spent.valid || spent.scaled) return 0;
  return static_cast<double>(spent.instructions) /
         static_cast<double>(text.size());
}

// Each dash of the run is tested: were each test to read to the run's end, a
// token would cost four times as much in four times the run.
void a_run_costs_its_length(const cursiv::Parser& parser) {
  const cursiv::bench::PerfCounters counters;
  if (!counters.active()) return;
  const auto dashes = [](unsigned n) { return repeat("-", n) + ";"; };
  (void)per_token(counters, parser, dashes(400));
  std::array<double, 3> cost{};
  for (unsigned i = 0; i < 3; ++i)
    cost[i] = per_token(counters, parser, dashes(400u << (2 * i)));
  if (cost[0] == 0 || cost[1] == 0 || cost[2] == 0) return;
  CHECK(cost[1] <= 1.5 * cost[0]);
  CHECK(cost[2] <= 1.5 * cost[1]);
}

// A run of dashes and then `>` or not, asked from each dash.
struct Dashes {
  static inline unsigned steps = 0;
  static PredicateCursor start(const char*, TextOffset, PredicateCursor p,
                               TextOffset*) {
    return p;
  }
  static PredicateState step(const char* src, TextOffset len, PredicateState in,
                             TextOffset* seen) {
    PredicateState out;
    for (std::size_t i = 0; i < in.size(); ++i) {
      ++steps;
      const TextOffset at = in[i].at;
      *seen = std::max(*seen, std::min(at + 1, len));
      if (at < len && src[at] == '-') out.add({at + 1, at + 1});
    }
    return out;
  }
  static bool rest(const char* src, TextOffset len, PredicateState in,
                   TextOffset* seen) {
    bool holds = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
      const TextOffset at = in[i].at;
      *seen = std::max(*seen, std::min(at + 1, len));
      holds = holds || (at < len && src[at] == '>');
    }
    return holds;
  }
};

// The rounds a test kept answer every later test at once, with what a test
// reading alone answers and reads.
void a_kept_round_answers_every_later_test() {
  constexpr cursiv::PredicateRound round{&Dashes::start, &Dashes::step, true};
  for (const std::string_view end : {">", ";"}) {
    const std::string text = repeat("-", 1000) + std::string(end);
    const auto len = static_cast<TextOffset>(text.size());
    cursiv::PredicateRuns runs;
    unsigned kept = 0, alone = 0;
    bool same = true;
    for (TextOffset at = 1; at <= 1000; ++at) {
      TextOffset seenKept = 0, seenAlone = 0;
      Dashes::steps = 0;
      const bool a =
          cursiv::predicate_run(&runs, 7, round, &Dashes::rest, text.data(),
                                len, PredicateState{{at, at}}, &seenKept);
      kept += Dashes::steps;
      Dashes::steps = 0;
      const bool b =
          cursiv::predicate_run(nullptr, 7, round, &Dashes::rest, text.data(),
                                len, PredicateState{{at, at}}, &seenAlone);
      alone += Dashes::steps;
      same = same && a == b && a == (end == ">") && seenKept == seenAlone;
    }
    CHECK(same);
    CHECK(kept <= 1001u);
    CHECK(alone > 500000u);
  }
}

// A dash and an optional comma: each round stands both at the comma and past
// it, so the cursors a run has visited grow by two a round.
struct DashComma {
  static PredicateState step(const char* src, TextOffset len, PredicateState in,
                             TextOffset*) {
    PredicateState out;
    for (std::size_t i = 0; i < in.size(); ++i) {
      const TextOffset at = in[i].at;
      if (at >= len || src[at] != '-') continue;
      out.add({at + 1, at + 1});
      if (at + 1 < len && src[at + 1] == ',') out.add({at + 2, at + 2});
    }
    return out;
  }
};

// Rounds that leave several cursors cost no more as the run grows.
void a_run_of_several_cursors_costs_its_length() {
  const cursiv::bench::PerfCounters counters;
  if (!counters.active()) return;
  constexpr cursiv::PredicateRound round{&Dashes::start, &DashComma::step,
                                         false};
  const auto per_round = [&](unsigned n) {
    const std::string text = repeat("-,", n) + ">";
    TextOffset seen = 0;
    const cursiv::bench::PerfSample from = counters.read();
    counters.enable();
    const bool holds = cursiv::predicate_run(
        nullptr, 0, round, &Dashes::rest, text.data(),
        static_cast<TextOffset>(text.size()), PredicateState{{0, 0}}, &seen);
    counters.disable();
    const cursiv::bench::PerfSample spent =
        cursiv::bench::perf_delta(from, counters.read());
    CHECK(holds);
    if (!spent.valid || spent.scaled) return 0.0;
    return static_cast<double>(spent.instructions) / n;
  };
  (void)per_round(2000);
  const double small = per_round(2000);
  const double large = per_round(8000);
  if (small == 0 || large == 0) return;
  CHECK(large <= 1.5 * small);
}

}  // namespace

int main() {
  auto ll = tcondgrowth::generated::Ll::create_parser();
  auto lr = tcondgrowth::generated::Lr::create_parser();
  for (const cursiv::Parser* parser : {ll.get(), lr.get()}) {
    each_round_asks_the_rest_of_its_condition(*parser);
    a_caret_asks_the_run_of_what_is_typed(*parser);
    a_run_costs_its_length(*parser);
  }
  a_kept_round_answers_every_later_test();
  a_run_of_several_cursors_costs_its_length();
  return cursiv::test::report("ConditionGrowth_test");
}
